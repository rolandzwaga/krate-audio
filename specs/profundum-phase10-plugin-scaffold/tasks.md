# Tasks: Profundum Phase 10 — Plugin Scaffold

**Spec:** `specs/profundum-phase10-plugin-scaffold/spec.md` (Clarifications Q1–Q7, 2026-10-22) · **Plan:**
`specs/profundum-phase10-plugin-scaffold/plan.md` (§1.3 P-1…P-13, §2–§7, open questions Q-A / Q-B) · **Roadmap:**
`specs/Profundum-roadmap.md` Part B (lines 748–751), Phase 10 (lines 753–774) · **Phase base:** `285613c5` on `synth/profundum`
**Status:** Task list (2026-10-22). The build stage has not started. No commit tasks: commits happen outside this workflow.

**Deliverables.** A new plugin tree `plugins/profundum/` (FR-008 skeleton), a new test target `profundum_tests` (nine
`TEST_CASE`s in nine TUs + `test_main.cpp` + fixture + stubs), and a Profundum entry at **every** roster outside `plugins/`
(FR-007, FR-070 – FR-080). **No new DSP. No file under `dsp/`, `tests/test_helpers/` or another plugin is modified (FR-090).**
All paths are repo-relative to `f:/projects/iterum`.

---

## Read this first (every executor)

### Canonical order inside every task

1. Write the failing test **first**, in the named file and `TEST_CASE`, with the exact assertions given. A section that is a
   regression guard for behaviour an earlier task already built says so and names its **non-vacuity arm** (the control that
   proves the assertion can fail).
2. Implement the minimum that makes it pass, exactly as the cited plan section pins it.
3. Zero warnings. "Pre-existing" is not a category.
4. Tests pass at the group gate.

**Executors do not build or run tests**; the group gate does, after every group. The gate builds `Profundum profundum_tests` and runs
`profundum_tests.exe '[profundum]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]'` (`.claude/workflows/profundum-phase.js:551–552`).
Every TEST_CASE of this phase therefore carries **`[profundum]`**. The one hidden case (`Profundum_ProcessorCpu`, `[.perf]`) carries it
too and runs only in the main-loop CPU lane (T031).

Commands for anyone who does run them (Windows, full CMake path; capture once, never re-run a slow suite for its output):

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target Profundum profundum_tests -- /nr:false 2>&1 | tee F:/tmp/p10-build.log | tail -5
grep -cE "warning C[0-9]|warning:" F:/tmp/p10-build.log            # must print 0 (SC-001)
build/windows-x64-release/bin/Release/profundum_tests.exe "[profundum]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/profundum_tests.exe "Profundum_StateRoundTrip" -c "Hostile" 2>&1 | tail -5
```

Catch2 filters are positional; tags go in brackets. Never `ctest -R profundum_tests` (CTest registers case names). The post-build
copy to `C:/Program Files/Common Files/VST3/` may fail with a permission error; compilation still succeeded.

### Parallelism

`[P]` tasks inside one group create **only brand-new files**, disjoint from every other task in that group. Every task that edits a
file that already exists — `processor.cpp`, `controller.cpp`, a pack header, a test TU created by an earlier group, any repo file outside
`plugins/profundum/` — is **alone in its group**. All test TUs link into the one `profundum_tests` binary, so gate builds never overlap.

### Portability and naming rules for every file of this phase (plan §4.5, P-10, P-13)

- Never `std::isnan` / `std::isinf` / `std::isfinite`. Use `Krate::DSP::detail::isFinite` (`dsp/include/krate/dsp/core/db_utils.h:118`
  float, `:125` double) or the fixture's `isFiniteBits`. Non-finite test values are built from bit patterns through a `volatile` (FR-062).
- No brace-init narrowing. Never `VoiceMode{index}`: write `idx == 1 ? VoiceMode::Poly : VoiceMode::Mono` (P-10). Fill `ProcessSetup` /
  `ProcessData` fields by explicit assignment.
- Never name a `kPlatformType*` constant in a test: use `Krate::TestSupport::nativePlatformType()`
  (`tests/test_helpers/editor_lifecycle_harness.h:57, :87`).
- `<allocation_operator_overrides.h>` is included by `tests/unit/test_main.cpp` only (FR-061).
- Plugin types are always spelled `::Profundum::…`. The test namespace is `ProfundumPluginTest`, never `ProfundumTest`
  (`Krate::DSP::ProfundumTest` exists in the dsp tests). Never `using namespace Krate::DSP::ProfundumTest`.
- Parameter titles are ASCII: `"Weight"`, `"Dark/Bright"`, `"Tight/Loose"`, `"Clean/Violent"` (P-13).
- Every `Processor` and `ProfundumEngine` in a test lives on the heap (FR-064).
- No bit-exact float golden (FR-065). Two renders in one process are compared with explicit mismatch counts or max-abs bars only.
- **Exact zero** means value equality `x == 0.0f`, which admits `-0.0f` (P-4). The sentinel-overwrite check compares bits against
  `std::bit_cast<std::uint32_t>(0.123f)`.

### Precondition table (plan items the tasks build at the plan's reading)

| Item | What the tasks build | If ruled otherwise | Task(s) |
|---|---|---|---|
| P-1 / **Q-A (open)** | SC-015 gates MONO ≤ 8 % and POLY4 ≤ 25 % at the registered defaults, plus the 1.05× ratio. **RULED Q-A (user, 2026-10-22): the macros-max arms are gated** — P-MAX-MONO ≤ 8 % and P-MAX-POLY ≤ 25 % (all four macros at 1.0), two more `REQUIRE`s in T020's case | nothing else changes | T020, T031 |
| P-2 | engine.h:15 reads "Latency 0." (stale). The wrapper follows engine.h:107 / :444. Recorded in a run-log; `dsp/` untouched | — | T013 |
| P-3 | `setActive(false)` calls `engine_->reset()` only when `engine_ && prepared_` | — | T013 |
| P-5 | `IdleIsExactZero` probe on the engine before any processor body; **stop rule** if red | — | T006, G3 gate |
| P-6 / **Q-B (RULED 2026-10-22)** | `.claude/workflows/release-readiness.js` **is** edited: `profundum: { testTarget: 'profundum_tests', bundle: 'Profundum.vst3' }` in `PLUGIN_MAP` (:13–22), one line in T024; the file is in the SC-019 allow-list (T028) | — | T024, T028 |
| P-7 | `tools/ci-test-times.json` is **not** edited; the per-push wall clock is recorded for a later refresh | — | T029 |
| P-8 | Pack defaults are **derived** from engine constants, tied by `static_assert`s | — | T004 |
| P-9 | `krate_test_pch(profundum_tests)` after the per-source flag blocks | — | T007 |
| P-11 | Containers are built before every `AllocationScope`; the count is read inside, asserted after | — | T018 |
| P-12 | `setupProcessing` refuses a non-`kSample32` size before `prepare` | — | T013 |

### Deviations from the plan made while cutting these tasks (surface them; do not silently revert)

1. **The in-tree CMake comes early (G3); the last group's single CMake task is an audit (T026).** A test cannot fail before
   `plugins/profundum/CMakeLists.txt`, `plugins/profundum/tests/CMakeLists.txt` and the root `add_subdirectory` exist, and CMake refuses to
   configure a list that names a missing source. G2 creates every FR-008 file (stubs or placeholders), G3 writes the three CMake sites once
   with the complete source lists, and no later task edits a CMake list except T026 (audit). Profundum Phase 9 deviation 1 and Vorago
   Phase 11 G4 are the precedent.
2. **Roster edits are split into one group per shared file (G17 – G21), not one group (plan §7.1, R-8).** The task format puts every
   shared-file edit alone in its group. `tools/lint-plugin-roster.js` is red from G3 (the directory exists) until T024. No gate runs the
   lint and nothing commits mid-build, so the red window is harmless. T024 is the first task that requires the lint green.
3. **The P-5 probe drives `ProfundumEngine` directly** (the fixture's `DirectEngine`), because no processor body exists when it runs. P-5
   is a property of the engine (plan §1.3). The processor-path exact-zero clauses are proven later by `Guards` and `Reset` (T014).
4. **Placeholder TUs contain no TEST_CASE.** G2 creates eight TUs that hold only includes and a comment naming the task that writes
   their case, so no case is ever vacuously green. `lifecycle_test.cpp` starts with the probe section only.
5. **`setupProcessing`, latency and guards (T013) precede the render loop (T014), and the render loop's `buildEventOrder` stays minimal
   (list order, clamped, at most 1024) until T015** adds the sort and the overflow queue test-first. This is the Vorago Phase 11 T013 / T014 /
   T015 split; it gives `EventOrder` and `Overflow` a real red.
6. **`lint-apple-globals.js` is added to the portability script (T030).** No spec row names it. It is a read-only check the repo runs
   before pushing (macOS-only `MAC` / `Rect` collisions), costs seconds and changes nothing.

### Verified API surface these tasks rely on (read this session at `285613c5`)

| Symbol | File:line |
|---|---|
| `ProfundumEngine::VoiceMode { Mono = 0, Poly = 1 }`, `kMaxVoices = 8`, `kDefaultPolyphony = 4` | `dsp/include/krate/dsp/systems/profundum_engine.h:97, :99, :100` |
| `kLimiterLookaheadSamples = 64`, `kDefaultCeilingDb` (−1), `kMinSampleRate = 22050.0`, `kMaxSampleRate = 192000.0` | engine.h `:107, :110, :118, :119` |
| `prepare(double, const ProfundumEngineConfig& = {})` (clamps fs at `:151–152`), `reset()` | engine.h `:147, :201` |
| `noteOn(uint8, uint8)`, `noteOff(uint8)`, `setTempoBpm(float)`, `setVoiceMode`, `setPolyphony(size_t)` (clamps to `[1, kMaxVoices]`), `setOutputCeilingDb`, `setMacros` | engine.h `:264, :280, :299, :326, :330, :364, :371` |
| `tempoBpm()`, `macros()`, `processBlock(float*, float*, size_t)`, `latencySamples()` (the limiter look-ahead: 0 before prepare), `droppedEvents()`, `stateFinite()` | engine.h `:380, :398, :422, :444, :446, :448` |
| `ProfundumMacro { Weight = 0, DarkBright, TightLoose, CleanViolent, Count }`; `ProfundumMacroValues` (4 × 0.5f); `ProfundumMacroMatrix::kNeutral = 0.5f`, `kNumMacros` | `dsp/include/krate/dsp/systems/profundum_macro_matrix.h:38, :57–62, :67, :68` |
| `TruePeakLimiter::setCeilingDb`: `ceilingLin_ = std::pow(10.0f, db * 0.05f)` | `dsp/include/krate/dsp/processors/true_peak_limiter.h:133–137` |
| `OnePoleSmoother(float initial)`, `configure(ms, fs)`, `setTarget`, `getTarget`, `getCurrentValue`, `process`, `snapTo` | `dsp/include/krate/dsp/primitives/smoother.h:148, :160, :170, :185, :191, :197, :263` |
| `detail::isFinite(float)` / `(double)` | `dsp/include/krate/dsp/core/db_utils.h:118, :125` |
| `PresetManager` explicit constructor | `plugins/shared/src/preset/preset_manager.h:55` |
| `createDropdownParameterWithDefault` | `plugins/shared/src/ui/parameter_helpers.h:47` |
| `Krate::Test::EventList::addEvent / addNoteOn(pitch, vel, offset) / addNoteOff(pitch, offset)` | `tests/test_helpers/vst_event_list.h:34, :58, :63, :77` |
| `TestHelpers::AllocationDetector::instance()`, `AllocationScope` | `tests/test_helpers/allocation_detector.h:20, :95, :111` |
| `Krate::TestSupport::exerciseEditorLifecycle(controller, templateName, uidescPath, cycles = 3)` | `tests/test_helpers/editor_lifecycle_harness.h:102–105` |
| Vorago `clampOffset`, `quantiseVelocity` | `plugins/vorago/src/processor/processor.cpp:48–54, :58–60` |
| Vorago overflow-queue block and `buildEventOrder` | V-proc `:373–380`, `:1097–1104` |
| Vorago lifecycle case and tag | `plugins/vorago/tests/unit/controller/editor_lifecycle_test.cpp:114` |
| Vorago fixture classes | `plugins/vorago/tests/vorago_test_fixture.h:60, :123, :191` |
| Vorago test CMake: `-fno-fast-math` block, CPU TU deliberately absent, `catch_discover_tests`, `krate_test_pch` | `plugins/vorago/tests/CMakeLists.txt:150–172` |
| Root `add_subdirectory` list ending in `plugins/vorago` | `CMakeLists.txt:494–501` |
| `.gitignore` Vorago trio | `.gitignore:79–81` |
| `GENERATORS`, `LINTS` | `tools/hooks/guard-ci-gates.js:31–35, :38–48` |
| `check-portability.js` usage (`<file> [...]`) | `tools/check-portability.js:21–24` |
| `run-cpu-tests.js` `DEFAULT_TARGETS`, `FILTER` | `tools/run-cpu-tests.js:45–51, :55` |
| `run-close-lanes.js` suite list | `tools/run-close-lanes.js:37–38` |
| `ci-test-shard.js` preset list | `tools/ci-test-shard.js:171–174` |
| `check-changelog-coverage.js` `PLUGINS` | `tools/check-changelog-coverage.js:50` |
| `gen-specs-index.js` `SUBSYSTEMS` (`seraphis`, `vorago` before the DSP keywords) | `tools/gen-specs-index.js:19–27` |
| `ci.yml`: 54 lines name Vorago, four of them comments (`:332, :612, :796, :1071`) | `.github/workflows/ci.yml` (grep this session) |
| `release.yml` `- vorago`, `hashFiles` key; `valgrind-nightly.yml` build / run lists | `release.yml:42, :143`; `valgrind-nightly.yml:284, :291` |
| `run-clang-tidy.ps1` `ValidateSet`, `"vorago"` case, `all`; `.sh` usage, `vorago)`, `all)` | `run-clang-tidy.ps1:66, :218–225, :240–251`; `run-clang-tidy.sh:67, :160–161, :177–178` |

---

## Group and task map

| Group | Tasks | Theme |
|---|---|---|
| G1 | T001 | Preflight: branch, base, ODR / subtype / GUID sweeps |
| G2 | T002 [P], T003 [P], T004 [P], T005 [P], T006 [P] | New files: packaging + leaf docs; identity headers + configs + entry; `src/` interface stubs; uidesc placeholder; test infrastructure + the P-5 probe |
| G3 | T007 | Plugin + tests `CMakeLists.txt`, root `add_subdirectory`, `.gitignore` (gate applies the P-5 stop rule) |
| G4 | T008 | Processor buses (SC-012) |
| G5 | T009 | Parameter latching and denormalisation (SC-010 processor arm) |
| G6 | T010 | Controller registration and display (SC-010 controller arm) |
| G7 | T011 | Processor state (SC-011.1 – .5) |
| G8 | T012 | Controller `setComponentState` (SC-011.6, `ControllerMirrorHostile`) |
| G9 | T013 | `setupProcessing`, latency, `setActive`, shape guards, not-ready path (SC-014, SC-017 parts) |
| G10 | T014 | Render loop: push, tempo, gain, slices, dispatch (SC-005, SC-006, SC-017.1 / .4 / .5, recovery arms) |
| G11 | T015 | Event sort, clamp, overflow queue (SC-009) |
| G12 | T016 | Parameter flow and audibility (SC-016) |
| G13 | T017 | Wrapper transparency and the hostile-gain ceiling (SC-008, SC-011.5 render arm) |
| G14 | T018 | Zero allocation (SC-007, SC-017.7) |
| G15 | T019 | Editor, placeholder surface, preset manager (SC-013) |
| G16 | T020 | `Profundum_ProcessorCpu` TU (SC-015; run by T031 only) |
| G17 | T021 | `.github/workflows/ci.yml` |
| G18 | T022 | `release.yml`, `valgrind-nightly.yml` |
| G19 | T023 | Both clang-tidy scripts |
| G20 | T024 | Tool lists: `check-changelog-coverage.js`, `gen-specs-index.js`, `run-cpu-tests.js`, `ci-test-shard.js`, `run-close-lanes.js` (roster lint goes green) |
| G21 | T025 | Root `CLAUDE.md` rosters |
| G22 | T026 → T031 | Integration: CMake registration audit (single task); regenerated artifacts + lints + generator checks; registration greps + FR inspection + scope fence; suites lane, clang-tidy, ASan (main loop); portability; CPU lane alone (main loop) |

**Run-log convention.** Every task writes the commands it ran (or the checks it performed) and their verbatim last lines to
`specs/profundum-phase10-plugin-scaffold/evidence/run-log/T0NN.txt`. Compliance quotes those files, never memory.

---

# GROUP 1 — Preflight

## T001 — Branch, base, sweeps, GUID collision check (no code)

**Files created:** `specs/profundum-phase10-plugin-scaffold/evidence/run-log/T001.txt`.

1. `git branch --show-current` prints `synth/profundum`; `git merge-base --is-ancestor 285613c5 HEAD` exits 0; `git status --porcelain`
   lists only ` M .serena/project.yml` and paths under `specs/profundum-phase10-plugin-scaffold/`. Anything else: STOP and report blocked.
2. ODR re-sweep (spec §New components). Each command prints 0 lines:
   `grep -rnw "namespace Profundum" dsp/ plugins/ tests/`;
   `grep -rn "makeProfundumEngineConfig\|kOutputGainSmoothMs\|makeProfundumPresetConfig\|makeProfundumUpdateConfig\|ProfundumPluginTest" dsp/ plugins/ tests/`.
3. AU subtype: `grep -rn "Prfd" plugins/ tools/ .github/ CMakeLists.txt cmake/ dsp/` prints 0 lines.
4. GUID collisions: for each 8-hex word of `94C64960 F7E844BE B439A0F3 95AF84C9` and `449C9192 E39F4DA0 AD6E7BCA 260F4CA0` (plan §2.1), and
   for the installer `AppId` `4DDA5FC2-4CD6-44E2-BF11-18887F0F6CD9`, `grep -rni` over `plugins/*/src/plugin_ids.h` and
   `plugins/*/installers/windows/setup.iss` prints 0 hits. A hit: STOP and report blocked. Never invent a replacement GUID silently.
5. Re-read engine.h `:97–119`, `:147`, `:201` and `:264–448` and confirm every engine row of "Verified API surface" above. A mismatch is a
   blocked report naming the row.

**Verify.** `T001.txt` holds every command and its output. No source file changes.

---

# GROUP 2 — New files (all [P]; every file below is brand new and owned by exactly one task)

## T002 [P] — Packaging, AU identity, installers, leaf `CLAUDE.md` (FR-002, FR-008 – FR-010, FR-015 – FR-017, FR-051, FR-056)

**Files created (all under `plugins/profundum/`):** `version.json`, `CHANGELOG.md`, `README.md`, `CLAUDE.md`, `resources/au-info.plist`,
`resources/auv3/audiounitconfig.h.in`, `resources/auv3/macOS/Profundum.entitlements`, `installers/windows/setup.iss`,
`installers/linux/README.txt`, `docs/.gitkeep`, `src/ui/.gitkeep`, `resources/presets/Sub/.gitkeep`.

- `version.json` (plan §2.12): exactly six keys, `{"version": "0.1.0", "name": "Profundum", "description": "VST3 Mass Bass Instrument",
  "publisher": "Krate Audio", "url": "https://krateaudio.com/profundum/", "copyright": "Copyright (c) 2026 Krate Audio"}`. No `preset_subdir`.
- `CHANGELOG.md`: `# Changelog`, then `## [0.1.0]` with scaffold bullets: identity, buses, the four global and four macro parameters,
  latency 64, `profundum_tests`, roster registration (FR-010).
- `README.md`: one paragraph, plus the build, test and pluginval commands for Profundum.
- `audiounitconfig.h.in`: a full copy of `plugins/vorago/resources/auv3/audiounitconfig.h.in` (re-open it; 40 lines) with `Vorago` →
  `Profundum` and `'Vrgo'` / `Vrgo` → `'Prfd'` / `Prfd`. Everything else byte-identical: `kSupportedNumChannels 02`, the flags, the delegate,
  `@AU_COMPONENT_VERSION@`, `kAUcomponentTag Synthesizer` (FR-015).
- `au-info.plist`: Vorago's file with the description → `Profundum Mass Bass Instrument`, the name → `Krate Audio: Profundum`, the subtype →
  `Prfd`. Exactly one `AudioComponents` dict and one `SupportedNumChannels` dict, `Inputs 0 / Outputs 2` (FR-016).
- `Profundum.entitlements`: a byte copy of `plugins/vorago/resources/auv3/macOS/Vorago.entitlements` (FR-017).
- `setup.iss`: Vorago's with `Vorago` → `Profundum` and `AppId={{4DDA5FC2-4CD6-44E2-BF11-18887F0F6CD9}`. `installers/linux/README.txt`:
  Vorago's, retitled.
- `CLAUDE.md` leaf (FR-009), in the shape of `plugins/vorago/CLAUDE.md`: identity (AU `aumu` / `Prfd` / `KrAt`, bundle
  `com.krateaudio.profundum`, the two FUIDs of plan §2.1, "never change"), roadmap pointer, *"No DSP lives in this plugin"*, skeleton,
  generated files, buses, the reserved ID table (spec §Reserved parameter-ID map), `profundum_tests` and the pluginval path. It ends with a
  section **"Decisions that outlive Phase 10"** carrying FR-009's nine items verbatim. Each of these anchors appears at least once:
  `64 samples`, `after the limiter`, `Polyphony`, `Sub`, `IMidiMapping`, `Mass and Morph`, `ProfundumPluginTest`, `22050`, `120 BPM`.

**Verify (executor, seconds).**
`node -e "const j=require('./plugins/profundum/version.json');console.log(Object.keys(j).length, 'preset_subdir' in j)"` prints `6 false`.
`diff <(sed 's/Vorago/Profundum/g;s/Vrgo/Prfd/g' plugins/vorago/resources/auv3/audiounitconfig.h.in) plugins/profundum/resources/auv3/audiounitconfig.h.in`
prints nothing. The nine `grep -c` anchors on the leaf are ≥ 1 each. Record in `T002.txt`.

## T003 [P] — `plugin_ids.h`, engine / preset / update configs, `entry.cpp` (FR-011 – FR-014, FR-018, FR-050, FR-052, FR-053)

**Files created:** `plugins/profundum/src/plugin_ids.h`, `src/engine/profundum_engine_config.h`, `src/preset/profundum_preset_config.h`,
`src/update/profundum_update_config.h`, `src/entry.cpp`.

These files are declarative. Later tests gate their behaviour: `kStateV1Bytes` in T011, `makeProfundumPresetConfig` in T019, the IDs in
T009 / T010, the factory by pluginval in T029.

- `plugin_ids.h`: **plan §2.1 verbatim**. `namespace Profundum`; `kCurrentStateVersion = 1`; `kStateV1Bytes = 36` with its `static_assert`;
  `kProcessorUID(0x94C64960, 0xF7E844BE, 0xB439A0F3, 0x95AF84C9)` and `kControllerUID(0x449C9192, 0xE39F4DA0, 0xAD6E7BCA, 0x260F4CA0)`;
  `static const char* const kSubCategories = "Instrument|Synth";` with the GCC-13 rationale copied from `plugins/vorago/src/plugin_ids.h:65–72`;
  `enum ParameterIDs : Steinberg::Vst::ParamID` with `kOutputGainId = 0, kVoiceModeId = 1, kPolyphonyId = 2, kOutputCeilingId = 3,
  kGlobalParamRangeEnd = 100, kMacroWeightId = 100, kMacroDarkBrightId = 101, kMacroTightLooseId = 102, kMacroCleanViolentId = 103,
  kMacroParamRangeEnd = 200` and the reserved-band comment.
- `profundum_engine_config.h`: plan §2.2 verbatim. `makeProfundumEngineConfig()` returns `Krate::DSP::ProfundumEngineConfig{}`;
  `inline constexpr float kOutputGainSmoothMs = 20.0f;`. No other function.
- `profundum_preset_config.h`: plan §2.7. Returns `{kProcessorUID, "Profundum", "Synth", {"Sub"}}` in the field order of
  `plugins/shared/src/preset/preset_manager_config.h:18–23`.
- `profundum_update_config.h`: a copy of `plugins/vorago/src/update/vorago_update_config.h` with the namespace and function renamed
  (`makeProfundumUpdateConfig`). It uses `stringPluginName` / `VERSION_STR` from the generated `version.h`.
- `entry.cpp`: `plugins/vorago/src/entry.cpp:23–76` with `Vorago` → `Profundum`, `#define stringPluginName "Profundum"` and exactly two
  `DEF_CLASS2`. The `#include <ui/arc_knob.h>` line is **removed** (FR-018: no `ui/*.h`).

**Verify (executor).** `grep -c DEF_CLASS2 plugins/profundum/src/entry.cpp` prints `2`; `grep -n '"ui/\|<ui/' plugins/profundum/src/entry.cpp`
prints nothing. Compilation is proven at the G3 gate.

## T004 [P] — `src/` interface stubs: processor, controller, parameter packs (compilable, behaviour-free)

**Files created:** `plugins/profundum/src/processor/processor.h`, `src/processor/processor.cpp`, `src/controller/controller.h`,
`src/controller/controller.cpp`, `src/parameters/global_params.h`, `src/parameters/macro_params.h`.

- `processor.h`: **plan §2.5.1 verbatim**. The full class declaration: every override; the FR-033 seams `engineForTest`,
  `globalParamsForTest`, `macroParamsForTest`, `outputGainValueForTest`, `lastSliceCountForTest`; the private `EventSlot`; the helper
  declarations; members `engine_`, `globalParams_`, `macroParams_`, `outputGain_{1.0f}`, `snapGainPending_ = true`, `prepared_ = false`,
  `lastSliceCount_ = 0`, `eventOrder_`; `kMaxEventsPerBlock = 1024`; and `static_assert(sizeof(Processor) < 64u * 1024u, …)`.
- `processor.cpp` **stubs**: the constructor calls `setControllerClass(kControllerUID)`. `initialize`, `terminate`, `setupProcessing`,
  `setActive` and `setBusArrangements` forward to `AudioEffect`. `process` returns `kResultOk`. `getLatencySamples` returns 0.
  `getState` / `setState` return `kResultFalse`. Private helpers have empty bodies. Unused parameters are unnamed or `[[maybe_unused]]`
  (C4100 is a warning).
- `controller.h`: plan §2.6 verbatim (no `IMidiMapping`, no `INoteExpressionController`; `presetManagerForTest()`; member
  `std::unique_ptr<Krate::Plugins::PresetManager> presetManager_`, with `preset_manager.h` included). `controller.cpp` stubs: every override
  forwards to `EditControllerEx1`; `createView` returns `nullptr`.
- `global_params.h`: plan §2.3's **declarations and constants**: the real `GlobalParams` struct, `kOutputGainFloorDb`,
  `kNumPolyphonyEntries = 8`, the derived defaults (`kDefaultPolyphony = static_cast<int>(Engine::kDefaultPolyphony)`,
  `kDefaultCeilingDb = Engine::kDefaultCeilingDb`, `kDefaultOutputGain = 1.0f`, `kDefaultVoiceModeIndex = 0`) and both `static_assert`s
  (`kNumPolyphonyEntries == Engine::kMaxVoices`; `kMinCeilingDb == -12 && kMaxCeilingDb == 0`). Function bodies are **inert stubs**:
  - mapping helpers return their field's default: `outputGainFromNormalized` → `1.0f`, `outputGainToNormalized` → `1.0`,
    `polyphonyFromNormalized` / `clampPolyphony` → `kDefaultPolyphony`, `ceilingFromNormalized` → `kDefaultCeilingDb`,
    `ceilingToNormalized` → `11.0 / 12.0`;
  - `handle…`, `register…` and `save…` do nothing; `format…` returns `kResultFalse`; `load…` returns `false`; `load…ToController` does
    nothing.
- `macro_params.h`: plan §2.4's real `MacroParams` struct with `{Krate::DSP::ProfundumMacroMatrix::kNeutral}` initialisers, `kNumMacroParams`
  and the three `static_assert`s. `macroField` (a switch over 0..3) and `macroValues` are real, because they are pure accessors. The six pack
  functions are inert stubs, as above.

**Verify.** Compiles with zero warnings at the G3 gate (no test exercises the stubs).

## T005 [P] — `resources/editor.uidesc` minimal placeholder (FR-054 shape; controls land in T019)

**File created:** `plugins/profundum/resources/editor.uidesc`.

A valid VSTGUI uidesc: a leading comment *"Phase 10 PLACEHOLDER — Phase 12 replaces this file wholesale; stock views only"*, an empty
`<control-tags>` block, and one template `"editor"` — a `CViewContainer` of size `420, 360` holding one untagged `CTextLabel` reading
`Profundum`. No bitmaps, no custom classes. Model the XML header and attribute sets on the Vorago Phase 11 placeholder
(`specs/vorago-phase11-plugin-scaffold/plan.md:932–941`). The file is non-empty, so `tools/check-bundle.js` accepts it. T019 adds the eight
controls test-first.

## T006 [P] — Test infrastructure, eight placeholder TUs, and the P-5 probe

**Files created (under `plugins/profundum/tests/`):** `vstgui_test_stubs.cpp`, `unit/test_main.cpp`, `profundum_test_fixture.h`,
`unit/processor_bus_test.cpp`, `unit/param_denorm_test.cpp`, `unit/state_roundtrip_test.cpp`, `unit/midi_event_test.cpp`,
`unit/lifecycle_test.cpp`, `unit/controller/editor_lifecycle_test.cpp`, `integration/processor_audio_test.cpp`,
`integration/param_flow_test.cpp`, `integration/processor_cpu_test.cpp`.

- `vstgui_test_stubs.cpp`: a copy of `plugins/vorago/tests/vstgui_test_stubs.cpp` (12 lines; it names no plugin).
- `unit/test_main.cpp`: a copy of `plugins/vorago/tests/unit/test_main.cpp` (29 lines): `#include <enable_ftz_daz.h>`, the **only**
  `#include <allocation_operator_overrides.h>`, `void* moduleHandle = nullptr;`, and `enableFTZDAZ();` before `Catch::Session().run()`.
- `profundum_test_fixture.h`, namespace `ProfundumPluginTest`: plan §4.2 in full, modelled on `plugins/vorago/tests/vorago_test_fixture.h`
  (`MultiPointParamValueQueue` at `:60`, `MultiParamChanges` at `:123`, `ProcessorFixture` at `:191`):
  - `MultiPointParamValueQueue` / `MultiParamChanges`, which keep their capacity across `clear()`.
  - `CountingConnectionPoint final : Steinberg::Vst::IConnectionPoint`: `connect` / `disconnect` → `kResultTrue`; `notify` →
    `++notifications`, `kResultTrue`; `addRef` / `release` → 1; `queryInterface` → `kNoInterface`.
  - `struct ProcessorFixture`: owns `std::unique_ptr<::Profundum::Processor> proc`. The constructor takes
    `(Steinberg::FUnknown* hostContext = nullptr)`, calls `initialize(hostContext)` and sizes the L/R buffers to 4096 plus guard words.
    `prepare(double sr = 48000.0, int32 maxBlock = 4096)` calls `setupProcessing` with `processMode = kRealtime`,
    `symbolicSampleSize = kSample32`, `maxSamplesPerBlock = maxBlock`, `sampleRate = sr` (assigned field by field), then `setActive(true)`, and
    returns the `setupProcessing` result. `processBlock(n, IEventList* = nullptr, IParameterChanges* = nullptr, ProcessContext* = nullptr)`
    checks the guard words and appends to `capturedL` / `capturedR` (capacity reserved up front, never grown inside a scope).
    `processNoOutputs(IParameterChanges*)`. `struct ScriptedEvent { int64 sample; Kind kind (NoteOn, NoteOff, Param); int16 pitch; float
    velocity; ParamID id; double value; }`. `renderScript(span<const ScriptedEvent>, total, hostBlock)` builds per-block `EventList`s and
    `MultiParamChanges` with in-block offsets. `sizeBuffers`, `writeGuards`, `guardsIntact`.
  - `struct DirectEngine`: `std::unique_ptr<Krate::DSP::ProfundumEngine> e`, prepared with `ProfundumEngineConfig{}` at a given rate;
    `OnePoleSmoother g{1.0f}` configured with `::Profundum::kOutputGainSmoothMs`. `render(span<const DirectEvent>, total)` splits only at
    event samples, calls `noteOn` / `noteOff` / `setVoiceMode` / `setPolyphony` at the exact sample (equal samples in script order), then
    `processBlock`, then the per-sample gain loop, into owned vectors. The caller owns the event span, so the 1100-event reference of T015 needs
    no capacity limit.
  - Helpers: `isFiniteBits(float)` (an exponent test on `std::bit_cast<std::uint32_t>`); `makeNaN()`, `makePosInf()` and `makeNaND()`
    (double), each built from a bit pattern through a `volatile`; `rmsDb(span, from, to)` (returns `-200.0` for exact zero), `peakOf`,
    `maxAbsDiff`, `mismatches(a, b)` (the count of `a[i] != b[i]`); `kE1 = 28`, `kChord4 = {28, 33, 40, 48}`,
    `kChord8 = {28, 33, 40, 45, 52, 57, 64, 69}`.
- The eight TUs other than `lifecycle_test.cpp`: includes (`<catch2/catch_test_macros.hpp>` and the fixture) and one comment line naming the
  task that writes their case (bus T008, denorm T009, state T011, midi T015, editor T019, audio T014, flow T016, cpu T020). **No TEST_CASE**
  (deviation 4).
- `unit/lifecycle_test.cpp`: `TEST_CASE("Profundum_ProcessorLifecycle", "[profundum][processor]")` with exactly one section,
  `SECTION("IdleIsExactZero")`. This is **the P-5 probe** (plan §1.3 P-5 and §4.3; deviation 3), run on a `DirectEngine` at 48 kHz:
  1. 4096 event-free samples: `REQUIRE` every sample `== 0.0f` on both channels; `WARN` the count of non-zero samples.
  2. NoteOn(28, velocity 100) at sample 0, NoteOff(28) at sample 24 000, render to 96 000 (2 s). **D-1 (user ruling 2026-10-22):**
     a `SumTapObserver` on the engine reads the last non-zero Sum-tap sample, `REQUIRE`d < 30 000 (0.125 s after the note-off: the
     100 ms release + the 32-sample latch + the declick; measured 28 798); every output sample from 48 000 (0.5 s after) is
     `<= 1e-6` in magnitude (measured: last > 1e-6 at 40 863); every output sample from 84 000 (1.25 s after) is `== 0.0f` on both
     channels (the global 5 Hz DC blocker's decay of the last voice sample flushes to exact zero at 72 463 + 64 look-ahead = 72 527;
     Phase 9's `VoiceEngine_ModeSwitch` read the same tail at 1.3 s). `WARN` all three readings. The hidden
     `Profundum_IdleTailProbe` (`[.][profundum][probe]`) prints every tap's decay (>1e-3 … exact zero) and stays in the TU.

  **Stop rule (G3 gate).** If either clause is red, the cause lives in `dsp/` (FR-090). The fixer MUST NOT edit `dsp/` and MUST NOT weaken
  the bar. It records the reading under "Defects found", and the stage stops for a user ruling. SC-009(a)'s `< 1e-6` bar does not depend on
  this probe.

**Verify.** Compiles at the G3 gate; `IdleIsExactZero` green there.

---

# GROUP 3 — In-tree build wiring

## T007 — Plugin and test `CMakeLists.txt`, root `add_subdirectory`, `.gitignore` (FR-001, FR-003 – FR-007, FR-060, FR-062, FR-070)

**Files:** `plugins/profundum/CMakeLists.txt` (new), `plugins/profundum/tests/CMakeLists.txt` (new), root `CMakeLists.txt` (edit), `.gitignore`
(edit).

- Plugin CMake (plan §5.1). Start from `plugins/vorago/CMakeLists.txt` (re-open it):
  - `krate_plugin_read_version(PROFUNDUM)`, `krate_plugin_configure_generated_files()`, `set(PLUGIN_NAME "${PROFUNDUM_NAME}")`;
  - `smtg_add_vst3plugin` with the **enumerated** list `src/entry.cpp src/plugin_ids.h src/version.h src/processor/processor.h
    src/processor/processor.cpp src/controller/controller.h src/controller/controller.cpp src/parameters/global_params.h
    src/parameters/macro_params.h src/engine/profundum_engine_config.h src/preset/profundum_preset_config.h
    src/update/profundum_update_config.h` plus the SDK `memorystream.cpp`;
  - link `sdk vstgui_support KrateDSP KratePluginsShared` PRIVATE; `src/` as a PRIVATE include directory;
  - `smtg_target_configure_version_file`; `krate_plugin_platform_setup(${PLUGIN_NAME} TAG PROFUNDUM BUNDLE_BASE com.krateaudio.profundum
    ENTITLEMENTS Profundum.entitlements KIND instrument)`; `smtg_target_add_plugin_resources(… resources/editor.uidesc)`;
    `krate_plugin_install_to_system`, `krate_plugin_install_presets`, `krate_plugin_set_warnings`;
    `if(VSTWORK_BUILD_TESTS) add_subdirectory(tests) endif()`.
  - **Vorago's C4459 block (`:130–141`) is not copied.** No `/wd`, `-Wno-` or `pragma warning(disable)` anywhere (FR-006).
- Test CMake: **plan §5.2 verbatim**. The ten test TUs; the second compilation of `processor.cpp` and `controller.cpp`; the five SDK sources
  and `vstgui_test_stubs.cpp`; link `KrateDSP KratePluginsShared Catch2::Catch2 test_helpers vstgui_support sdk`; the include directories;
  `cxx_std_20`; `PROFUNDUM_RESOURCES_DIR`; the `-fno-fast-math -fno-finite-math-only` block over the six named TUs under `Clang|GNU`, with
  `processor_cpu_test.cpp` deliberately absent and commented; `catch_discover_tests(profundum_tests REPORTER console)`;
  `krate_test_pch(profundum_tests)` last.
- Root `CMakeLists.txt`: `add_subdirectory(plugins/profundum)` after `add_subdirectory(plugins/vorago)` (`:501`).
- `.gitignore`: after `:81`, exactly `/plugins/profundum/resources/win32resource.rc`, `/plugins/profundum/src/version.h` and
  `/plugins/profundum/resources/auv3/audiounitconfig.h` (the leading-slash Vorago form).

**Verify (group gate).** Configure (`"$CMAKE" --preset windows-x64-release`; the root list changed) and build `Profundum profundum_tests`
with 0 warning lines. `profundum_tests.exe "[profundum]"` runs `Profundum_ProcessorLifecycle` (`IdleIsExactZero`) green, or the T006 stop
rule applies. After the build, `git status --porcelain plugins/profundum` lists only untracked sources, never `version.h`, `win32resource.rc`
or `audiounitconfig.h` (an early SC-021 check; record in `T007.txt`).

---

# GROUP 4

## T008 — Processor buses (FR-020, FR-021, FR-022) — SC-012

**Files:** `plugins/profundum/tests/unit/processor_bus_test.cpp` (owner), `src/processor/processor.cpp`.

**Test first.** `TEST_CASE("Profundum_ProcessorBusSetup", "[profundum][processor]")`, heap `Processor`, `initialize(nullptr)`:
- `SECTION("BusCounts")`: `getBusCount(kAudio, kInput) == 0`, `getBusCount(kEvent, kInput) == 1`, `getBusCount(kAudio, kOutput) == 1`;
  `getBusInfo(kAudio, kOutput, 0, info)` gives `info.channelCount == 2`; `engineForTest() != nullptr` after `initialize` and `== nullptr`
  after `terminate()`.
- `SECTION("Arrangements")`: `setBusArrangements(nullptr, 0, &stereo, 1) == kResultTrue`; `(&stereo, 1, &stereo, 1) == kResultFalse`;
  `(nullptr, 0, &mono, 1) == kResultFalse`; `(nullptr, 0, twoStereo, 2) == kResultFalse`.

Both fail against the T004 stub (no buses, no engine).

**Implement** (plan §2.5.2, §2.5.3):
- `initialize`: the base, then `addEventInput(STR16("Event In"))` and `addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo)`, never
  `addAudioInput` (copy the `-10875` comment from V-proc:130–149); then `engine_ = std::make_unique<Krate::DSP::ProfundumEngine>()`. Never
  `setObserver`.
- `terminate`: `engine_ = nullptr; prepared_ = false;`, then the base. Assign `nullptr`; never `.reset()` (V-proc:151–158).
- `setBusArrangements`: V-proc:167–181.

**Verify (gate).** `Profundum_ProcessorBusSetup` passes; zero warnings.

---

# GROUP 5

## T009 — Parameter latching and denormalisation (FR-024.1 – .2, FR-042, FR-043) — SC-010 processor arm

**Files:** `tests/unit/param_denorm_test.cpp` (owner), `src/parameters/global_params.h`, `src/parameters/macro_params.h`,
`src/processor/processor.cpp`.

**Test first.** `TEST_CASE("Profundum_ParamDenormRoundTrip", "[profundum][params]")` on an initialised, **unprepared** `ProcessorFixture`.
Each check sends a one-point `MultiParamChanges` through `processNoOutputs` and reads `globalParamsForTest()` / `macroParamsForTest()`.
- `SECTION("GridValues")`, for `v ∈ {0, 0.25, 0.5, 0.75, 1}`:
  - gain: `== 0.0f` at 0 and `== 1.0f` at 1; at 0.25 / 0.5 / 0.75, `std::abs(g − e) <= 1e-5 * e` with
    `e = float(std::pow(10.0, (-60.0 + 60.0 * v) / 20.0))` (0.0056234 / 0.0316228 / 0.1778279); every value read is in `[0, 1]`;
  - voice mode `{0, 0, 1, 1, 1}`; polyphony exactly `{1, 3, 5, 6, 8}`;
  - ceiling `std::abs(c − (-12 + 12 v)) <= 1e-5`; each macro `std::abs(m − v) <= 1e-6`.
- `SECTION("LastPointWins")`: one queue for `kMacroWeightId` holding `{0.1 @ 0, 0.9 @ 100}` stores 0.9 (within 1e-6); the same queue on
  `kOutputCeilingId` stores `-12 + 12 · 0.9` (within 1e-5).
- `SECTION("UnregisteredIds")`: snapshot all eight atomics, send 0.3 on IDs 4, 99, 104, 199, 200 and 1000, and the snapshot is unchanged (`==`).
- `SECTION("NaNIgnored")`: after setting known values, `makeNaND()` on each of the eight IDs changes no atomic; `+Inf` likewise.

All four fail against the T004 stubs (`process` does not latch; the mapping stubs return defaults).

**Implement.**
- Packs (plan §2.3, §2.4, §3.1): the real mapping helpers — `outputGainFromNormalized` (exactly 0 at `v <= 0`, exactly 1 at `v >= 1`, else
  `float(std::pow(10.0, (kOutputGainFloorDb + 60.0 * v) / 20.0))`), `outputGainToNormalized`, `polyphonyFromNormalized` =
  `clamp(int(v * 7 + 1 + 0.5), 1, 8)`, `clampPolyphony`, `ceilingFromNormalized`, `ceilingToNormalized`. `handleGlobalParamChange` follows
  the plan §2.3 table (IDs 4–99 store nothing). `handleMacroParamChange` returns before computing the index for an ID outside `[100, 103]`
  and stores `clamp(float(v), 0, 1)`. Every store is `memory_order_relaxed`.
- `processor.cpp`: `processParameterChanges` (plan §2.5.8): the last point; `if (!detail::isFinite(value)) continue;`; clamp to `[0, 1]`;
  `id < kGlobalParamRangeEnd` → global, `id < kMacroParamRangeEnd` → macro, else ignored. `process()` gains only its prologue:
  `const Krate::DSP::ScopedDenormalMode denormalGuard;` as the **first statement**, then `processParameterChanges(data.inputParameterChanges)`,
  then guard 2 (`numOutputs <= 0 || outputs == nullptr` → `return kResultOk`). The rest stays a stub `return kResultOk;` until T013.

**Verify (gate).** The four sections pass; zero warnings.

---

# GROUP 6

## T010 — Controller registration and display (FR-040 format, FR-047) — SC-010 controller arm

**Files:** `tests/unit/param_denorm_test.cpp`, `src/parameters/global_params.h`, `src/parameters/macro_params.h`,
`src/controller/controller.cpp`.

**Test first.** Append to `Profundum_ParamDenormRoundTrip`, on a heap `Controller` after `initialize(nullptr)`:
- `SECTION("RegisteredInfo")`: `getParameterCount() == 8`. For each ID, the `getParameterInfo` default equals
  `{0: 1.0, 1: 0.0, 2: 3.0/7.0, 3: 11.0/12.0, 100..103: 0.5}` within 1e-12. Polyphony `stepCount == 7`; voice mode `stepCount == 1`.
  `getParamStringByValue(kPolyphonyId, k / 7.0)` reads `"1"`…`"8"` for `k = 0…7`; voice mode reads `"Mono"` / `"Poly"`.
- `SECTION("Display")`: `getParamStringByValue(kOutputGainId, 0.0)` → `"-inf dB"`; `(…, 1.0)` → `"0.0 dB"`; `(…, 0.9)` → `"-6.0 dB"`;
  `(kOutputCeilingId, 11.0/12.0)` → `"-1.0 dBTP"`; `(kMacroWeightId, 0.5)` → `"50%"`.

Both fail against the stubs (0 parameters).

**Implement** (plan §2.3, §2.4, §2.6):
- `registerGlobalParams`: output gain `addParameter(STR16("Output Gain"), STR16("dB"), 0, 1.0, kCanAutomate, kOutputGainId)`; voice mode
  and polyphony through `createDropdownParameterWithDefault`, then `->getInfo().defaultNormalizedValue = 0.0` / `3.0 / 7.0` by hand; ceiling
  `addParameter(STR16("Output Ceiling"), STR16("dBTP"), 0, 11.0 / 12.0, kCanAutomate, kOutputCeilingId)`.
- `registerMacroParams`: four plain parameters, unit `"%"`, default 0.5, the ASCII titles of P-13.
- `formatGlobalParam`: `"-inf dB"` at `v <= 0`, else `"%.1f dB"` of `-60 + 60 v`; the ceiling as `"%.1f dBTP"`; the two lists return
  `kResultFalse`. `formatMacroParam`: `"%.0f%%"`.
- `Controller::initialize`: the base, `registerGlobalParams(parameters)`, `registerMacroParams(parameters)` (the `PresetManager` lands in
  T019). `getParamStringByValue`: global, then macro, then the base (V-ctl:252–268). `terminate`: the base.

**Verify (gate).** Both sections pass and the earlier sections stay green; zero warnings.

---

# GROUP 7

## T011 — Processor state (FR-044, FR-045) — SC-011.1 – .5 (processor arm)

**Files:** `tests/unit/state_roundtrip_test.cpp` (owner), `src/parameters/global_params.h`, `src/parameters/macro_params.h`,
`src/processor/processor.cpp`.

**Test first.** `TEST_CASE("Profundum_StateRoundTrip", "[profundum][state]")`, with `Steinberg::MemoryStream`:
- `SECTION("ByteIdentical36")`: through `processNoOutputs`, set gain 0.25 (normalized), Poly, polyphony normalized 5/7 (→ 6), ceiling
  normalized 0.75 (→ −3) and macros `{0.1, 0.2, 0.8, 0.9}`. `getState` → a fresh processor's `setState` → `getState`. Both streams are exactly
  `::Profundum::kStateV1Bytes` (36) bytes and `memcmp == 0`.
- `SECTION("DefaultDecode")`: a default processor's stream decodes (`IBStreamer`, little-endian) to `int32 1`, `1.0f`, `0`, `4`, `-1.0f`,
  `0.5f × 4`.
- `SECTION("FutureVersion")`: a stream with version `kCurrentStateVersion + 1` followed by valid fields → `setState == kResultFalse`, and
  every atomic is unchanged.
- `SECTION("Truncated")`: on a processor holding known macros, a valid stream cut at 20 bytes loads the global block and leaves the macros
  unchanged.
- `SECTION("Hostile")`: each stream is loaded on a processor pre-set to known non-default values:
  - polyphony 99 → 8; polyphony 0 → 1; ceiling +20 → 0; ceiling −50 → −12;
  - `outputGain` 4.0 → 1.0; −1.0 → 0.0; a macro of 5.0 → 1.0;
  - `voiceMode` 7 and −1 → unchanged;
  - NaN and +Inf (bit patterns through `volatile`, FR-062) in each of the six float fields in turn → that atomic unchanged, and every later
    field still loaded.

All five fail against the stubs (`kResultFalse`, empty stream).

**Implement** (plan §2.3, §2.4, §2.5.10, §3.4): `saveGlobalParams` / `loadGlobalParams` / `saveMacroParams` / `loadMacroParams` with the
FR-045 rules (a failed read returns `false` and stops). `getState`: null → `kResultFalse`; `IBStreamer s(state, kLittleEndian)`; version,
globals, macros. `setState`: null → `kResultFalse`; a failed version read → `kResultFalse`; version `> kCurrentStateVersion` → `kResultFalse`
before any field is read; `loadGlobalParams(...) && loadMacroParams(...)`; `kResultOk`. `setState` touches atomics only and makes no engine
call.

**Verify (gate).** All five sections pass; zero warnings.

---

# GROUP 8

## T012 — Controller `setComponentState` (FR-045 controller side, FR-046) — SC-011.6, `ControllerMirrorHostile`

**Files:** `tests/unit/state_roundtrip_test.cpp`, `src/parameters/global_params.h`, `src/parameters/macro_params.h`,
`src/controller/controller.cpp`.

**Test first.** Append to `Profundum_StateRoundTrip`:
- `SECTION("ControllerMirror")`: the `ByteIdentical36` stream goes through `setComponentState` on an initialised `Controller`. Every
  `getParamNormalized` is within 1e-6 of the expected value (gain within 1e-5 of 0.25, voice mode 1.0, polyphony 5/7, ceiling 0.75, macros
  `{0.1, 0.2, 0.8, 0.9}`). Inverse-taper points, each through a stream that changes only the gain: gain 1.0 → 1.0, gain 0.0 → 0.0, gain
  `10^(−30/20)` → 0.5 within 1e-5. Version 2 → `kResultFalse` and no parameter moves.
- `SECTION("ControllerMirrorHostile")` (plan §4.3 SC-011 step 9): a fresh controller is first loaded with the `ByteIdentical36` stream, then
  with each hostile stream of `Hostile`; read all eight `getParamNormalized`:
  - NaN / +Inf in a float field → that ID unchanged (`==` within 1e-12);
  - `voiceMode` 7 and −1 → unchanged;
  - polyphony 99 → exactly `1.0`, 0 → exactly `0.0`; ceiling +20 → `1.0`, −50 → `0.0`; gain 4.0 → `1.0`, −1.0 → `0.0`; macro 5.0 → `1.0`;
  - every value in `[0, 1]`.

Both fail against the stubs.

**Implement** (plan §2.3, §2.6): `loadGlobalParamsToController` / `loadMacroParamsToController` apply the FR-045 rules **before** they
normalise — gain → `outputGainToNormalized`; voice mode → `double(idx)`, with **no call** for an invalid value; polyphony → `(n − 1) / 7.0`;
ceiling → `ceilingToNormalized`; macros → clamp. A non-finite field makes **no** call. `Controller::setComponentState`: null →
`kResultFalse`; version > current → `kResultFalse`; `set = [this](ParamID id, double v) { setParamNormalized(id, v); }`.

**Verify (gate).** Both sections pass; zero warnings.

---

# GROUP 9

## T013 — `setupProcessing`, latency, `setActive`, shape guards, not-ready path (FR-019, FR-023, FR-030 – FR-032, P-3, P-12) — SC-014, SC-017.2 / .3 / .6 / .8 / .9 (silence parts), `SampleSize64` steps 1–3

**Files:** `tests/unit/lifecycle_test.cpp`, `src/processor/processor.cpp`.

**Test first.** Append these sections to `Profundum_ProcessorLifecycle`, after `IdleIsExactZero`:
- `SECTION("Latency")`: a heap processor before `initialize` → `getLatencySamples() == 0`. After `initialize`, before any setup → `== 64` and
  `== ProfundumEngine::kLimiterLookaheadSamples`. For each fs in `{22050, 44100, 48000, 88200, 96000, 192000}`: `setupProcessing ==
  kResultOk`, latency `== 64` and `== engineForTest()->latencySamples()`. Still 64 after a second setup at 44100, after `setActive(false)` /
  `setActive(true)`, after a parameter change on each of the eight IDs, and after a refused rate (384000). After `terminate()`: 0.
- `SECTION("NoLatencyMessage")`: a stack `Steinberg::Vst::HostApplication host;` declared before the fixture
  (`extern/vst3sdk/public.sdk/source/vst/hosting/hostclasses.h:32`); `ProcessorFixture f(&host)`; `CountingConnectionPoint peer;
  f.proc->connect(&peer)`. Run the whole `Latency` sequence → `peer.notifications == 0`. Positive control:
  `f.proc->sendTextMessage("probe") == kResultOk` and then `peer.notifications == 1`. Disconnect before teardown.
- `SECTION("NotReady")`: a never-prepared fixture with buffers pre-filled with `0.123f`; one 512 block carrying NoteOn(28) @0 → `kResultOk`;
  no sample's bits equal `bit_cast(0.123f)`; every sample `== 0.0f`; `silenceFlags == 3`; `droppedEvents()` unchanged.
- `SECTION("SampleSize64")` steps 1–3 (P-12), on a never-prepared fixture and on one prepared at 48 kHz: `setupProcessing` with
  `symbolicSampleSize = kSample64` at 48 kHz → `kResultFalse`. A valid 32-bit 512 block pre-filled with `0.123f` and carrying NoteOn(28) @0
  takes the not-ready path: every sample `== 0.0f`, no sentinel bits, `silenceFlags == 3`. Latency stays 64. (Step 4, the recovery render,
  is T014's.)
- `SECTION("UnsupportedRate")` (silence parts): `setupProcessing` at 384000, at 11025 and at a NaN rate (`makeNaND()`) each return
  `kResultFalse`. After each, on a never-prepared fixture and on one prepared at 48 kHz, a 512 block with NoteOn(28) @0 and the sentinel →
  `kResultOk`, every sample `== 0.0f`, `silenceFlags == 3`, latency 64, `droppedEvents()` unchanged. 22050 and 192000 → `kResultOk`. (The
  recovery render is T014's.)
- `SECTION("PushInSetup")` (SC-017.6): a never-prepared processor; `setState` with polyphony 2, voice mode 1, ceiling −6, macros
  `{0.1, 0.2, 0.3, 0.4}` and gain 1.0; `setupProcessing` at 48 kHz with **no** `process()`: `polyphony() == 2`,
  `voiceMode() == VoiceMode::Poly`, `outputCeilingDb() == -6.0f`, and `macros()` equals the loaded floats exactly.
- `SECTION("NoMidiInterfaces")` (SC-017.8, FR-019): on a heap `Controller`, `queryInterface(Steinberg::Vst::IMidiMapping::iid, &obj)` and
  `queryInterface(Steinberg::Vst::INoteExpressionController::iid, &obj)` each return `kNoInterface`. This is a regression guard; what makes it
  pass is `controller.h` declaring neither base.

`Latency`, `NoLatencyMessage` (the latency part), `NotReady`, `SampleSize64`, `UnsupportedRate` and `PushInSetup` fail against the stubs
(latency 0 after initialize; the stub `process` writes nothing, so the sentinel survives; no refusal; no push).

**Implement.**
- `setupProcessing`: **plan §2.5.4 verbatim**. Null engine → `prepared_ = false`, return the base. A non-finite or out-of-range rate →
  `prepared_ = false`, `return kResultFalse` (no base call, no prepare). `canProcessSampleSize(setup.symbolicSampleSize) != kResultTrue` → the
  same. Then `engine_->prepare(sr, makeProfundumEngineConfig())`; `outputGain_.configure(kOutputGainSmoothMs, float(sr))`;
  `snapGainPending_ = true`; `pushParamsToEngine()`; `prepared_ = true`; return the base.
- `pushParamsToEngine` (plan §2.5.7): `setVoiceMode(mode == 1 ? Poly : Mono)`, `setPolyphony(size_t(clampPolyphony(...)))`,
  `setOutputCeilingDb(...)`, `setMacros(macroValues(macroParams_))`.
- `setActive` (plan §2.5.5, P-3): `state != 0` → `snapGainPending_ = true`; else `if (engine_ && prepared_) engine_->reset();`; return the base.
- `getLatencySamples` (plan §2.5.9): `engine_ ? uint32(kLimiterLookaheadSamples) : 0u`. No `restartComponent`, no `IMessage`.
- `process()`: after the T009 prologue, guards 3–7 in the FR-030 order (plan §2.5.6). Guard 7 (`!prepared_ || !engine_`) zero-fills both
  channels with `std::fill_n`, sets `silenceFlags = 3` and returns `kResultOk`. The rendered path stays a stub (`return kResultOk;`) until
  T014.
- In `T013.txt`, record P-2: engine.h:15 reads "Latency 0." while `:107` / `:444` give 64. It is a note for a later `dsp/` change and is not
  edited here.

**Verify (gate).** The seven sections pass; zero warnings.

---

# GROUP 10

## T014 — The render loop (FR-024 – FR-028, FR-029) — SC-005, SC-006, SC-014 reality check, SC-017.1 / .4 / .5, recovery arms

**Files:** `tests/integration/processor_audio_test.cpp` (owner), `tests/unit/lifecycle_test.cpp`, `src/processor/processor.cpp`.

**Test first.**

`TEST_CASE("Profundum_ProcessorRendersHeldNote", "[profundum][processor][audio]")`:
- `SECTION("HeldNote")` (SC-005): 48 kHz, 512-sample blocks, registered defaults, `NoteOn(28, 100/127.f)` @0, 2 s (188 blocks):
  `rmsDb(L, 24000, 96000) >= -40.0` and the same for R; every sample passes `isFiniteBits`; `WARN` the peak and both RMS values.
- `SECTION("Ceiling")` (SC-006): block 0 carries parameter points for voice mode 1.0, polyphony 1.0, the four macros at 1.0 and gain 1.0,
  plus the eight `kChord8` NoteOns @0 at velocity `100/127.f`; render 4 s (375 blocks).
  - `const float ceil1 = std::pow(10.0f, -1.0f * 0.05f);` (the limiter's own expression, `true_peak_limiter.h:136`): `peakOf(L) <= ceil1`
    and `peakOf(R) <= ceil1`.
  - Non-vacuity: `max(peakL, peakR) >= 0.5f * ceil1`. If it is not, make the script louder; never move the bar.
  - Then, at a block start `b`, a point `kOutputCeilingId = 0.0`. From sample `b · 512 + 64` to the end of a further 2 s:
    `peak <= std::pow(10.0f, -12.0f * 0.05f)` on both channels.
  - `WARN` every peak.

Append to `Profundum_ProcessorLifecycle`:
- `SECTION("LatencyIsReal")` (SC-014): a fresh fixture prepared at 48 kHz, NoteOn(28) @0, one 512 block. The first index with `x != 0.0f` on
  either channel lies in `[64, 68]`; `WARN` it.
- `SECTION("Guards")` (SC-017.1): a prepared fixture with no note sounding. For each guard shape — numOutputs 0; `outputs == nullptr`;
  `channelBuffers32 == nullptr`; `numChannels == 1`; `numSamples == 0`; one null channel pointer — a block carrying NoteOn(28) @0 returns
  `kResultOk`. After each, the next valid 512 block with **no** events reads `== 0.0f` everywhere, and `voiceMode()`, `polyphony()`,
  `outputCeilingDb()`, `macros()` and `droppedEvents()` are unchanged. Non-vacuity arm: the same NoteOn(28) @0 in a valid block makes the
  next event-free block non-silent (`peakOf > 1e-4`).
- `SECTION("Reset")` (SC-017.4): hold E1 for 0.5 s, `setActive(false)`, `setActive(true)`, one event-free 512 block → every sample `== 0.0f`,
  and `stateFinite()` is true. Non-vacuity: without the `setActive` pair, the block is non-silent.
- `SECTION("GainSnap")` (SC-017.5): a never-prepared processor; `setState` with gain 0.5 (linear); `setupProcessing` at 48 kHz; one block →
  `outputGainValueForTest() == 0.5f` exactly. Then `setState` with gain 0.25, `setActive(false)`, `setActive(true)`, one block → `== 0.25f`
  exactly.
- Append the recovery arms to `SampleSize64` (step 4) and `UnsupportedRate`: a `kSample32` setup at 48000 → `kResultOk`; E1 held 1 s →
  `rmsDb(·, 24000, 48000) >= -40` on each channel.

All of these fail against T013's stub rendered path (nothing is written, the onset is never found, the snap never runs).

**Implement** (plan §2.5.6, §3.1, §3.2):
- After guard 7: `pushParamsToEngine()`. Host tempo (FR-027): `if (data.processContext && (data.processContext->state &
  ProcessContext::kTempoValid)) engine_->setTempoBpm(float(data.processContext->tempo));`. Then
  `outputGain_.setTarget(globalParams_.outputGain.load(relaxed))` and
  `if (snapGainPending_) { outputGain_.snapTo(outputGain_.getTarget()); snapGainPending_ = false; }`.
- `buildEventOrder`, **minimal for this task**: copy up to `kMaxEventsPerBlock` events **in list order** into `eventOrder_` with
  `clampOffset` (V-proc:48–54). No sort, no overflow queue; T015 adds both test-first.
- The slice loop: `lastSliceCount_ = 0`; `while (cursor < total)`: dispatch **every** pending event whose effective offset is `<= cursor` (a
  `while`, so equal offsets never make a zero-length slice); `sliceEnd = havePending ? pendingOffset : total`;
  `engine_->processBlock(outL + cursor, outR + cursor, sliceEnd − cursor)`; `applyOutputGain(...)`; `++lastSliceCount_`. **No slice cap.** No
  scratch buffer.
- `dispatchEvent` (FR-026): a `kNoteOnEvent` with a pitch outside `[0, 127]` is dropped; `velocity > 0` → `noteOn(pitch,
  quantiseVelocity(v))` (`uint8(clamp(v · 127 + 0.5, 1, 127))`, V-proc:58–60); otherwise `noteOff`. An in-range `kNoteOffEvent` → `noteOff`.
  Every other type is ignored.
- `applyOutputGain`: per sample, `const float g = outputGain_.process(); l[s] *= g; r[s] *= g;`.
- `data.outputs[0].silenceFlags = 0` on every rendered block. `getTailSamples` is not overridden.

**Verify (gate).** The new sections pass and every earlier section stays green; the `WARN` figures are copied into `T014.txt`; zero warnings.

---

# GROUP 11

## T015 — Event sort, clamp, overflow queue (FR-025, FR-026) — SC-009

**Files:** `tests/unit/midi_event_test.cpp` (owner), `src/processor/processor.cpp`.

**Test first.** `TEST_CASE("Profundum_MidiEventTranslation", "[profundum][processor][midi]")`, at 48 kHz:
- `SECTION("PartitionInvariance")`: a 2 s script (96 000 samples) — NoteOn(28) @311, NoteOn(33) @4097, NoteOff(28) @20001, NoteOn(40)
  @47999, NoteOff(33) @70003; no parameter change; no offset a multiple of 32. `renderScript` at host blocks `{1, 7, 32, 65, 512, 4096}` (the
  fixture prepared with `maxBlock = 4096`). Precondition: the 512 render's `rmsDb(·, 24000, 96000) >= -40` (lengthen the script if not).
  Every partition vs 512: `mismatches == 0` per channel.
- `SECTION("EventOrder")`: one 512 block listing NoteOn(40) @300 **before** NoteOff(40) @100, then 0.5 s of event-free blocks, compared with
  the same events listed in offset order: `mismatches == 0` per channel. Precondition: the reference's peak over its last 0.25 s is `> 1e-4`.
  **Fails** against T014's list-order copy (the NoteOff is dispatched at 300 and releases the note).
- `SECTION("Overflow")` (spec SC-009, plan §4.3):
  - Script: block A carries points voice mode 1.0 and polyphony 1.0 and the eight `kChord8` NoteOns @0; then 1 s of event-free blocks; then
    block B (512 samples, 1100 events): list indices 0–1023 are `kNoteOffEvent` fillers on pitch `100 + (i % 28)` at offset 0, and indices
    1024–1099 are 76 `kNoteOffEvent`s on `kChord8[k % 8]` at offset `450 − 5k` (`k = 0…75`); then 1.5 s of event-free blocks.
  - Right after block B: `lastSliceCountForTest() == 2` (the slices `[0, 450)` and `[450, 512)`).
  - (a) Every sample over `[B_end + 48000, B_end + 72000)` has `|x| < 1e-6` on both channels.
  - (b) Control: the same script without indices 1024–1099 gives `rmsDb >= -60` over that window on each channel.
  - (c) Block B plus the next 4 800 samples, compared with a `DirectEngine` replay (`setVoiceMode(Poly)` and `setPolyphony(8)` before block A;
    the 1024 fillers at block B's sample 0, then the 76 at sample 450, each group in list order): `mismatches == 0` per channel.
  - `WARN` `droppedEvents()`.
  - **Fails** against T014, which stops at 1024: the chord keeps sounding and (a) is red.
- `SECTION("OffsetClamp")`: POLY, polyphony 8; one 512 block with NoteOn(28) @−5 and NoteOn(40) @612; then 0.5 s of event-free blocks.
  `guardsIntact()` after every block; every sample passes `isFiniteBits`; right after the block, `lastSliceCountForTest() == 2`; the whole
  render has `mismatches == 0` against the same script with offsets 0 and 511.
- `SECTION("Translation")`: velocity `0.003f` → `rmsDb(·, 24000, 48000) >= -40`. A held E1 released by a `kNoteOnEvent` with velocity 0 at
  0.5 s → every sample over `[72000, 96000)` has `|x| < 1e-6`. Pitches −1, 128 and 300 (`kNoteOnEvent`) and one `kDataEvent` → each render has
  `mismatches == 0` against an event-free render of the same length.
- `SECTION("SliceCount")`: an event-free 4096 block → `lastSliceCountForTest() == 1`; a 512 block with NoteOn(28) @100, NoteOff(28) @200 and
  NoteOn(33) @300 → `== 4`.

**Implement** (plan §3.2):
- `buildEventOrder`: V-proc:1097–1124, a stable insertion sort of `EventSlot{clampOffset(off, total), int32(i)}` (strict `>` shift, so equal
  offsets keep list order), returning the sorted count `min(count, 1024)`.
- The overflow queue of V-proc:373–415: events `[1024, count)` are read from `data.inputEvents` in list order after the sorted queue, each at
  the effective offset `max(clampOffset(off), previous effective)`, seeded with the last sorted offset.
- One "next pending event" helper feeds both the dispatch `while` and `sliceEnd`. Explicit `static_cast`s; no allocation.

**Verify (gate).** `Profundum_MidiEventTranslation` passes and every earlier case stays green; the `WARN` lines are copied into `T015.txt`;
zero warnings.

---

# GROUP 12

## T016 — Parameter flow and audibility (FR-024.4, FR-027, FR-028, FR-041, FR-043) — SC-016

**Files:** `tests/integration/param_flow_test.cpp` (owner); `src/processor/processor.cpp` only if a section exposes a defect.

**Test first.** `TEST_CASE("Profundum_ParamFlowReachesEngine", "[profundum][params][audio]")`, 48 kHz, 512-sample blocks:
- `SECTION("ReadBack")`: after one block carrying points on all eight IDs (values 0.3 / 1.0 / 0.6 / 0.4 / 0.2 / 0.7 / 0.9 / 0.1 in ID order),
  the engine's `voiceMode()`, `polyphony()`, `outputCeilingDb()` and `macros()` equal the denormalised atomics exactly (`==`).
- `SECTION("Audibility")`: each render is 2 s with E1 held from 0 (the POLY rows use `kChord4` with voice mode 1.0), compared with the
  default render of the same notes:
  - gain normalized 0.9: `rms(x) / rms(default)` over `[2400, 96000)` lies in `[0.491, 0.511]`;
  - gain 0.0: every sample from index 960 (20 ms) on is `== 0.0f`;
  - gain 1.0: `mismatches == 0` against the default render;
  - voice mode, with 28 held from 0 and 40 held from 24000: Mono vs Poly → `maxAbsDiff > 1e-3`;
  - polyphony 1 vs 4 in Poly on `kChord4` → `std::abs(rmsDb1 − rmsDb4) > 1.0`;
  - ceiling normalized 0 on `kChord8` (Poly, polyphony 8): `peak <= std::pow(10.0f, -12.0f * 0.05f)`, and the default render's peak is above
    that value;
  - each macro at 0 and at 1, against 0.5 → `rmsDb(x − default) > -60`; **D-2 (user ruling 2026-10-22):** the Clean ↔ Violent
    0 arm is `WARN`-recorded, not gated (inert by construction on the default patch: mix / Fold / Character already at 0, the
    row's offsets clamp at 0; spec SC-016.2 row); its 1 arm is gated like every other arm.

  `WARN` every measured value.
- `SECTION("Tempo")`: before any context, `tempoBpm() == 120.0f`. A block with a stack `ProcessContext` (`state = kTempoValid`, `tempo = 90`)
  → `90.0f`. The next block with `state = 0`, `tempo = 140` → still `90.0f`. The next block with `processContext = nullptr` → `90.0f`.
- `SECTION("MacrosLive")`: after a block, `macros()` equals the atomics. A point `kMacroTightLooseId = 0.8` → after the next block,
  `macros().tightLoose == 0.8f`. With all four at 0.5, a 2 s E1 render has `mismatches == 0` against a `DirectEngine` given the same NoteOn.
- `SECTION("StateWhileRunning")`: a prepared fixture renders one block with E1 held; `setState` (Poly, polyphony 2, ceiling −6, gain 1.0,
  macros `{0.1, 0.2, 0.3, 0.4}`); one block with `inputParameterChanges = nullptr` and no events → `voiceMode() == Poly`, `polyphony() == 2`,
  `outputCeilingDb() == -6.0f`, and `macros()` exactly `{0.1f, 0.2f, 0.3f, 0.4f}`.
- `SECTION("GainRamp")`: a processor arm and a unity `DirectEngine` arm, E1 @0. Block 0 is bit-identical (`mismatches == 0`). Block 1
  carries `kOutputGainId = 0.9` @0 (the `DirectEngine` stays at unity). After block 1:
  - `outputGainValueForTest()` lies strictly inside `(0.5011872f, 1.0f)`;
  - a local `OnePoleSmoother s{1.0f}; s.configure(kOutputGainSmoothMs, 48000.0f); s.setTarget(0.5011872f);` advanced 512 times predicts the
    value: `std::abs(outputGainValueForTest() − s.getCurrentValue()) <= 1e-5`;
  - over block 1, at every index where `|direct| > 1e-4`, the ratio `r[i] = out[i] / direct[i]` satisfies `r[i] <= r[i−1] + 1e-6`, and
    `r[first] − r[last] > 0.1`.

These sections prove T013 / T014 behaviour, and each carries its own non-vacuity arm: the default-vs-setting comparisons, `GainRamp`'s ratio
clause (a block-step gain fails it), and `StateWhileRunning` (a change-triggered push fails it). A red section is fixed at its root cause in
`processor.cpp`; a floor is never moved.

**Verify (gate).** All six sections pass; the `WARN` lines are copied into `T016.txt`; zero warnings.

---

# GROUP 13

## T017 — Wrapper transparency and the hostile-gain ceiling (FR-024, FR-028, FR-045) — SC-008, SC-011.5 render arm

**Files:** `tests/integration/processor_audio_test.cpp`, `tests/unit/state_roundtrip_test.cpp`; `src/processor/processor.cpp` only for a
defect.

**Test first.**
- Append `SECTION("WrapperTransparency")` to `Profundum_ProcessorRendersHeldNote` (SC-008, plan §4.3). The script is 3 s (144 000 samples):
  NoteOn(28) @1000, NoteOn(33) @9001, NoteOff(28) @20003, NoteOff(33) @40017; at block 100 (sample 51 200) a point `kVoiceModeId = 1.0` @0;
  the `kChord4` NoteOns @60011, 60013, 61007 and 70001, all released @120019.
  - Arm (a): `ProcessorFixture` at block 512, defaults.
  - Arm (b): `DirectEngine` with the same note events at the same samples, and `setVoiceMode(Poly)` immediately before rendering sample
    51 200, with **no other setter**.
  - `mismatches == 0` on both channels. Precondition: arm (a) has `rmsDb(·, 0, 144000) >= -40`.
- Append `SECTION("HostileGainKeepsCeiling")` to `Profundum_StateRoundTrip`: load a stream with `outputGain` 4.0 (stored as 1.0), voice mode
  1, polyphony 8, ceiling −1 and macros 1.0; prepare at 48 kHz; render `kChord8` for 2 s → `peak <= std::pow(10.0f, -1.0f * 0.05f)` on both
  channels, and `globalParamsForTest().outputGain == 1.0f`.

Both are proofs of earlier tasks; the precondition gives each a non-vacuous signal. A red is fixed in `processor.cpp` at its root cause (for
example, a push that is not change-only), never by a tolerance.

**Verify (gate).** Both sections pass; zero warnings.

---

# GROUP 14

## T018 — Zero allocation (FR-029, FR-031) — SC-007, SC-017.7

**Files:** `tests/unit/lifecycle_test.cpp`; `src/processor/processor.cpp` only for a defect.

**Test first.** Append to `Profundum_ProcessorLifecycle`, under the P-11 discipline: every container is built **before** the scope; inside
the scope there are only `proc->…` calls and no Catch macro; the count is read inside with
`TestHelpers::AllocationDetector::instance().getAllocationCount()` and asserted after the scope.
- `SECTION("ActivateNoAlloc")`: a prepared fixture, `setActive(false)`; a scope around `setActive(true)` → count `== 0`.
- `SECTION("ProcessNoAlloc")`:
  1. Liveness probe first: `{ TestHelpers::AllocationScope s; auto* p = new int(7); n =
     TestHelpers::AllocationDetector::instance().getAllocationCount(); delete p; } REQUIRE(n >= 1);`.
  2. Prepare and render one warm-up block.
  3. Build 188 blocks of 512 (2 s) carrying NoteOns and NoteOffs (E1 and `kChord4`), points on voice mode, polyphony, ceiling, gain and all
     four macros, and a `ProcessContext` with `kTempoValid` at 90, then 140.
  4. Inside one scope, call `proc->process(data)` for all 188 blocks → count `== 0`.

This proves the T013 / T014 code. A non-zero count is a defect, fixed at its source in `processor.cpp`.

**Verify (gate).** Both sections pass; zero warnings.

---

# GROUP 15

## T019 — Editor, placeholder surface, preset manager (FR-050 – FR-055) — SC-013.1 – .4

**Files:** `tests/unit/controller/editor_lifecycle_test.cpp` (owner), `resources/editor.uidesc`, `src/controller/controller.cpp`.

**Test first.** `TEST_CASE("Profundum_EditorLifecycle", "[profundum][controller][ui][lifecycle]")`:
- `SECTION("PresetConfigIsLive")`, modelled on `plugins/vorago/tests/unit/controller/editor_lifecycle_test.cpp:115–170`:
  `makeProfundumPresetConfig()` → `pluginName == "Profundum"`, `pluginCategoryDesc == "Synth"`, `subcategoryNames ==
  std::vector<std::string>{"Sub"}`. A `PresetManager` pointed at a temporary factory directory `profundum_sc013_factory_<n>/Sub/Probe.vstpreset`,
  with a separate empty user directory `profundum_sc013_user_<n>`, scans exactly 1 preset with subcategory `"Sub"`.
  `fs::is_directory(PROFUNDUM_RESOURCES_DIR "/presets/Sub")` holds and its filename `== subcategoryNames[0]`. The temporary directories are
  removed at the end.
- `SECTION("HarnessCycles")`: a heap `Controller`, `initialize(nullptr)`;
  `Krate::TestSupport::exerciseEditorLifecycle(*controller, "editor", std::string(PROFUNDUM_RESOURCES_DIR) + "/editor.uidesc", 3)`; then
  `REQUIRE(controller->presetManagerForTest() != nullptr)`; `terminate()`.
- `SECTION("EditorBindsSurface")`: attach a `VST3Editor` from `createView(ViewType::kEditor)` with `nativePlatformType()` and walk the frame.
  The set of `CControl` tags `>= 0` is exactly `{0, 1, 2, 3, 100, 101, 102, 103}` (count 8), and tags 1 and 2 are `COptionMenu`s. Detach and
  release.
- `SECTION("CreateViewNames")`: `createView("nonsense") == nullptr`; `createView(ViewType::kEditor)` is a `VSTGUI::VST3Editor`.

`HarnessCycles`, `EditorBindsSurface` and `CreateViewNames` fail against T005 / T010 (no controls, `createView` returns `nullptr`, no preset
manager). `PresetConfigIsLive` is green from T003; it is the regression guard for FR-050 / FR-051.

**Implement.**
- `editor.uidesc` (plan §2.10): `<control-tags>` `OutputGain`=0, `VoiceMode`=1, `Polyphony`=2, `OutputCeiling`=3, `MacroWeight`=100,
  `MacroDarkBright`=101, `MacroTightLoose`=102, `MacroCleanViolent`=103; six `CSlider`s (gain, ceiling and the four macros) and two
  `COptionMenu`s (voice mode and polyphony), each beside an untagged `CTextLabel`; stock views only.
- `controller.cpp`:
  - `initialize` adds `presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(makeProfundumPresetConfig(), nullptr, this)`
    (`preset_manager.h:55`; V-ctl:113);
  - `terminate` sets `presetManager_ = nullptr` before the base;
  - `createView`: `FIDStringsEqual(name, ViewType::kEditor) ? new VSTGUI::VST3Editor(this, "editor", "editor.uidesc") : nullptr`
    (V-ctl:271–275);
  - the update config is compiled through a `static_assert` touch point, as at V-ctl:61–65. No `UpdateChecker` (FR-052).

**Verify (gate).** `Profundum_EditorLifecycle` passes; `profundum_tests.exe "[lifecycle]" --list-tests` lists it (SC-013.4; record in
`T019.txt`); zero warnings. The ASan run (SC-013.5) is T029's.

---

# GROUP 16

## T020 — `Profundum_ProcessorCpu` (FR-067) — SC-015 (written here, run only by T031)

**Files:** `tests/integration/processor_cpu_test.cpp` (owner).

`TEST_CASE("Profundum_ProcessorCpu", "[profundum][.perf][performance]")`, plan §4.4. Constants `kFs = 48000`, `kBlock = 512`,
`kWarmupBlocks = 200`, `kTrials = 16`, `kBlocksPerTrial = 100`, timed with `std::chrono::steady_clock`; all arms run in one interleaved trial
loop:
- **P-MONO / P-POLY:** `ProcessorFixture`, bare `proc->process(data)` calls. The first warm-up block carries NoteOn(28) (MONO), or `kChord4`
  plus a `kVoiceModeId = 1` point (POLY). Every timed block has `inputEvents = nullptr` and `inputParameterChanges = nullptr`, with buffers
  pre-built.
- **D-MONO / D-POLY:** `DirectEngine` (`processBlock(L, R, 512)` plus the gain loop), the same notes, `setVoiceMode(Poly)` for POLY.
- **P-MAX-MONO / P-MAX-POLY** (P-1, **gated**, user ruling Q-A 2026-10-22): as P-MONO / P-POLY, with all four macro points at 1.0 in
  the warm-up block.
- `best_X` = the minimum over trials of ns per 100 blocks; `pct_X = best_X / (100 × 512 / 48000 × 1e9) × 100`.
- `REQUIRE(best_P_MONO <= 1.05 * best_D_MONO)`, `REQUIRE(best_P_POLY <= 1.05 * best_D_POLY)`, `REQUIRE(pct_P_MONO <= 8.0)`,
  `REQUIRE(pct_P_POLY <= 25.0)`, `REQUIRE(pct_P_MAX_MONO <= 8.0)`, `REQUIRE(pct_P_MAX_POLY <= 25.0)` (Q-A).
- `WARN` every percentage, both ratios, both P-MAX arms, and the Phase 9 references 7.5798 and 24.005 labelled *"showcase setting, not comparable
  (P-1)"*.

The TU stays **out** of the `-fno-fast-math` block (T007). The case is hidden: the gate never runs it, and the executor does not run it.

**Verify (gate).** It compiles with zero warnings, and `profundum_tests.exe "[.perf]" --list-tests` lists `Profundum_ProcessorCpu` (record in
`T020.txt`; SC-002.1). The timing run is T031's.

---

# GROUP 17

## T021 — `.github/workflows/ci.yml` (FR-071)

**File:** `.github/workflows/ci.yml` (edit only). Re-open each site before editing it, and model every edit on its Vorago twin.

1. Sites the roster lint checks:
   - the detect-changes output `profundum: ${{ steps.set-outputs.outputs.profundum }}` after `:68`;
   - the paths-filter `profundum:` / `- 'plugins/profundum/**'` after `:100–101`;
   - `for p in … vorago profundum` (`:124`) and the `$GITHUB_OUTPUT` echo after `:142`;
   - `'plugins/profundum/CMakeLists.txt'` in the three `hashFiles` keys (`:266, :533, :1007`);
   - the nine `for plugin_info in \` blocks (`:304, :356, :411, :575, :636, :691, :1043, :1095, :1150`), each gaining the Profundum entry in
     the same shape (the macOS build entry is `"profundum:Profundum:Profundum_AU:Profundum_AUV3 profundum_tests"`), with the last list
     element keeping the `; do` terminator; and their `case` arms (`:314, :366, :421, :585, :646, :701, :1053, :1105, :1160`), each gaining
     `profundum) CHANGED="${{ needs.detect-changes.outputs.profundum }}" ;;`.
2. Sites the lint does not check:
   - `for area in … vorago profundum` (`:116`);
   - the Windows artifact upload (twin of `:495–500`: artifact `Profundum-Windows-x64`, path `Profundum.vst3`);
   - the macOS `Run Profundum AU Validation` step (twin of `:796–805`, with `auval -v aumu Prfd KrAt`);
   - the AUv3 bundle verification (twin of `:864–870`, `Profundum AUv3.app`);
   - the macOS artifact upload (twin of `:954–962`: `Profundum-macOS`, `.vst3`, `.component`, `Profundum AUv3.app`);
   - the Linux artifact upload (twin of `:1239–1244`: `Profundum-Linux-x64`).

**Verify (executor, seconds).** Write `specs/profundum-phase10-plugin-scaffold/evidence/close/ci-twins.js`. For every non-comment line of
`ci.yml` that contains `vorago` (case-insensitive), it builds the twin by `vorago→profundum`, `Vorago→Profundum` and `Vrgo→Prfd`,
normalises both sides by trimming whitespace and a trailing `\` or `; do`, and requires either that the line itself contains `profundum` or
that a normalised twin line exists. It prints `CI-TWINS missing=<n>` and exits 1 when `n > 0`. Run it: `missing=0`. Also,
`grep -c 'auval -v aumu Prfd KrAt' .github/workflows/ci.yml` ≥ 1. Record both in `T021.txt`. (`lint-plugin-roster.js` stays red until T024;
see deviation 2.)

---

# GROUP 18

## T022 — `release.yml` and `valgrind-nightly.yml` (FR-072, FR-073)

**Files:** `.github/workflows/release.yml`, `.github/workflows/valgrind-nightly.yml`.
- `release.yml`: `          - profundum` after `- vorago` (`:42`); `'plugins/profundum/CMakeLists.txt'` in the `hashFiles` key (`:143`).
- `valgrind-nightly.yml`: `profundum_tests` appended to the build list (`:284`) and to the run-loop list (`:291`). The run filter
  `'[lifecycle]'` (`:301`) is unchanged; it selects `Profundum_EditorLifecycle`.

**Verify (executor).** `grep -n profundum` on both files shows the four edits; record in `T022.txt`.

---

# GROUP 19

## T023 — Both clang-tidy scripts (FR-074, FR-075)

**Files:** `tools/run-clang-tidy.ps1`, `tools/run-clang-tidy.sh`.
- `.ps1`: `"profundum"` in the `ValidateSet` (`:66`); a `"profundum" { … }` case after the `"vorago"` case (model `:218–225`) that adds
  `plugins/profundum/src` and `plugins/profundum/tests` to `$SourceDirs` and `$IncludeDirs`; the same four lines in the `all` block
  (`:240–251`).
- `.sh`: `profundum` in the usage text (`:67`); a `profundum)` case with `SOURCE_DIRS=("plugins/profundum/src" "plugins/profundum/tests")`
  (model `:160–161`); the two paths in the `all)` block (`:177–178`).

**Verify (executor).** `awk '/"all" *\{/,/^ *\}/' tools/run-clang-tidy.ps1 | grep -c 'plugins/profundum/\(src\|tests\)'` ≥ 2;
`awk '/^ *all\)/,/;;/' tools/run-clang-tidy.sh | grep -c 'plugins/profundum/\(src\|tests\)'` ≥ 2; `bash -n tools/run-clang-tidy.sh` exits 0.
Record in `T023.txt`.

---

# GROUP 20

## T024 — Tool lists (FR-076, FR-077, FR-080); the roster lint goes green

**Files:** `tools/check-changelog-coverage.js`, `tools/gen-specs-index.js`, `tools/run-cpu-tests.js`, `tools/ci-test-shard.js`,
`tools/run-close-lanes.js`, `.claude/workflows/release-readiness.js` (Q-B, user ruling 2026-10-22).
- `check-changelog-coverage.js:50`: append `'profundum'` to `PLUGINS`.
- `gen-specs-index.js`: `['profundum', 'Profundum'],` **before** the DSP keywords (after the `vorago` entry, `:19–27`), with a comment in the
  style of `:20–26` noting that Profundum slugs contain `harmonic`, `stereo-field` and `voice-engine`.
- `run-cpu-tests.js:45–51`: append `'profundum_tests'` to `DEFAULT_TARGETS`.
- `ci-test-shard.js:171–174`: append `['profundum', 'Profundum']` to the Windows preset-install list.
- `run-close-lanes.js:37–38`: add `'profundum_tests'` to the suite-lane list.
- `.claude/workflows/release-readiness.js:13–22`: add `profundum: { testTarget: 'profundum_tests', bundle: 'Profundum.vst3' },`
  after the `vorago` entry of `PLUGIN_MAP` (FR-080; Q-B ruled 2026-10-22).
- **Not edited:** `tools/ci-test-times.json` (P-7).

**Verify (executor, seconds).** `node --check` on all five files; `node tools/lint-plugin-roster.js` exits 0 (SC-018.1);
`node tools/check-changelog-coverage.js profundum` exits 0 (FR-010). Record in `T024.txt`.

---

# GROUP 21

## T025 — Root `CLAUDE.md` rosters (FR-079)

**File:** root `CLAUDE.md`. Add Profundum to:
- the Project Overview bullet list: `- **Profundum**: Mass bass instrument at `plugins/profundum/` (AU type: `aumu`, subtype `Prfd`)`;
- the Monorepo Structure sentence "Plugins today: …" (add `profundum`);
- the per-directory leaf list (`[`profundum`](plugins/profundum/CLAUDE.md)`);
- the pluginval block (`# Profundum` and the `build/windows-x64-release/VST3/Release/Profundum.vst3` line);
- the plugin test-target list (`--target profundum_tests  # Profundum`);
- the "Built plugins are at" list (`Profundum.vst3/`);
- the clang-tidy `-Target <…|vorago|profundum>` roster;
- the Quick Reference rows `Add Profundum parameter` (plugin_ids.h → parameters/ → processor → controller → uidesc), `Add Profundum test`
  (`plugins/profundum/tests/unit/` or `tests/integration/`) and `Change Profundum UI` (`plugins/profundum/resources/editor.uidesc`).

Match the existing style; touch nothing else.

**Verify (executor).** The root-`CLAUDE.md` rows of the SC-018.2 table (T028) each meet their expected count, and
`tr '\n' ' ' < CLAUDE.md | grep -c 'Plugins today:.*profundum'` ≥ 1. Record in `T025.txt`.

---

# GROUP 22 — Integration (last group)

## T026 — CMake registration audit (single task)

No new edits unless the audit finds a gap. This is the only task after T007 allowed to edit a CMake list.
- `plugins/profundum/CMakeLists.txt` names every `src/` file of the FR-008 skeleton exactly once (`grep -c` per path = 1; the generated
  `version.h` is listed once). It contains no `/wd`, `-Wno-` or `pragma warning`, and it calls each of `krate_plugin_read_version`,
  `krate_plugin_configure_generated_files`, `smtg_add_vst3plugin`, `smtg_target_configure_version_file`, `krate_plugin_platform_setup`,
  `smtg_target_add_plugin_resources`, `krate_plugin_install_to_system`, `krate_plugin_install_presets` and `krate_plugin_set_warnings`
  exactly once.
- `plugins/profundum/tests/CMakeLists.txt` names each of the ten test TUs exactly once in `add_executable` and `processor.cpp` /
  `controller.cpp` once each; names the six `-fno-fast-math` TUs exactly once in the flag block and `processor_cpu_test.cpp` in no flag
  block; has `sdk` after `vstgui_support` in the link list, `catch_discover_tests(profundum_tests REPORTER console)`, and `krate_test_pch`
  last.
- Root: `grep -c 'add_subdirectory(plugins/profundum)' CMakeLists.txt` = 1; `git diff 285613c5 -- CMakeLists.txt` shows one added line.
- Record every count in `T026.txt`.

## T027 — Regenerated artifacts, generator checks, every pre-commit lint (FR-078) — SC-018.1, SC-018.3, SC-022

**Files:** `specs/_architecture_/repo-map.json`, `specs/INDEX.md` (regenerated, never hand-edited).
1. `node tools/gen-repo-map.js` and `node tools/gen-specs-index.js` (writing). `gen-symbols.js` scans `dsp/include` only and is not re-run
   for writing.
2. Each `GENERATORS` entry (`tools/hooks/guard-ci-gates.js:31–35`) with `--check` → exit 0; each of the nine `LINTS` (`:38–48`) as
   `node tools/<script>` → exit 0.
3. `grep -n '^## Profundum' specs/INDEX.md` → 1 line; `awk '/^## Profundum/,/^## [^P]/' specs/INDEX.md | grep -c
   profundum-phase10-plugin-scaffold` ≥ 1.

Record every command with its exit code in `T027.txt`. A red lint is fixed at its source in a Phase 10 file. A red outside the Phase 10
change set is a blocked report (FR-090).

## T028 — Registration greps, FR inspection checklist, scope fence — SC-018.2, SC-019; FR-002, -006, -008, -009, -014, -018, -053, -056

**Files:** `specs/profundum-phase10-plugin-scaffold/evidence/close/registration-greps.sh`, `evidence/close/scope-fence.js`, run-log
`T028.txt`.
- `registration-greps.sh` prints `<pattern> <count> <expected> PASS|FAIL` for:
  - every row of spec SC-018.2's table and of plan §5.5's "SC-018 extra greps" table;
  - every row of plan §5.5's "FR inspection checklist": FR-002 `6 false`; FR-006 0 lines; FR-008 every non-generated skeleton path present
    (a `node -e` existence check); FR-009 the nine anchors ≥ 1; FR-014 one line; FR-018 `2` and 0 lines; FR-053 `1`; FR-056
    `ls -A plugins/profundum/src/ui` = `.gitkeep`.

  Its last line is `REGISTRATION GREEN|RED`.
- `scope-fence.js` (SC-019): the union of `git diff --name-only 285613c5` and `git status --porcelain --untracked-files=all` must match the
  allow-list: `plugins/profundum/**`, `specs/profundum-phase10-plugin-scaffold/**`, root `CMakeLists.txt`, `.gitignore`,
  `.github/workflows/{ci,release,valgrind-nightly}.yml`, `tools/run-clang-tidy.{ps1,sh}`, `tools/check-changelog-coverage.js`,
  `tools/gen-specs-index.js`, `tools/run-cpu-tests.js`, `tools/ci-test-shard.js`, `tools/run-close-lanes.js`,
  `.claude/workflows/release-readiness.js` (Q-B), `specs/INDEX.md`, `specs/_architecture_/repo-map.json` and root `CLAUDE.md`. `.serena/project.yml` is excluded by name. The script prints each offending path
  and `SCOPE GREEN|RED`. Any `dsp/`, `tests/test_helpers/` or `plugins/<other>/` path is RED (FR-090: stop and report).
- **Executor:** write both, run both once (seconds) and quote their outputs in `T028.txt`. A FAIL row in a Phase 10 file is fixed here.

## T029 — Suites lane, pluginval, bundle, clean tree, clang-tidy, ASan — **deferred to the main loop** (SC-001 Windows, SC-002, SC-003, SC-013.5, SC-018.4, SC-020, SC-021)

**Files:** `specs/profundum-phase10-plugin-scaffold/evidence/close/suites.sh`, `evidence/close/tidy.sh`, `evidence/close/asan.sh`.

**Lane scope (encoded 2026-10-18, Phase 6 ruling G9-4).** The phase's only executable is `profundum_tests`, and every one of its TUs is a
Phase 10 TU. No shared header is modified (FR-090), so there is **no consumer suite**.

`suites.sh` runs detached and writes `evidence/run-log/close/status.txt`, one line per lane (name, exit code, wall clock):
1. **Change-set scope.** `git diff --name-only 285613c5 -- dsp/include dsp/src plugins/shared tests/test_helpers` must print nothing. A
   path here is RED (FR-090). It is written to the status file together with the consumer executables it would require. The lane does
   **not** run those consumers; the stage stops for a user ruling.
2. One build of `Profundum profundum_tests` with `-- /nr:false` to `evidence/run-log/close/build.log`. The warning count
   (`warning C[0-9]|warning:`, no path filter) must be 0 (SC-001). The wall clock is recorded.
3. Concurrently:
   - (a) the per-push lane, `profundum_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]" -D 15` →
     `perpush-profundum_tests.log`, whose last line reads `All tests passed` (SC-002.2); its wall clock is recorded for a later
     `ci-test-times.json` refresh (P-7);
   - (b) the `[long]` lane of the phase's own TUs:
     `profundum_tests.exe --list-tests "[profundum][long]~[performance]~[perf]~[benchmark]~[!benchmark]"`. Zero cases are expected (no
     Phase 10 case is `[long]`). If any appear, they run one shard per case through `xargs -P <min(shards, cores − 4)>`. The lane never runs
     every `[long]` case of another executable;
   - (c) `--list-tests` (the eight non-hidden FR-066 names are present) and `"[.perf]" --list-tests` (`Profundum_ProcessorCpu`) (SC-002.1);
   - (d) `tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Profundum.vst3` → exit 0 (SC-003);
   - (e) `node tools/check-bundle.js build/windows-x64-release/VST3/Release/Profundum.vst3` → the lines `OK   Profundum.vst3: editor.uidesc +
     moduleinfo.json present` and `check-bundle: all 1 bundle(s) OK` (SC-020);
   - (f) after the build, `git status --porcelain plugins/profundum` prints no generated file (SC-021).
4. A summary block with every lane's verdict and wall clock.

`tidy.sh`: reconfigure `build/windows-ninja` inside vcvars64 (the test CMake changed, and a stale compile database gives phantom warnings);
confirm the 14 Profundum TUs are in `compile_commands.json`; then run `./tools/run-clang-tidy.ps1 -Target profundum -BuildDir
build/windows-ninja` to `evidence/run-log/close/tidy.log` under a 900 s wall-clock ceiling. Last line `TIDY GREEN|RED`; green means zero
warnings (SC-018.4). Not `-Target all`.

`asan.sh`: `"$CMAKE" --build build-asan --config Debug --target profundum_tests` (it reconfigures on the root change), then
`build-asan/bin/Debug/profundum_tests.exe "[lifecycle]"` → exit 0 with no `AddressSanitizer` line (SC-013.5), to
`evidence/run-log/close/asan.log`.

**Executor:** write the three scripts, `bash -n` each, and return done with notes "deferred to the main loop at close: run `suites.sh`
detached, and `tidy.sh` and `asan.sh` concurrently; quote each lane's last lines into `evidence/run-log/T029.txt`; any Phase 10 red is owned
(fix, re-run only the affected case or lane)". A timing-budget assertion of an untouched case that fails under the concurrent lanes is
recorded as load, not re-run. None is expected, because the per-push lane holds only Phase 10 cases.

## T030 — Portability (cross-cutting; SC-001)

**Files:** `specs/profundum-phase10-plugin-scaffold/evidence/close/portability.sh`, run-log `T030.txt`.
- `portability.sh`:
  1. `node tools/check-portability.js` with the **14** new `.cpp` files passed explicitly by path, all under `plugins/profundum/` (the plan
     §5.5 list): `src/entry.cpp`, `src/processor/processor.cpp`, `src/controller/controller.cpp`, `tests/vstgui_test_stubs.cpp`,
     `tests/unit/test_main.cpp`, the five `tests/unit/*_test.cpp`, `tests/unit/controller/editor_lifecycle_test.cpp` and the three
     `tests/integration/*_test.cpp`. 300 s ceiling. The required line is `check-portability: all clear -- 14 compiled.` with **no**
     `, M skipped` suffix. Cross-check: `git status --porcelain --untracked-files=all plugins/profundum | grep -c '\.cpp$'` prints `14`.
  2. `node tools/lint-apple-globals.js` over the plugin's `src/` headers and the TUs (deviation 6; re-open its usage line first).
  3. `wsl --shutdown`.

  Output goes to `evidence/run-log/close/portability.log`; the last line is `PORTABILITY GREEN|RED`.
- **Executor:** write the script and run it once. Green → quote the log in `T030.txt`, done. A finding in a Phase 10 file is fixed here
  (the group gate rebuilds). If the run fails with `wsl: Failed to translate` (the sandbox does not mount the drive) or exceeds 5 minutes,
  stop it and return done with notes "deferred to the main loop at close: run `bash
  specs/profundum-phase10-plugin-scaffold/evidence/close/portability.sh` unsandboxed".

## T031 — CPU lane, alone (SC-015) — **deferred to the main loop**

- Precondition: the T029 and T030 lanes have finished and the machine is idle.
- **Executor:** return done with notes "deferred to the main loop at close: run `node tools/run-cpu-tests.js profundum_tests` last and alone;
  quote every `WARN` percentage, both ratios and both P-MAX arms into `evidence/run-log/T031.txt`".
- A red reading is re-run **once**, after the machine has idled (root `CLAUDE.md`, timing-sensitive tests). Still red → a defect reported to
  the user. No budget is relaxed and no workload is shrunk. The P-MAX arms are gated (Q-A ruled 2026-10-22).

---

## Close procedure (main loop, in this order)

1. G3 gate: the P-5 probe (`IdleIsExactZero`). A red goes to the user before anything else runs.
2. The G4 – G21 gates, as the workflow runs them.
3. T029: `suites.sh` detached, with `tidy.sh` and `asan.sh` concurrently; `portability.sh` too if T030 deferred it.
4. Any red → triage into "Defects found", fix, and re-run only the affected case or lane.
5. T031: the CPU lane, alone, after the machine has idled.
6. Comply (outside this task list) quotes the run-log files. SC-004 (`auval`) and the macOS / Linux legs of SC-001 are marked
   **PENDING-CI** until the user pushes and a run ID exists, never PASS.

## Open questions carried from the plan (built at the plan's reading; surface, do not decide)

- **Q-A (P-1) — RULED 2026-10-22:** the P-MAX arms are gated at ≤ 8 % / ≤ 25 % (T020, T031).
- **Q-B (P-6) — RULED 2026-10-22:** `profundum` is added to the `PLUGIN_MAP` of `.claude/workflows/release-readiness.js` now; the
  file is in the SC-019 allow-list (T024, T028).

## Defects found

(Empty at task-cut time. Gates, fixers and the main loop record entries here: task, case / section, verbatim reading, and triage — which
task owns it, or the user ruling. A red triaged here for a later group is not a fixer's to attack.)

- **D-1 — G3 gate, T006 P-5 probe, `Profundum_ProcessorLifecycle` / `IdleIsExactZero` clause 2 — RED, AWAITING USER RULING.**
  Verbatim reading (lifecycle_test.cpp): `(65) P-5 idle: non-zero samples L = 0, R = 0 of 4096`; `(91) P-5 held E1 RMS (L, 0 .. 0.5 s)
  = -14.3377 dBFS`; `(97) P-5 release: last non-zero sample L = 72527, R = 72527 (tail starts at 72000)`; `(98) FAILED: REQUIRE(
  nonZeroCount(d.outL, kTailFrom) == 0u ) with expansion: 528 (0x210) == 0`. Clause 1 (idle exact zero) is GREEN. Clause 2: after
  NoteOff at 24 000 the `DirectEngine` output is still non-zero for 48 528 samples (1.011 s at 48 kHz), 528 samples past the 1 s bar.
  The probe matches tasks.md T006 and plan §1.3 P-5 / §4.3 ("the same 1 s after a released note") as written, so the test is not wrong.
  Triage: per the T006 stop rule and FR-090 the cause is in `dsp/` (the `ProfundumEngine` default-config release tail: amp-envelope
  release plus the voice's `isActive()` chain at `profundum_voice.h:359`, and the C-9 32-sample grid latch of the NoteOff at
  `profundum_engine.h:279-288`, not yet isolated). The fixer did NOT edit `dsp/` and did NOT move the bar. The stage stops here for a
  user ruling: (a) rule the tail bar (e.g. measure from the engine's reported release end), or (b) authorise a `dsp/` fix.
  **RULED 2026-10-22 (main loop, hidden probe `Profundum_IdleTailProbe` on the engine taps first):** the voice is exactly silent at
  28 799 (0.100 s after the note-off: the 100 ms default release), the field stage by 30 095; from there the output is the global
  5 Hz DC blocker's own exponential decay of the last voice sample (−0.0115; τ 31.8 ms): > 1e-3 until 30 244, > 1e-6 until 40 863,
  > 1e-12 until 61 907, exactly zero after 72 463 (its denormal guard) + the 64-sample look-ahead = 72 527. The 1 s clause rested on
  a wrong premise (that the output is exact zero once the voices are), not on a defect; no spec SC depends on it (SC-017.1 never
  sounds a voice, SC-017.4 goes through reset). The user chose the reading refinement over a `dsp/` blocker change: the clause now
  reads the Sum tap exactly zero by 0.125 s, \|out\| ≤ 1e-6 from 0.5 s and exact zero from 1.25 s after the note-off (T006 as
  rewritten above; plan P-5 / §4.3 / R-1; spec Clarifications D-1). Re-read on the rewritten clause: green (run-log/D-1.txt).
- **D-2 — G12 gate, T016, `Profundum_ParamFlowReachesEngine` / `Audibility`, macro row `kMacroCleanViolentId` = 0 — RED, AWAITING
  USER RULING.** Verbatim reading (param_flow_test.cpp:284-285): `macro 103 = 0: rmsDb(x - default) = -200 dBFS (bar > -60)`;
  `FAILED: REQUIRE( d > -60.0 ) with expansion: -200.0 > -60.0`. Every earlier row is green (e.g. `macro 101 = 1: -19.6822`,
  `102 = 0: -18.1675`, `102 = 1: -19.1082`); the `103 = 1` row is not reached (the REQUIRE aborts the section). Not a processor
  defect: `ReadBack` proves the value reaches the engine (`macros().cleanViolent == 0.1f`), and the push runs every block. Cause, read
  in `dsp/` (not edited): the default patch is already at the Clean end. `MorphState` defaults `excitation.mix = 0`
  (profundum_core.h:355), `foldAmount = 0` and `characterAmount = 0` (morph_state.h:105, :108); Clean ↔ Violent's rows add
  `amount · sign(u) · |u|` and clamp each field to `[0, …]` (profundum_macro_matrix.h:88-91, :220-223, addClamped :249-258), so at
  macro 0 mix / Fold / Character stay at 0 and only `excitation.drive` moves (0.5 → 0), which is inert at mix 0. Output is
  therefore bit-identical to the 0.5 render. This is the Phase 9 B10-1 situation ("on A the macro is inert by construction"): the
  macro's lower half has no travel on the default patch. The test matches SC-016.2 / T016 as written; the spec row's premise ("each of
  the four macros, 0 and 1, vs 0.5") is false on `ProfundumEngineConfig{}` + the default `MorphState`. The fixer did NOT edit `dsp/`
  (FR-090) and did NOT move the floor. Options for the user: (a) rule SC-016.2's Clean ↔ Violent row to the arm the default patch can
  move (macro 1 vs 0.5; the 0 arm recorded as inert by construction, as B10-1), (b) give the row a base patch with non-zero excitation
  **RULED 2026-10-22: (a).** The spec SC-016.2 row carries the recorded arm with the attribution; T016's Audibility section records the
  103 = 0 reading and gates 103 = 1; the shipped default sound and `dsp/` are untouched. Re-read: run-log/D-2.txt.
  mix / Fold / Character (e.g. a Violent-side default set through the processor, which changes the shipped default sound), or
  (c) authorise a `dsp/` change (macro-matrix law or `MorphState` defaults; Phase 9 surface).
- **D-3 — comply, FR-006 / CC-warnings / SC-001 — RULED 2026-10-22 (FR-090 exception).** The Profundum target build printed four
  warning lines (run-log/close/build-tidyfix.log): 2× `closed_form_adaa2.h(124,22): warning C4459: declaration of 'kTwoPi' hides
  global declaration` and 2× `profundum_engine.h(991,50): warning C4456: declaration of 'r' hides previous local declaration`
  (entry.cpp and processor.cpp; profundum_tests compiled with 0). The spec's "C4459 source is unreachable" claim was false. The user
  allowed two identifier renames in `dsp/`: `kTwoPi` → `kTwoPiD` in `diodeH1Coefficient` and `r` → `rc` in the a1-recipe block
  of `gridStep` — no expression changes, bit-identical. Gate: the per-push suites of the headers' consumers (dsp_processors_tests,
  dsp_systems_tests, dsp_effects_tests; run-log/close/consumer-*.log) plus profundum_tests, a zero-warning rebuild
  (run-log/close/build-d3.log), pluginval and check-portability on the final bits. The 32 clang-tidy warnings of SC-018 (all in
  plugins/profundum) were fixed in the same close pass (run-log/close/tidy.log TIDY GREEN).
