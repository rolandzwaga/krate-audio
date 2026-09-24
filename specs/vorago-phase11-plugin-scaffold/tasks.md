# Tasks: Vorago Phase 11 — Plugin Scaffold

**Spec:** `specs/vorago-phase11-plugin-scaffold/spec.md`
**Plan:** `specs/vorago-phase11-plugin-scaffold/plan.md`
**Roadmap:** `specs/Vorago-roadmap.md` → Part B (lines 519–522), Phase 11 (lines 524–542), Open
Questions 1 and 7 (lines 620–634).
**Deliverables:** a new plugin tree `plugins/vorago/` (FR-008 skeleton: 2 processor TUs, 2 controller
TUs, `entry.cpp`, 6 headers, resources, installers, leaf `CLAUDE.md`); a new test target `vorago_tests`
(nine `TEST_CASE`s in nine TUs + `test_main.cpp` + fixture + stubs); registration at **every** roster
outside `plugins/` (root CMake, `.gitignore`, `ci.yml`, `release.yml`, `valgrind-nightly.yml`, both
clang-tidy scripts, `check-changelog-coverage.js`, `gen-specs-index.js`, `run-cpu-tests.js`, root
`CLAUDE.md`, regenerated `repo-map.json` / `INDEX.md`).
**No new DSP.** No file under `dsp/`, `plugins/seraphis/` or `plugins/shared/` is modified (SC-027).

All paths are repo-relative to `f:/projects/iterum`.

---

## How to execute these tasks

**Canonical order inside every task, no exceptions:**

1. Write the failing test **first**, in the named file, inside the named `TEST_CASE`, with the exact
   assertions given. Build, run it, confirm it **fails** (a task that says "regression guard" instead
   states what makes it fail and requires the non-vacuity arm to be proven first).
2. Implement the minimum that makes it pass, exactly as the cited plan section pins it.
3. Fix **all** compiler warnings — zero is the gate. "Pre-existing" is not a category.
4. Run the named verification command and confirm green.

**Build and run (Windows, always the full CMake path):**

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target Vorago vorago_tests 2>&1 | tee <scratch>/build.log | tail -5
grep -cE "warning C[0-9]|warning:" <scratch>/build.log          # must print 0
build/windows-x64-release/bin/Release/vorago_tests.exe 2>&1 | tail -5                    # all non-hidden cases
build/windows-x64-release/bin/Release/vorago_tests.exe "Vorago_StateRoundTrip" 2>&1 | tail -5   # one case
build/windows-x64-release/bin/Release/vorago_tests.exe "Vorago_ProcessorLifecycle" -c "DegenerateShapes"   # one SECTION
```

Catch2 filters are **positional** (`<exe> "Vorago_*"`); tags go in brackets (`"[lifecycle]"`). Never
`ctest -R vorago_tests` — `catch_discover_tests` registers case names, not executables. The post-build
copy to `C:/Program Files/Common Files/VST3/` may fail with a permission error; compilation still
succeeded. Capture slow output to a log once — never re-run a slow suite to see its output again.

**Parallelism.** Tasks marked **[P]** inside one GROUP touch fully disjoint files and may be *authored*
concurrently. In G3 those files are brand new. In G5 and G14 each [P] task is the **sole owner** of a
file that T006/T005 created only as a placeholder (an empty scaffold case or an interface stub), which
no other task in the group touches — that is what makes them disjoint. Because every test TU links into
the **one** `vorago_tests` binary, the *build + run* verify steps of [P] tasks are executed **one at a
time**, never as concurrent builds of the same target. Every task that edits a file another task also
edits (`processor.cpp`, `controller.cpp`, a shared test TU, any existing repo file) is **alone in its
group**.

**Why the in-tree CMake comes early (G4) and a CMake task also comes last (G20).** A plugin cannot have
a failing test until `plugins/vorago/CMakeLists.txt`, `plugins/vorago/tests/CMakeLists.txt` and the
root `add_subdirectory` exist. So G4 writes all three **once, with the complete FR-008 source lists**
(every later file is created as a placeholder in G3 first), and no later task edits a CMake list. G20's
single CMake-registration task is the audit that re-checks those three sites against FR-008 and makes
the one conditional change the plan defers (`/wd4459` on `vorago_tests`, plan §5.2). This mirrors the
Phase 10 precedent (`specs/vorago-phase10-voice-engine/tasks.md` T006 wiring → T027 audit).

**No commit tasks.** Commits happen outside this workflow.

**Namespace hazard (every test TU).** `Krate::DSP::TestUtils::Vorago` exists
(`tests/test_helpers/vorago_fixtures.h:73`; `dsp/tests/unit/systems/vorago_perf_budget.h:61`). Every test
TU names plugin types **fully qualified** as `::Vorago::…` and **never** writes
`using namespace Krate::DSP::TestUtils;`. No test TU includes `vorago_fixtures.h` (spec edge case
"Namespace hazard"; `applyFastAttack` cannot reach a processor-owned engine and SC-005 forbids it).

**Non-finite checks (every test TU).** Use `Krate::DSP::detail::isFinite(float)`
(`dsp/include/krate/dsp/core/db_utils.h:118`, bit-pattern based). Never `std::isnan` / `std::isinf` /
`std::isfinite` (FR-062). Non-finite payloads are built from bit patterns through a `volatile
std::uint32_t` + `std::memcpy`.

**Stop-and-surface (binding on every task):** no threshold is relaxed, no render shortened below the
stated length, no partition removed from a list, no test tagged `[long]` to dodge the per-push lane
(SC-006 in particular is a bounded-output test and must never be `[long]`, plan P-7). When a
non-vacuity precondition misses its floor, the **script is lengthened** (longer render, velocity 127)
— never the threshold (plan P-6). A CPU figure over `kReferenceNs` is reported to the user as a Phase 10
budget finding, never absorbed (FR-067).

---

## Verified API surface these tasks rely on (read this session, `ddd3c476`)

| Symbol | Where | Signature / fact |
|---|---|---|
| `VoragoEngine` | `dsp/include/krate/dsp/systems/vorago_engine.h` | `kMaxVoices = 6` (:175), `kDefaultPolyphony = 4` (:178), `kMaxBlockSamples = 2048` (:182); `void prepare(double, const VoragoEngineConfig&) noexcept` (:277); `void reset() noexcept` (:427); `void silence() noexcept` (:447); `void setPolyphony(std::size_t) noexcept` (:507); `std::size_t getPolyphony() const noexcept` (:523); `void setSeed(std::uint32_t) noexcept` (:544); `void noteOn(std::uint8_t, std::uint8_t) noexcept` (:564); `void noteOff(std::uint8_t) noexcept` (:601); `void processStereoBlock(float*, float*, std::size_t) noexcept` (:891); `void processOutputStage(float*, float*, std::size_t) noexcept` (:1006); `std::size_t getLatencySamples() const noexcept` (:1031); `getActiveVoiceCount()` (:1040); `VoiceState getVoiceState(std::size_t) const noexcept` (:1065) |
| `VoiceState` | `dsp/include/krate/dsp/systems/voice_allocator.h:43` | `enum class VoiceState : uint8_t` (`Idle`, `Active`, `Releasing`) |
| `CavernVerb` | `dsp/include/krate/dsp/effects/cavern_verb.h` | `struct PrepareConfig` (:300); `void prepare(double, const PrepareConfig&) noexcept` (:358); `void reset() noexcept` (:487); `void processStereoBlock(const float*, const float*, float*, float*, std::size_t) noexcept` (:568); `setSize` (:613), `setDarkness` (:619), `setDecaySeconds` (:626), `setFog` (:652), `setDamperDepth` (:717), `setWidth` (:741), `setMix` (:748), `setSeed` (:758); `std::size_t getLatencySamples() const noexcept` (:786) |
| `VoragoMacroMatrix` | `dsp/include/krate/dsp/systems/vorago_macro_matrix.h` | `enum class VoragoMacro : std::uint8_t { Darkness = 0, Age, Density, Movement, Gravity, Entropy, Pressure, Weight, Fog, Life, Depth, Mass, Count }` (:95–109); `VoragoCavernTargets { size 0.50f, darkness 0.80f, decaySeconds 20.0f, fog 0.30f, damperDepth 0.35f, mix 1.00f, width 1.00f }` (:182–190); `VoragoMacroValues` defaults `0.0f` except `gravity = 0.5f` (:198–211); `kNumMacros` (:251); `void setMacro(VoragoMacro, float) noexcept` (:832); `void setMacros(const VoragoMacroValues&) noexcept` (:915); `void apply(VoragoEngine&) const noexcept` (:954); `VoragoCavernTargets computeCavernTargets() const noexcept` (:1026) |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | class (:134); `explicit OnePoleSmoother(float initialValue) noexcept` (:148); `void configure(float smoothTimeMs, float sampleRate) noexcept` (:160); `void setTarget(float) noexcept` (:170); `float getCurrentValue() const noexcept` (:191); `float process() noexcept` (:197); `void snapTo(float) noexcept` (:263) |
| `ScopedDenormalMode` | `dsp/include/krate/dsp/core/scoped_denormal_mode.h:60` | RAII class |
| `detail::isFinite` | `dsp/include/krate/dsp/core/db_utils.h:118` | `constexpr bool isFinite(float) noexcept` |
| `createDropdownParameterWithDefault` | `plugins/shared/src/ui/parameter_helpers.h:47–68` | `(const TChar* title, ParamID id, int32_t defaultIndex, std::initializer_list<const TChar*>)` → `StringListParameter*`; sets only the **current** value via `setNormalized` (:64–66), not `defaultNormalizedValue` (plan P-1) |
| `PresetManager` ctor | `plugins/shared/src/preset/preset_manager.h:55–61` | `explicit PresetManager(PresetManagerConfig, Steinberg::Vst::IComponent*, Steinberg::Vst::IEditController*, std::filesystem::path userDirOverride = {}, std::filesystem::path factoryDirOverride = {})` |
| `Krate::Test::EventList` | `tests/test_helpers/vst_event_list.h:36` | `addNoteOn(int16 pitch, float velocity, int32 sampleOffset = 0)` (:63), `addNoteOff(int16 pitch, int32 sampleOffset = 0)` (:77), `addEvent(Event&)` (:58), `clear()` (:88); **stores events in a `std::vector` — `addNoteOn` allocates; build lists outside any `AllocationScope`** |
| `Krate::Test::ParameterChanges` | `tests/test_helpers/vst_param_changes.h:78` | `addChange(ParamID, double)` (single point at offset 0), `setChange`, `clear()`; `ParamValueQueue(ParamID, double)` (:38) |
| `exerciseEditorLifecycle` | `tests/test_helpers/editor_lifecycle_harness.h:102` | `Krate::TestSupport::exerciseEditorLifecycle(Steinberg::Vst::EditController&, const char* templateName, const std::string& uidescAbsolutePath, int cycles = 3)` |
| `AllocationScope` | `tests/test_helpers/allocation_detector.h:111` | `TestHelpers::AllocationScope` (namespace `TestHelpers`, :20); `TestHelpers::AllocationDetector::instance().getAllocationCount()` (:70, :95) |
| `kReferenceNs` | `dsp/tests/unit/systems/vorago_perf_budget.h:82` | `Krate::DSP::TestUtils::Vorago::kReferenceNs` (namespace opened at :61) |
| Seraphis fixture model | `plugins/seraphis/tests/seraphis_test_fixture.h:158` | `struct ProcessorFixture` with `kGuardWords = 8`, `kGuardValue = -8.5e17f` |
| Seraphis test target model | `plugins/seraphis/tests/CMakeLists.txt` | SDK sources (:81–90), link order `… vstgui_support sdk` (:93–101), include dirs (:103–111), `-fno-fast-math` block, `catch_discover_tests(seraphis_tests REPORTER console)` |
| Seraphis test main | `plugins/seraphis/tests/unit/test_main.cpp` | `#include <allocation_operator_overrides.h>` (only TU), `void* moduleHandle = nullptr;`, `enableFTZDAZ();` before `Catch::Session().run` |
| Seraphis uidesc attribute sets | `plugins/seraphis/resources/editor.uidesc:189–200` | `CSlider` `orientation="horizontal" draw-frame="true" draw-back="true" draw-value="true" frame-color back-color value-color`; `COptionMenu` `font font-color frame-color back-color fill-color frame-width="1" transparent="false" style-3D-in="false" style-3D-out="false"` |
| Root CMake plugin list | `CMakeLists.txt:488–494` | seven `add_subdirectory(plugins/<p>)`, last `plugins/seraphis` |
| Lint list | `tools/hooks/guard-ci-gates.js:38–48` | nine lints incl. `lint-plugin-roster.js` |
| CPU runner | `tools/run-cpu-tests.js:45–50` | `DEFAULT_TARGETS` ends `'membrum_tests', 'seraphis_tests'` |
| Specs index | `tools/gen-specs-index.js:19–37` | `SUBSYSTEMS`, first match wins; `['seraphis', 'Seraphis']` first with its comment at :20–22; `['spectral', …]` at :31 |

---

## Group and task map

| Group | Tasks | Theme |
|---|---|---|
| G1 | T001 | Preflight: branch, phase-base SHA, ODR / subtype / FUID sweeps, GUID generation |
| G2 | T002 | Write the twelve plan §8.2 amendments back into `spec.md` |
| G3 | T003 [P], T004 [P], T005 [P], T006 [P] | Placeholder tree: identity + AU files; resources; `src/` interface stubs; test infrastructure |
| G4 | T007 | In-tree build wiring: plugin + tests `CMakeLists.txt`, root `add_subdirectory`, `.gitignore` |
| G5 | T008 [P], T009 [P], T010 [P] | Engine-config header (SC-024); parameter packs (pack-level SC-009/010); preset + update configs (SC-012.3) |
| G6 | T011 | Processor: buses (SC-011) |
| G7 | T012 | Processor: parameter latching + state (SC-009 process arm, SC-010.1–4) |
| G8 | T013 | Processor: `setupProcessing`, latency, FR-030 guards (SC-013, SC-021) |
| G9 | T014 | Processor: the render loop (SC-005, SC-006, SC-019, SC-023, SC-008.2) |
| G10 | T015 | Processor: event sort, clamp, overflow queue, slicing (SC-022, SC-008.1) |
| G11 | T016 | Processor: `setActive`, zero allocation, corrupt-stream convergence (SC-026, SC-007, SC-010 corrupt arm) |
| G12 | T017 | Controller: registration, formatting, `setComponentState` (SC-009 controller arm, SC-010.5) |
| G13 | T018 | Controller: `createView`, the 14-control placeholder `editor.uidesc`, preset-manager seam (SC-012.1–2) |
| G14 | T019 [P], T020 [P] | CPU test TU (SC-014); leaf `CLAUDE.md`, README, installers |
| G15 | T021 | `.github/workflows/ci.yml` |
| G16 | T022 | `release.yml` + `valgrind-nightly.yml` |
| G17 | T023 | Both clang-tidy scripts |
| G18 | T024 | `check-changelog-coverage.js`, `gen-specs-index.js`, `run-cpu-tests.js` |
| G19 | T025 | Root `CLAUDE.md` rosters |
| G20 | T026 → T032 | Integration: CMake registration audit (single task), regenerated artifacts, full-suite run, pluginval/bundle, portability + lints + registration greps, ASan lifecycle, isolated CPU run |

---

# GROUP 1 — Preflight

## T001 — Branch, phase base, sweeps, fresh GUIDs

**Files created:** `specs/vorago-phase11-plugin-scaffold/compliance.md` (header + "Phase base" line only).

1. `git branch --show-current` must print `feat/vorago-phase1-events-modulation` (one branch per roadmap;
   never rename it). If not, stop and surface.
2. `git rev-parse HEAD` → write it into `compliance.md` as **Phase base** (SC-027 diffs against it).
3. `ls plugins/vorago` must fail (the tree does not exist yet).
4. Re-run the spec's sweeps and paste the output into `compliance.md`:
   - `grep -rn "^namespace Vorago \?{" dsp/ plugins/ tests/` → exactly 1 hit, `tests/test_helpers/vorago_fixtures.h:73`.
   - `grep -rn "makeVoragoEngineConfig\|makeVoragoCavernConfig\|applyCavernTargets\|makeVoragoPresetConfig\|makeVoragoUpdateConfig" dsp/ plugins/ tests/` → 0 hits.
   - `grep -rn "kAUcomponentSubType " plugins/*/resources/auv3/audiounitconfig.h.in` → `Dsrm Grad Innx Itrm Mbrm Ruin Srph`.
   - `grep -rn "Vrgo" plugins/ tools/ .github/ CMakeLists.txt cmake/` → 0 hits.
   - `grep -rn "FUID k\(Processor\|Controller\)UID" plugins/*/src/plugin_ids.h` → 14 lines; save them.
5. Generate **three** v4 GUIDs: `node -e "for(let i=0;i<3;i++)console.log(require('crypto').randomUUID())"`.
   GUID 1 → `kProcessorUID`, GUID 2 → `kControllerUID`, GUID 3 → the Windows installer `AppId`. For the
   two FUIDs, split each into four `0x`-prefixed 32-bit groups (hex digits 1–8, 9–16, 17–24, 25–32 with
   the dashes removed). Confirm none of the three matches any of the 14 saved lines or Seraphis's
   installer `AppId` (`plugins/seraphis/installers/windows/setup.iss:18`). Record all three in
   `compliance.md` under "Identity (immutable)".

**Verify.** `compliance.md` holds the phase-base SHA, the five sweep outputs and three GUIDs; nothing
else in the tree changed (`git status --porcelain` lists only that file plus the pre-existing entries).

---

# GROUP 2 — Spec amendments

## T002 — Write plan §8.2's twelve amendments into `spec.md`

**File edited:** `specs/vorago-phase11-plugin-scaffold/spec.md` (only this task touches it).

Apply each of plan §8.2 items 1–12 verbatim in intent, citing the plan's P-number beside each change:

1. FR-048: after `createDropdownParameterWithDefault`, set `getInfo().defaultNormalizedValue = 3.0 / 5.0` before `addParameter` (P-1).
2. SC-019.1: delete *"possible only because of the first-block snap"*; add the gain-0.5 non-vacuity arm (`peak >= 1e-4`) and the `masterGainValueForTest() == 0.0f` snap arm; add that seam to FR-024a.2 (P-2).
3. FR-025: name the fixed-capacity (`kMaxEventsPerBlock = 1024`) stable insertion sort and the >1024 overflow rule; add SC-022 clause 9 as written in plan §4.3 (P-3).
4. SC-012.3: factory override → temp dir holding `Drones/Probe.vstpreset`; user override → a **separate empty** temp dir (P-4).
5. Edge cases / Seed determinism: replace *"It does not rewind the slots' life/ecosystem trajectories"* with *"Whether it rewinds them is not asserted"* (P-5; `vorago_engine.h:447–456`).
6. SC-008 (1), SC-023, SC-026: add the `>= 1e-4` reference-peak precondition; SC-026's hold becomes 8 s at velocity 127 with the last-1 s precondition and the no-`setActive` negative control (P-6).
7. FR-066 table: SC-008 (2) lives in `Vorago_ParamFlowReachesEngine`.
8. SC-009: runs through `process()` on an unprepared processor with `numOutputs = 0`, reading `globalParamsForTest()` / `macroParamsForTest()`; add the IDs 150 / 199 unregistered-in-band sub-arm; FR-043 states `handleMacroParamChange` ignores `id > kMacroMassId`.
9. FR-024 step 0: *"push the global parameters (FR-024a) once per `process()`, before the first slice"* (P-9).
10. FR-024 step 2: *"once per `process()`, after step 0 and before the first slice"* with the equivalence argument and the Phase 12 rule *"per block, never per slice"*; SC-014 gains the bare-`process()` arm-P rule and the `WARN`-recorded arm E (P-10).
11. FR-066 table / FR-029: `Vorago_ProcessorLifecycle` covers FR-029's no-allocation half only; the `ScopedDenormalMode` clause is *"verified by inspection (grep of processor.cpp)"*.
12. FR-002 / FR-009 / FR-010 / FR-054: name plan §5.7's content checks as their verification.

**Verify.** `grep -n "P-1\|P-2\|P-3\|P-4\|P-5\|P-6\|P-9\|P-10" specs/vorago-phase11-plugin-scaffold/spec.md`
shows each amendment site; no threshold number in the spec changed (diff review: every changed line is
one of the twelve items).

---

# GROUP 3 — Placeholder tree (four parallel tasks, all files new)

## T003 [P] — Identity, packaging and AU files

**Files created**

- `plugins/vorago/src/plugin_ids.h` — **final content**, plan §2.1 verbatim: `#pragma once`, includes
  `pluginterfaces/base/funknown.h` + `pluginterfaces/vst/vsttypes.h`; `namespace Vorago`;
  `constexpr Steinberg::int32 kCurrentStateVersion = 1;`; `static const Steinberg::FUID kProcessorUID(…)`
  / `kControllerUID(…)` with T001's GUIDs 1 and 2; `static const char* const kSubCategories =
  "Instrument|Synth";` with the GCC-13 `-Wunused-variable` rationale copied from
  `plugins/seraphis/src/plugin_ids.h:43–49`; the reserved-map comment (0–99 Global … 1200+ UNASSIGNED);
  `enum ParameterIDs : Steinberg::Vst::ParamID` with exactly `kMasterGainId = 0, kPolyphonyId = 1,
  kMacroDarknessId = 100 … kMacroMassId = 111` in `VoragoMacro` order; `kGlobalParamRangeEnd = 100`,
  `kMacroParamRangeEnd = 200`. No DSP include (the `kNumMacros` static_assert lives in `macro_params.h`).
- `plugins/vorago/src/entry.cpp` — **final content**: `plugins/seraphis/src/entry.cpp:47–85` with
  `Seraphis`→`Vorago` (`#define stringPluginName "Vorago"`, `BEGIN_FACTORY_DEF`, two `DEF_CLASS2` —
  processor `kVstAudioEffectClass` + `Vorago::kSubCategories`, controller `kVstComponentControllerClass`
  — `END_FACTORY`). Includes `processor/processor.h`, `controller/controller.h`, `plugin_ids.h`,
  `version.h`. **No `ui/*.h` include** (FR-018).
- `plugins/vorago/version.json` — exactly six keys: `version "0.1.0"`, `name "Vorago"`, `description`,
  `publisher "Krate Audio"`, `url "https://krateaudio.com/vorago/"`, `copyright`. **No `preset_subdir`**
  (FR-002).
- `plugins/vorago/CHANGELOG.md` — `## [0.1.0]` section describing the scaffold (FR-010; format of
  `plugins/seraphis/CHANGELOG.md`).
- `plugins/vorago/resources/auv3/audiounitconfig.h.in` — complete copy of
  `plugins/seraphis/resources/auv3/audiounitconfig.h.in:1–40` with `Seraphis`→`Vorago`, `Srph`→`Vrgo`
  in **both** quoted and unquoted (`kAUcomponentSubType1`) forms; `kAUcomponentName Krate Audio: Vorago`,
  `kAUcomponentTag Synthesizer`, `kAUcomponentVersion @AU_COMPONENT_VERSION@`, digit-pair comment,
  `kSupportedNumChannels 02`, flags and delegate define kept verbatim (FR-015).
- `plugins/vorago/resources/au-info.plist` — `plugins/seraphis/resources/au-info.plist` with the same
  substitutions: exactly one `AudioComponents` dict (`AUWrapperFactory`, `KrAt`, `Vrgo`, `aumu`,
  `Krate Audio: Vorago`) and exactly one `SupportedNumChannels` dict `Inputs 0 / Outputs 2` (FR-016).
- `plugins/vorago/resources/auv3/macOS/Vorago.entitlements` — byte copy of Seraphis's (FR-017).

**Test first.** None compilable yet (no target). The check is textual and runs now:
`grep -c "Vrgo" plugins/vorago/resources/auv3/audiounitconfig.h.in` ≥ 2,
`grep -c "Srph\|Seraphis" plugins/vorago/resources/auv3/audiounitconfig.h.in plugins/vorago/resources/au-info.plist plugins/vorago/src/entry.cpp` → 0 each,
`grep -c "kSupportedNumChannels 02" plugins/vorago/resources/auv3/audiounitconfig.h.in` → 1, and the
FR-002 `node -e` key check from plan §5.7 exits 0.

**Verify.** The four greps and the `node -e` check above; compile verification happens in T007.

## T004 [P] — Resource placeholders and empty-directory markers

**Files created**

- `plugins/vorago/resources/editor.uidesc` — a **minimal valid** placeholder: `<vstgui-ui-description
  version="1">` with one `<template name="editor" class="CViewContainer" size="420, 520" …>` holding a
  single untagged `CTextLabel` ("Vorago — Phase 11 placeholder"), a leading XML comment *"Phase 11
  PLACEHOLDER — Phase 13 replaces this file wholesale; stock views only"*, no bitmaps, no custom class.
  (T018 adds the `<control-tags>` block and the fourteen bound controls; this stub exists so
  `smtg_target_add_plugin_resources` in T007 has a non-empty file.)
- `plugins/vorago/resources/presets/Drones/.gitkeep` (FR-051)
- `plugins/vorago/src/ui/.gitkeep` (FR-056 — nothing else ever goes in `src/ui/` this phase)
- `plugins/vorago/docs/.gitkeep` (FR-080)

**Verify.** `grep -o 'class="[^"]*"' plugins/vorago/resources/editor.uidesc | sort -u` prints only
`class="CViewContainer"` and `class="CTextLabel"`; the three `.gitkeep` files exist.

## T005 [P] — `src/` interface stubs (compilable, behaviour-free)

Every file below declares the **final interface** from the cited plan section with **stub bodies**
(return `kResultOk` / `kResultFalse` / `0` / do nothing), so the test TUs of later tasks compile and
**fail at run time**. Each later task replaces exactly the stub bodies it owns.

**Files created**

- `plugins/vorago/src/engine/vorago_engine_config.h` — plan §2.2 declarations: `kMaxBlockSamples`
  (`= Krate::DSP::VoragoEngine::kMaxBlockSamples`), `kEngineSeed = 1u`, `kCavernSeed = 1u`,
  `kMasterGainSmoothMs = 20.0f`; `makeVoragoEngineConfig(std::size_t)` and
  `makeVoragoCavernConfig(std::size_t)` returning default-constructed structs (stub: `maxBlockSamples`
  not yet set); `applyCavernTargets(CavernVerb&, const VoragoCavernTargets&) noexcept` with an **empty**
  body. Owner of the real bodies: T008.
- `plugins/vorago/src/parameters/global_params.h` — plan §2.3: `struct GlobalParams { std::atomic<float>
  masterGain{1.0f}; std::atomic<int> polyphony{4}; };`, `clampPolyphony(int)` (stub returns 4), and the
  six functions with stub bodies (`registerGlobalParams` registers nothing; `loadGlobalParams` returns
  `false`; `formatGlobalParam` returns `kResultFalse`). Owner: T009.
- `plugins/vorago/src/parameters/macro_params.h` — plan §2.4: `struct MacroParams` with the twelve
  **explicitly initialised** atomics (`gravity{0.5f}`, others `{0.0f}`) — the initialisers are final
  here, not stubbed — the `static_assert(kMacroMassId - kMacroDarknessId + 1 ==
  Krate::DSP::VoragoMacroMatrix::kNumMacros)`, and stub bodies for `handleMacroParamChange`,
  `registerMacroParams`, `formatMacroParam`, `saveMacroParams`, `loadMacroParams` (returns `false`),
  `loadMacroParamsToController`. Owner: T009.
- `plugins/vorago/src/preset/vorago_preset_config.h` — `makeVoragoPresetConfig()` returning a
  default-constructed `PresetManagerConfig{}` (stub). Owner: T010.
- `plugins/vorago/src/update/vorago_update_config.h` — `makeVoragoUpdateConfig()` returning a
  default-constructed `UpdateCheckerConfig{}` (stub). Owner: T010.
- `plugins/vorago/src/processor/processor.h` — **final** class declaration of plan §2.5.1 (all overrides,
  all seven `…ForTest()` seams, `EventSlot`, `kMaxEventsPerBlock = 1024`, every member with its
  initialiser, including `Krate::DSP::OnePoleSmoother masterGain_{1.0f};`) and the
  `static_assert(sizeof(Processor) < 64u * 1024u)` (FR-064).
- `plugins/vorago/src/processor/processor.cpp` — stub bodies: every override forwards to `AudioEffect`
  (`initialize` → `AudioEffect::initialize(context)` only, no buses; `process` → `return kResultOk;`;
  `getLatencySamples` → `0`; `setState`/`getState` → `kResultOk` without touching the stream). Owners:
  T011–T016 in sequence.
- `plugins/vorago/src/controller/controller.h` — plan §2.6 declaration (bases `EditControllerEx1` +
  `VSTGUI::VST3EditorDelegate`, the five overrides, `presetManagerForTest()`,
  `std::unique_ptr<Krate::Plugins::PresetManager> presetManager_`). No `INoteExpressionController`, no
  `IMidiMapping` (FR-019).
- `plugins/vorago/src/controller/controller.cpp` — stub bodies forwarding to `EditControllerEx1`;
  `createView` returns `nullptr`; plus the update-config compile touch point of plan §2.6 (`#include
  "update/vorago_update_config.h"` + `static_assert(std::is_same_v<decltype(Vorago::makeVoragoUpdateConfig()),
  Krate::Plugins::UpdateCheckerConfig>)`). Owners: T017, T018.

**ODR.** All new names were swept in T001; no new class name beyond `Vorago::Processor`,
`Vorago::Controller`, `Vorago::GlobalParams`, `Vorago::MacroParams` (spec New components table).

**Verify.** Compiles in T007.

## T006 [P] — Test infrastructure and nine scaffold TUs

**Files created**

- `plugins/vorago/tests/vstgui_test_stubs.cpp` — copy of `plugins/seraphis/tests/vstgui_test_stubs.cpp`
  (`GetPluginFactory()` returning `nullptr`).
- `plugins/vorago/tests/unit/test_main.cpp` — model `plugins/seraphis/tests/unit/test_main.cpp`: the
  binary's **only** `#include <allocation_operator_overrides.h>`, `void* moduleHandle = nullptr;` (with
  the NOLINT line and rationale), `enableFTZDAZ();` before `Catch::Session().run(argc, argv)` (FR-061).
- `plugins/vorago/tests/vorago_test_fixture.h` — `namespace VoragoTest`, plan §4.2. Defines **only** what
  `Krate::Test` lacks (FR-063):
  - `class MultiPointParamValueQueue : public Steinberg::Vst::IParamValueQueue` — ctor `(ParamID)`,
    `void addTestPoint(Steinberg::int32 offset, double value)`, reports points in insertion order.
  - `class MultiParamChanges : public Steinberg::Vst::IParameterChanges` over a
    `std::vector<MultiPointParamValueQueue>`; `MultiPointParamValueQueue& addQueue(ParamID)`, `clear()`,
    `reserve(std::size_t)`.
  - `struct ProcessorFixture` (model `plugins/seraphis/tests/seraphis_test_fixture.h:158`):
    `std::unique_ptr<::Vorago::Processor> proc` (heap, FR-064), created and `initialize(nullptr)`-ed in
    the ctor; `void prepare(double sr = 48000.0, Steinberg::int32 maxBlock = 2048)` →
    `setupProcessing({kRealtime, kSample32, maxBlock, sr})` + `setActive(true)` and sizes the two
    channel buffers once to `maxBlock + 2 * kGuardWords`; `kGuardWords = 8`, `kGuardValue = -8.5e17f`;
    `Steinberg::tresult processBlock(std::size_t n, Steinberg::Vst::IEventList* ev = nullptr,
    Steinberg::Vst::IParameterChanges* pc = nullptr)` — builds `ProcessData` / `AudioBusBuffers` **on the
    stack**, writes guard words, calls `process`, `REQUIRE`s the guards intact, appends the block to
    `capturedL` / `capturedR` (which must already be reserved — the method never grows them past
    `capacity()`; it `REQUIRE`s `size() + n <= capacity()`); `tresult processNoOutputs(IParameterChanges*)`
    (`numOutputs = 0`, `numSamples = 0`); `void reserveCapture(std::size_t)`; `struct ScriptedEvent {
    std::size_t at; Steinberg::Vst::Event e; }` and `void renderScript(std::span<const ScriptedEvent>,
    std::size_t totalSamples, std::span<const std::size_t> blockPattern)` that repeats `blockPattern`
    cyclically, converts each event to `(block, at - blockStart)`, and feeds a per-block `Krate::Test::EventList`.
  - Stats helpers: `float peakOf(std::span<const float>)`, `double rmsOf(std::span<const float>)`,
    `float maxAbsDiff(std::span<const float>, std::span<const float>, std::size_t from, std::size_t to)`,
    `double rmsDiff(…same…)`, `bool allFinite(std::span<const float>)` using
    `Krate::DSP::detail::isFinite`.
- Nine scaffold test TUs, each with exactly one scaffold case that its owning task deletes:
  | File | Scaffold case | Owner |
  |---|---|---|
  | `tests/unit/processor_bus_test.cpp` | `Vorago_Scaffold_Bus` | T011 |
  | `tests/unit/param_denorm_test.cpp` | `Vorago_Scaffold_Denorm` | T009 |
  | `tests/unit/state_roundtrip_test.cpp` | `Vorago_Scaffold_State` | T009 |
  | `tests/unit/midi_event_test.cpp` | `Vorago_Scaffold_Midi` | T015 |
  | `tests/unit/lifecycle_test.cpp` | `Vorago_Scaffold_Lifecycle` | T013 |
  | `tests/unit/controller/editor_lifecycle_test.cpp` | `Vorago_Scaffold_Editor` | T010 |
  | `tests/integration/processor_audio_test.cpp` | `Vorago_Scaffold_Audio` | T008 |
  | `tests/integration/param_flow_test.cpp` | `Vorago_Scaffold_ParamFlow` | T014 |
  | `tests/integration/processor_cpu_test.cpp` | `Vorago_Scaffold_Cpu` | T019 |

  ```cpp
  TEST_CASE("Vorago_Scaffold_Bus", "[vorago][scaffold]") { SUCCEED("wiring only - deleted by T011"); }
  ```

**Verify.** Compiles in T007.

---

# GROUP 4 — In-tree build wiring

## T007 — Plugin + test `CMakeLists.txt`, root `add_subdirectory`, `.gitignore`

**Files created**

- `plugins/vorago/CMakeLists.txt` — plan §5.1, copying argument spellings from
  `plugins/seraphis/CMakeLists.txt` (model lines `:10–11, :18–82, :85, :90–95, :100–103, :108, :113,
  :118, :133–135, :140–142`): `krate_plugin_read_version(VORAGO)` then
  `krate_plugin_configure_generated_files()` (FR-001); `smtg_add_vst3plugin` with the **enumerated**
  source list (FR-003: `entry.cpp`, `plugin_ids.h`, `version.h`, both processor files, both controller
  files, the two packs, the three config headers); link `sdk vstgui_support KrateDSP KratePluginsShared`
  PRIVATE; include `${CMAKE_CURRENT_SOURCE_DIR}/src` PRIVATE; `smtg_target_configure_version_file`;
  `krate_plugin_platform_setup(${PLUGIN_NAME} TAG VORAGO BUNDLE_BASE com.krateaudio.vorago ENTITLEMENTS
  Vorago.entitlements KIND instrument)` (FR-004); `smtg_target_add_plugin_resources(… RESOURCES
  resources/editor.uidesc)`; `krate_plugin_install_to_system`; `krate_plugin_install_presets` (no
  arguments); `krate_plugin_set_warnings` (FR-005); the MSVC `/wd4459` block with a comment naming
  `dsp/include/krate/dsp/systems/timevar_comb_bank.h:931` reached via `vorago_voice.h:137` →
  `continuous_body.h`, and *"delete this block when the dsp/ shadow is removed"* (FR-006; the line is
  `:931` today, not Seraphis's stale `:915`); `if(VSTWORK_BUILD_TESTS) add_subdirectory(tests) endif()`.
- `plugins/vorago/tests/CMakeLists.txt` — plan §5.2 verbatim in content: the ten test sources, the
  **second compilation** of `../src/processor/processor.cpp` and `../src/controller/controller.cpp`,
  the five SDK sources + `vstgui_test_stubs.cpp`; link `KrateDSP KratePluginsShared Catch2::Catch2
  test_helpers vstgui_support sdk` (`sdk` **after** `vstgui_support`); include dirs `../src`,
  `${CMAKE_CURRENT_SOURCE_DIR}`, `${CMAKE_SOURCE_DIR}/tests`, `${vst3sdk_SOURCE_DIR}`,
  `${vst3sdk_SOURCE_DIR}/vstgui4`; `cxx_std_20`; definitions
  `VORAGO_RESOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources"` and
  `VORAGO_PERF_BUDGET_HEADER="${CMAKE_SOURCE_DIR}/dsp/tests/unit/systems/vorago_perf_budget.h"`; the
  `Clang|GNU` `-fno-fast-math -fno-finite-math-only` block over exactly `unit/state_roundtrip_test.cpp`,
  `unit/lifecycle_test.cpp`, `integration/processor_audio_test.cpp`, `integration/param_flow_test.cpp`
  with the comment that `processor_cpu_test.cpp` is deliberately absent; **no** `/wd4459` yet (T026
  decides it from the build log); `catch_discover_tests(vorago_tests REPORTER console)`.

**Files edited**

- `CMakeLists.txt` — add `add_subdirectory(plugins/vorago)` immediately after
  `add_subdirectory(plugins/seraphis)` (`:494`) (FR-070).
- `.gitignore` — after the Seraphis trio (`:76–78`) add exactly (FR-007):
  ```
  /plugins/vorago/resources/win32resource.rc
  /plugins/vorago/src/version.h
  /plugins/vorago/resources/auv3/audiounitconfig.h
  ```

**Test first.** The nine scaffold cases are the test: they must be discovered and pass.

**Verify.**
```bash
"$CMAKE" --preset windows-x64-release
"$CMAKE" --build build/windows-x64-release --config Release --target Vorago vorago_tests 2>&1 | tee <scratch>/t007.log | tail -5
grep -cE "warning C[0-9]|warning:" <scratch>/t007.log                      # 0
build/windows-x64-release/bin/Release/vorago_tests.exe --list-tests | grep -c "Vorago_Scaffold_"   # 9
build/windows-x64-release/bin/Release/vorago_tests.exe 2>&1 | tail -3      # All tests passed
git status --porcelain plugins/vorago                                      # nothing untracked-generated (SC-020 shape): version.h, win32resource.rc, audiounitconfig.h do NOT appear
```
If `vorago_tests` emits C4459, **record it** in `compliance.md` (T026 adds the suppression with the
comment); do not add it here.

---

# GROUP 5 — Headers that do not touch the processor (three parallel tasks)

## T008 [P] — `vorago_engine_config.h` real bodies (FR-053, FR-034a) — SC-024

**Files owned:** `plugins/vorago/src/engine/vorago_engine_config.h`,
`plugins/vorago/tests/integration/processor_audio_test.cpp`.

**Test first** — in `processor_audio_test.cpp` delete `Vorago_Scaffold_Audio`, create
`TEST_CASE("Vorago_ProcessorRendersHeldNote", "[vorago][integration]")` holding for now only
`SECTION("CavernTargetsArePushed")` (T014 adds the other two sections to this case):

- Three heap `Krate::DSP::CavernVerb`s (`std::make_unique`), each `prepare(48000.0,
  ::Vorago::makeVoragoCavernConfig(512))`.
- Input generated in the TU: 1 s of a 110 Hz sine at amplitude 0.25 on both channels, then 2 s of zeros
  (144 000 samples), processed in 512-sample blocks, out of place into per-cavern capture vectors.
- `T = VoragoCavernTargets{ .size = 0.9f, .darkness = 0.2f, .decaySeconds = 5.0f, .fog = 0.8f,
  .damperDepth = 0.9f, .mix = 0.5f, .width = 0.3f }` (designated initialisers — no positional brace
  init).
- Before **every** block: cavern A ← `::Vorago::applyCavernTargets(A, T)`; cavern B ← the seven setters
  called by hand in FR-034a order (`setSize, setDarkness, setDecaySeconds, setFog, setDamperDepth,
  setMix, setWidth`); cavern C untouched.
- `REQUIRE(maxAbsDiff(A, B, 0, end) <= 1.0e-6f)` per channel (clause 1).
- `REQUIRE(rmsDiff(A, C, 0, end) > 1.0e-3)` per channel (clause 2 — the push has an effect).
- Also: `REQUIRE(::Vorago::makeVoragoEngineConfig(2048).maxBlockSamples == 2048)`,
  `smearEnabled == true`, `smearFftSize == 2048`, `atmosGhostReverseProbability == 0.0f`,
  `atmosGhostEventTriggers == false`; `makeVoragoCavernConfig(2048)`: `maxBlockSamples == 2048`,
  `seed == 1u`, `numChannels == 8`, `spectralDiffusionEnabled == true`, `diffusionFftSize == 1024`.

With T005's stubs, clause 1 fails only by accident, clause 2 **fails** (empty `applyCavernTargets`
→ A == C), and the `maxBlockSamples` checks fail.

**Implement.** Plan §2.2 verbatim: `makeVoragoEngineConfig` sets only `maxBlockSamples`;
`makeVoragoCavernConfig` sets `maxBlockSamples` and `seed = kCavernSeed`; `applyCavernTargets` calls
the seven setters in the order above with the matching fields, each commented with its
`cavern_verb.h` line (:613, :619, :626, :652, :717, :748, :741). A header comment justifies that both
configs are the shipped defaults measured by the Phase 10 composed chain
(`dsp/tests/unit/effects/vorago_composed_chain_test.cpp:303–313`).

**Verify.** Build (serialised with the group); `vorago_tests.exe "Vorago_ProcessorRendersHeldNote" -c
"CavernTargetsArePushed"` passes; zero warnings.

## T009 [P] — Parameter packs (FR-040, FR-042, FR-044, P-1) — pack-level SC-009 / SC-010

**Files owned:** `plugins/vorago/src/parameters/global_params.h`,
`plugins/vorago/src/parameters/macro_params.h`, `plugins/vorago/tests/unit/param_denorm_test.cpp`,
`plugins/vorago/tests/unit/state_roundtrip_test.cpp`.

**Test first.**

In `param_denorm_test.cpp` delete the scaffold; create
`TEST_CASE("Vorago_ParamDenormRoundTrip", "[vorago][params]")` with `SECTION("PackHandlersDirect")`:
- `::Vorago::GlobalParams g;` for `v` in `{0.0, 0.25, 0.5, 0.75, 1.0}`:
  `handleGlobalParamChange(g, kMasterGainId, v)` → `g.masterGain == Approx(2.0 * v).margin(1e-6)`;
  `handleGlobalParamChange(g, kPolyphonyId, v)` → `g.polyphony` equals exactly `{1, 2, 4, 5, 6}` in order.
- Out-of-range: `kMasterGainId` at `1.5` → `2.0f`; at `-0.5` → `0.0f`.
- `clampPolyphony(-3) == 1`, `clampPolyphony(0) == 1`, `clampPolyphony(99) == 6`, `clampPolyphony(4) == 4`.
- `::Vorago::MacroParams m;` initial values: `gravity == 0.5f`, the other eleven `== 0.0f`.
  For each macro ID `100..111` and each `v`: `handleMacroParamChange(m, id, v)` → field
  `Approx(v).margin(1e-6)` (clamped to `[0, 1]` for `1.5` → `1.0f`).
- IDs `150` and `199` at `0.9`: all twelve fields unchanged (bit-equal to a snapshot) — the
  `id > kMacroMassId` early return.
- `Steinberg::Vst::ParameterContainer pc; registerGlobalParams(pc); registerMacroParams(pc);` →
  `pc.getParameterCount() == 14`; for `kPolyphonyId`,
  `getParameter(kPolyphonyId)->getInfo().defaultNormalizedValue == Approx(0.6).margin(1e-9)` and
  `toPlain(0.6) == 3.0` (index 3 = four voices) — **the P-1 detector**; `kMasterGainId` default `0.5`;
  `kMacroGravityId` default `0.5`; the other eleven macro defaults `0.0`; `kPolyphonyId`'s
  `getInfo().stepCount == 5`.
- `formatGlobalParam(kMasterGainId, 0.5, s)` → `kResultOk` and the string starts with `"0.0 dB"`;
  `formatGlobalParam(kPolyphonyId, …)` → `kResultFalse`; `formatMacroParam(kMacroFogId, 0.25, s)` →
  `"25%"`.

In `state_roundtrip_test.cpp` delete the scaffold; create
`TEST_CASE("Vorago_StateRoundTrip", "[vorago][state]")` with `SECTION("PackStreamsDirect")`:
- Write `saveGlobalParams` + `saveMacroParams` for non-default values (gain `1.37f`, polyphony `2`,
  macro `i` = `0.05f + 0.07f * i`) into a `Steinberg::MemoryStream` via `IBStreamer(…, kLittleEndian)`;
  stream size is exactly **56** bytes (8 + 48); load into fresh packs → every field bit-equal.
- A stream holding gain `+Inf` (bit pattern `0x7F800000` through `volatile std::uint32_t` + `memcpy`)
  and polyphony `99`: `loadGlobalParams` returns `true`, gain unchanged (`1.0f`), polyphony `6`.
- A stream holding only 4 bytes: `loadGlobalParams` returns `false`; polyphony unchanged.
- `loadMacroParams` on a stream holding 3 floats: returns `false`, fields 0–2 loaded, fields 3–11
  unchanged.
- `loadGlobalParamsToController` / `loadMacroParamsToController` with a capturing lambda: receives
  `kMasterGainId → 1.37 / 2`, `kPolyphonyId → (2 - 1) / 5 = 0.2`, and each macro ID → its value, all
  within `1e-6`.

**Implement.** Plan §2.3 and §2.4 exactly: denormalisation table (`clamp(float(value * 2.0), 0, 2)`;
`clamp(int(value * 5.0 + 1.0 + 0.5), 1, 6)`); `clampPolyphony`; registration with the plain
`addParameter(STR16("Master Gain"), STR16("dB"), 0, 0.5, kCanAutomate, kMasterGainId)` and the dropdown
followed by `poly->getInfo().defaultNormalizedValue = 3.0 / 5.0;` **before** `addParameter(poly)`;
`formatGlobalParam` `"%.1f dB"` of `20·log10(value·2)` with `-80 dB` below `1e-4`; the `loadGlobalParams`
body of plan §2.3 (isFinite + clamp; `clampPolyphony` on the int); the `macroField(MacroParams&, int)`
switch (plus `const` overload) with an unreachable `default` (`assert(false)` then the last field);
`handleMacroParamChange` returning early for `id > kMacroMassId` **before** computing the index;
`loadMacroParams` stores a read float only if `detail::isFinite`, clamped to `[0, 1]`, stopping at the
first failed read. Includes per plan §2.3 (`ui/parameter_helpers.h`, `vstparameters.h`,
`base/source/fstreamer.h`, `pluginterfaces/base/ustring.h`, `<krate/dsp/systems/vorago_engine.h>`,
`<krate/dsp/core/db_utils.h>`).

**Verify.** `vorago_tests.exe "Vorago_ParamDenormRoundTrip"` and `"Vorago_StateRoundTrip"` pass (only
the pack sections exist so far); zero warnings.

## T010 [P] — Preset and update configs (FR-050, FR-052) — SC-012.3

**Files owned:** `plugins/vorago/src/preset/vorago_preset_config.h`,
`plugins/vorago/src/update/vorago_update_config.h`,
`plugins/vorago/tests/unit/controller/editor_lifecycle_test.cpp`.

**Test first** — delete the scaffold; create
`TEST_CASE("Vorago_EditorLifecycle", "[vorago][controller][ui][lifecycle]")` (the `[lifecycle]` tag is
**required**: `valgrind-nightly.yml:283` selects by it) with `SECTION("PresetConfigIsLive")`:
- `auto cfg = ::Vorago::makeVoragoPresetConfig();` → `cfg.pluginName == "Vorago"`,
  `cfg.pluginCategoryDesc == "Synth"`, `cfg.subcategoryNames == std::vector<std::string>{"Drones"}`,
  `cfg.processorUID == ::Vorago::kProcessorUID`.
- Two temp directories under `std::filesystem::temp_directory_path()`, named with a per-run counter
  (`vorago_sc012_user_<n>`, `vorago_sc012_factory_<n>`), created by the section and removed at its end
  (P-4): the **user** one empty; the **factory** one holding a zero-byte `Drones/Probe.vstpreset`.
- `Krate::Plugins::PresetManager pm(cfg, nullptr, nullptr, userDir, factoryDir);` →
  `auto presets = pm.scanPresets(); REQUIRE(presets.size() == 1); REQUIRE(presets[0].subcategory ==
  "Drones");`
- `REQUIRE(std::filesystem::is_directory(VORAGO_RESOURCES_DIR "/presets/Drones"));`
- `auto u = ::Vorago::makeVoragoUpdateConfig();` → endpoint URL equals
  `"https://rolandzwaga.github.io/krate-audio/versions.json"` and the plugin name equals `"Vorago"`
  (field names read from `plugins/shared/src/update/update_checker_config.h` at implementation time).

**Implement.** Plan §2.7 and §2.8 verbatim (field order of `preset_manager_config.h:19–24`;
`UpdateCheckerConfig{ stringPluginName, VERSION_STR, "https://rolandzwaga.github.io/krate-audio/versions.json" }`,
model `plugins/seraphis/src/update/seraphis_update_config.h:21–27`). The update header is compiled only
by controller.cpp's touch point (T005) and this test TU.

**Verify.** `vorago_tests.exe "Vorago_EditorLifecycle" -c "PresetConfigIsLive"` passes; zero warnings.

---

# GROUP 6

## T011 — Processor buses (FR-020, FR-021, FR-022) — SC-011

**Files edited:** `plugins/vorago/src/processor/processor.cpp` (bodies of `initialize`, `terminate`,
`setBusArrangements`); `plugins/vorago/tests/unit/processor_bus_test.cpp` (owner).

**Test first** — delete the scaffold; `TEST_CASE("Vorago_ProcessorBusSetup", "[vorago][processor]")`,
processor on the heap, `initialize(nullptr)`:
- `getBusCount(kEvent, kInput) == 1`, `getBusCount(kAudio, kInput) == 0`,
  `getBusCount(kAudio, kOutput) == 1`; `getBusArrangement(kOutput, 0, arr)` → `arr == SpeakerArr::kStereo`.
- `setBusArrangements(nullptr, 0, {kStereo}, 1) == kResultTrue`;
  `(1 × kStereo in, {kStereo})`, `(0, {kMono})`, `(0, {kStereo, kStereo})` → each `kResultFalse`.
- `engineForTest() != nullptr` and `cavernForTest() != nullptr` after `initialize`; after `terminate()`
  both `== nullptr` (FR-022).

**Implement.** Plan §2.5.2 and §2.5.3: `addEventInput(STR16("Event In"))`,
`addAudioOutput(STR16("Main Out"), Vst::SpeakerArr::kStereo)`, **no** `addAudioInput`;
`engine_ = std::make_unique<Krate::DSP::VoragoEngine>(); cavern_ = std::make_unique<Krate::DSP::CavernVerb>();`;
`terminate()` assigns `nullptr` to both (not `.reset()`), clears `prepared_`, forwards; the four-line
`setBusArrangements` of §2.5.3.

**Verify.** `vorago_tests.exe "Vorago_ProcessorBusSetup"` passes; whole binary green; zero warnings.

---

# GROUP 7

## T012 — Parameter latching and processor state (FR-043, FR-045, FR-046) — SC-009, SC-010.1–4

**Files edited:** `processor.cpp` (`processParameterChanges`, the latch at the top of `process`, the
first two FR-030 guards so a `numOutputs = 0` call returns after latching, `getState`, `setState`);
`tests/unit/param_denorm_test.cpp`; `tests/unit/state_roundtrip_test.cpp`.

**Test first.**

`Vorago_ParamDenormRoundTrip`, new `SECTION("ThroughProcess")` — `VoragoTest::ProcessorFixture fx;`
(initialised, **not** prepared):
- For each of the 14 IDs × `{0, 0.25, 0.5, 0.75, 1}`: one-point `Krate::Test::ParameterChanges`
  (`addChange(id, v)`), `fx.processNoOutputs(&pc) == kResultOk`, then read
  `fx.proc->globalParamsForTest()` / `macroParamsForTest()`: gain `Approx(2v).margin(1e-6)`; polyphony
  exactly `{1, 2, 4, 5, 6}`; macros `Approx(v).margin(1e-6)`.
- Multi-point: `VoragoTest::MultiParamChanges` with `kMacroFogId` points `{0.1 @ 0, 0.9 @ 100}` →
  `fog == Approx(0.9f).margin(1e-6)` (last point wins).
- Unregistered in-band IDs: set all twelve macros to distinct non-defaults, snapshot, send IDs `150` and
  `199` at `0.9` → all twelve bit-equal to the snapshot and both globals unchanged.
- ID `200` (outside both packs) at `0.9` → nothing changes.

`Vorago_StateRoundTrip`, new sections (all `Steinberg::MemoryStream`):
- `SECTION("ByteRoundTrip")` (clause 1): set all 14 to non-defaults through `process()`; `getState(s1)`;
  a **fresh** processor `setState(s1)` then `getState(s2)`; `s1` and `s2` are both **60** bytes and
  `memcmp == 0`.
- `SECTION("DefaultStreamDecodes")` (clause 2): default processor's stream — bytes 0–3 decode to `int32
  1`, bytes 4–7 to `1.0f`, bytes 8–11 to `int32 4`, bytes 28–31 to `0.5f`, all other macro floats `0.0f`.
- `SECTION("FutureVersionRejected")` (clause 3): hand-built stream with version `2` followed by valid
  fields → `setState == kResultFalse`; all 14 atomics unchanged.
- `SECTION("TruncatedStreamKeepsMacros")` (clause 4): target processor macros set to non-defaults
  first; stream = the first **12** bytes of a valid state carrying gain `0.4f`, polyphony `3` →
  `kResultOk`, gain `0.4f`, polyphony `3`, all twelve macros unchanged.
- `setState(nullptr)` and `getState(nullptr)` → `kResultFalse`.

**Implement.** Plan §2.5.8 (`id < kGlobalParamRangeEnd` → global handler, else `id <
kMacroParamRangeEnd` → macro handler, last point per queue, `count <= 0` skipped, null `changes`
returns); in `process()`: `ScopedDenormalMode` as the **first statement**, then
`processParameterChanges(data.inputParameterChanges)`, then the FR-030 guards (all of them may be added
here in their binding order; T013 tests them); plan §2.5.10 `getState` / `setState` verbatim
(`IBStreamer(state, kLittleEndian)`, version first, `> kCurrentStateVersion` → `kResultFalse` before any
load, macros loaded only if the global block loaded).

**Verify.** Both cases pass; whole binary green; zero warnings.

---

# GROUP 8

## T013 — `setupProcessing`, latency, degenerate shapes (FR-023, FR-030, FR-033) — SC-013, SC-021

**Files edited:** `processor.cpp` (`setupProcessing`, `getLatencySamples`, the not-ready zero-fill path);
`tests/unit/lifecycle_test.cpp` (owner; delete the scaffold).

**Test first** — `TEST_CASE("Vorago_ProcessorLifecycle", "[vorago][processor]")`:

`SECTION("ReportedLatency")` (SC-013):
- Before any prepare: `WARN("latency before prepare = " << getLatencySamples())` (expect 1024; recorded,
  **not** asserted).
- For each `sr` in `{44100, 48000, 88200, 96000, 192000}`: `setupProcessing` at `sr` →
  `REQUIRE(getLatencySamples() == 3072u)` and `REQUIRE(getLatencySamples() ==
  engineForTest()->getLatencySamples() + cavernForTest()->getLatencySamples())`.
- Then `setupProcessing` at 48000 after 96000, `setActive(false)` + `setActive(true)`, one 512 block with
  all 14 parameters changed → still `3072`.
- Read immediately after the **first** `setupProcessing` (before the loop continues):
  `engineForTest()->getPolyphony() == 4` and `setPolyphonyCallCountForTest() == 1` (FR-023.4 counts its call).

`SECTION("DegenerateShapes")` (SC-021), one sub-section per shape, each `REQUIRE(process(data) ==
kResultOk)` and no crash: `numOutputs = 0`; `outputs` with `channelBuffers32 = nullptr`; mono bus
(`numChannels = 1`, a one-element pointer array — the test asserts element `[1]` was never read by
passing an array of exactly one pointer); `numSamples = 0`; `channelBuffers32[1] = nullptr`.
- **Not ready:** `process()` before `setupProcessing()` with both buffers pre-filled `0.5f` and
  `silenceFlags = 0` → every sample `== 0.0f` and `silenceFlags == 3`.
- A master-gain change (`0.25`) sent with `numSamples = 0` → `globalParamsForTest().masterGain ==
  Approx(0.5f)` afterwards.

**Implement.** Plan §2.5.4 exactly, in FR-023 order: null-component early out; `engine_->setSeed(kEngineSeed)`
**before** `engine_->prepare(sr, makeVoragoEngineConfig(kMaxBlockSamples))`; `cavern_->prepare(sr,
makeVoragoCavernConfig(kMaxBlockSamples))`; `engine_->setPolyphony(clampPolyphony(atomic))`,
`++setPolyphonyCalls_`, `lastPushedPolyphony_ = engine_->getPolyphony()`;
`masterGain_.configure(kMasterGainSmoothMs, float(sr))`, `snapGainPending_ = true`; `prepared_ = true`
last. Plan §2.5.9 `getLatencySamples` (0 when either pointer is null; after `initialize` both exist, so the
unprepared reading is the raw `0 + 1024`).
The not-ready branch of plan §2.5.6 (zero-fill both channels, `silenceFlags = 3`). `process()` still
returns right after the guards for a ready processor (rendering is T014).

**Verify.** `vorago_tests.exe "Vorago_ProcessorLifecycle"` passes; binary green; zero warnings.

---

# GROUP 9

## T014 — The render loop (FR-024, FR-024a, FR-026a, FR-027, FR-028, FR-034) — SC-005, SC-006, SC-019, SC-023, SC-008 (2)

**Files edited:** `processor.cpp` (`pushGlobalParams`, `dispatchEvent`, `renderSlice`, the slice loop,
a **minimal** `buildEventOrder`); `tests/integration/processor_audio_test.cpp`;
`tests/integration/param_flow_test.cpp` (owner; delete the scaffold).

**Test first.**

`Vorago_ProcessorRendersHeldNote`:
- `SECTION("HeldNoteIsAudible")` (SC-005): fixture 48 kHz, `prepare(48000, 512)`, registered defaults
  (no parameter changes), `NoteOn(48, 100/127.f)` at offset 0 of block 0, 8 s = 750 blocks of 512.
  `REQUIRE(allFinite)` on both channels; `REQUIRE(peakOf(last 48 000 samples) >= 1.0e-4f)` on at least
  one channel; `WARN` peak and RMS of that last second (for `compliance.md`).
- `SECTION("OutputNeverExceedsCeiling")` (SC-006): polyphony normalized `1.0` (→ 6) and master gain
  normalized `1.0` (→ linear 2.0) in block 0's changes; six notes `{36, 40, 43, 47, 50, 53}` at velocity
  `100/127.f`, offset 0; 30 s = 2813 blocks. `REQUIRE(max |x| <= 0.9661f)` on **both** channels, all
  finite; `WARN` both peaks. **Discrimination arm:** same script at master gain normalized `0.5` →
  `REQUIRE(max(peakL, peakR) >= 0.49f)`; `WARN` both. If short of 0.49, lengthen / raise velocity to
  127 — never lower 0.49 (it is `0.9661 / 2`). **Not** `[long]`; record wall time.

`TEST_CASE("Vorago_ParamFlowReachesEngine", "[vorago][integration]")`:
- `SECTION("GainZeroSilences")` (SC-019.1 + P-2): gain normalized `0.0` in block 0's changes,
  `NoteOn(48, 100/127.f)` @0, 4 s at 512 → `REQUIRE(peak < 1.0e-6f)`; **snap arm:** after block 0,
  `REQUIRE(masterGainValueForTest() == 0.0f)` exactly; **non-vacuity:** same script at gain `0.5` →
  `REQUIRE(peak >= 1.0e-4f)` (lengthen if not).
- `SECTION("PolyphonyReachesEngine")` (SC-019.2): polyphony `0.0` → `engineForTest()->getPolyphony() ==
  1`; `1.0` → `6`.
- `SECTION("PolyphonyPushIsEdgeTriggered")` (SC-019.3): read `c0 = setPolyphonyCallCountForTest()`;
  send polyphony `0.2` (→ 2) → count `== c0 + 1`; send `0.2` again → count unchanged; send nothing for 3
  blocks → unchanged.
- `SECTION("StateBeforePrepare")` (SC-019.4): processor A (prepared) with polyphony `0.2` → `getState`;
  processor B `initialize` only, `setState(stream)`, then `setupProcessing` →
  `B.engineForTest()->getPolyphony() == 2`.
- `SECTION("MacrosAreInert")` (SC-023): render M1 with all twelve macros at `1.0` (block 0) and M0 at
  defaults, same `NoteOn(48, 100/127.f)` script, 4 s → precondition `peakOf(M0, [3072, end)) >= 1e-4`
  (lengthen if not); `REQUIRE(maxAbsDiff(M0, M1, 0, end) <= 1.0e-5f)` per channel.
- `SECTION("ParamTimingIsBlockGranular")` (SC-008 (2)): 512 blocks, `NoteOn(48, 100/127.f)` @0, block
  `N = 94` (≥ 1 s warm-up). R1: gain `0.0` via `MultiParamChanges` at **offset 300** of block N; R2: gain
  `0.0` at offset 0 of block N; R0: no change. Total ≥ `N·512 + 3072 + 48 000` samples.
  `REQUIRE(maxAbsDiff(R1, R2, 0, end) <= 1.0e-5f)`; non-vacuity
  `REQUIRE(rmsDiff(R0, R1, N·512 + 3072, N·512 + 3072 + 48 000) > 1.0e-3)` (lengthen warm-up if not).

All fail against T013's code (`process()` renders nothing).

**Implement.** Plan §2.5.6 and §2.5.7:
- After the guards: `pushGlobalParams()` (edge-triggered polyphony with `++setPolyphonyCalls_`;
  `snapTo` on the first block after prepare, else `setTarget`); `macros_.apply(*engine_)`;
  `applyCavernTargets(*cavern_, macros_.computeCavernTargets())` — **once per `process()`** (P-9, P-10).
- `buildEventOrder` **minimal version for this task**: copy up to `kMaxEventsPerBlock` events in list
  order with `clampOffset` (plan §3.2), no sort, no overflow queue (T015 adds both, test-first).
- The slice loop of §2.5.6 (while-dispatch all due events; `sliceEnd = min(total, cursor + 2048, next
  offset)`; `renderSlice`; `++lastSliceCount_`), reset `lastSliceCount_ = 0` at the top.
- `dispatchEvent` per plan §3.3 (pitch `[0,127]` guard; `velocity > 0.0f` → `noteOn(pitch,
  uint8(clamp(v*127 + 0.5, 1, 127)))`, else `noteOff`; `kNoteOffEvent` → `noteOff`; everything else,
  CC64 included, ignored).
- `renderSlice`: `engine_->processStereoBlock` → `cavern_->processStereoBlock(l, r, l, r, n)` → per-sample
  `masterGain_.process()` multiply → `engine_->processOutputStage` (limiter last; never a gain after it).
- `data.outputs[0].silenceFlags = 0` on every rendered block. `getTailSamples` **not** overridden.
- Nothing copies `processOutputStage`'s internal 64-sample cadence (FR-027). No scratch vector (FR-028).

**Verify.** Both cases pass; whole binary green; record SC-005/SC-006 figures and wall times in
`compliance.md`; zero warnings.

---

# GROUP 10

## T015 — Event sort, clamping, overflow queue (FR-025, FR-026, FR-031) — SC-022, SC-008 (1)

**Files edited:** `processor.cpp` (`buildEventOrder`, the overflow reader);
`tests/unit/midi_event_test.cpp` (owner; delete the scaffold).

**Test first** — `TEST_CASE("Vorago_MidiEventTranslation", "[vorago][processor][midi]")`, reading the
engine through `engineForTest()`; `VoiceState` is `Krate::DSP::VoiceState`:

- `SECTION("NoteOnActivates")` (1): prepared 48k/512; `NoteOn(48, 100/127.f)` @0, one block →
  `getActiveVoiceCount() == 1` and exactly one slot `i < kMaxVoices` reads `VoiceState::Active`.
- `SECTION("NoteOffReleases")` (2): after (1), `NoteOff(48)` → that slot `VoiceState::Releasing`,
  `getActiveVoiceCount() == 1` (unchanged — no clause may require it to drop inside the 45 s release).
  Separately, a fresh fixture with `NoteOn(48, 0.0f)` after the note-on → same result.
- `SECTION("OutOfRangePitchDropped")` (3): `NoteOn` with pitch `128` and with pitch `-1` →
  count `0`, all slots `Idle`.
- `SECTION("NonNoteEventsIgnored")` (4): one `kPolyPressureEvent` and one `kDataEvent` (built with
  `EventList::addEvent`) after a held note → count and every slot state unchanged.
- `SECTION("OffsetTiming")` (5): A = `NoteOn(48, 100/127.f)` @300 of the first 512 block; B = a 300-sample
  block then the note @0 of the next 512 block, then 512s; C = the note @0 of the first block. Each ≥
  `3072 + 300 + 48 000` samples. Window `[3372, end)`: precondition `peakOf(A) >= 1e-4` (lengthen until it
  holds); `REQUIRE(maxAbsDiff(A, B) <= 1.0e-5f)`; negative control `REQUIRE(maxAbsDiff(A, C) > 1.0e-5f)`.
- `SECTION("TinyVelocityIsNoteOn")` (6): velocity `0.003f` → slot `Active`, not `Releasing`.
- `SECTION("OffsetClamping")` (7): NoteOn @600 in a 512 block vs @511 → `<= 1.0e-5f`; @-5 vs @0 →
  `<= 1.0e-5f`; clause-5 window and precondition.
- `SECTION("OutOfOrderEventsAreSorted")` (8, **the P-3 detector**): list `[NoteOn(55) @400, NoteOn(48)
  @100]` vs `[NoteOn(48) @100, NoteOn(55) @400]` → `<= 1.0e-5f` over the clause-5 window with the
  precondition. **Fails** against T014's unsorted copy.
- `SECTION("OverflowQueueDropsNothing")` (9): `prepare(48000, 2048)`, polyphony default 4, one 2048
  block with 1100 events: indices `[0, 1024)` alternate `NoteOn(60, 1.0f)` / `NoteOff(60)` at offset
  `= index` (1023 is a `NoteOff`); index 1024 = `NoteOn(62, 1.0f)` @1100; index 1025 = `NoteOn(64,
  1.0f)` @**5** (below the running maximum — must be clamped up to 1100, not rewind); indices
  `[1026, 1100)` = 74 alternating `NoteOn(60)` / `NoteOff(60)` at offsets 1101…1174 (1099 is a
  `NoteOff`). `REQUIRE(process == kResultOk)`, output all finite, canaries intact; then **exactly 2**
  slots `VoiceState::Active` and `getActiveVoiceCount() == 3`. **Fails** against T014 (which stops at
  1024: 0 Active).
- `SECTION("BlockSizeInvariance")` (SC-008 (1)): script over 4 s (192 000 samples) —
  `NoteOn(48, 1.0f)` @0, `NoteOn(55, 0.8f)` @37 111, `NoteOff(48)` @100 003, `NoteOn(60, 0.9f)` @150 007;
  no parameter change; rendered via `renderScript` at host blocks `{1, 7, 64, 65, 512, 2048, 4096}`
  (fixture `maxBlock` set to the block size, `4096` included — `setupProcessing` never uses it, FR-023.1).
  Precondition: the 512 reference's `peakOf([3072, end)) >= 1e-4` (lengthen if not). Every partition vs
  512: `REQUIRE(maxAbsDiff <= 1.0e-5f)` per channel. `static_assert(65 % 64 != 0)`. The 4096 run: one
  event-free 4096 block reports `lastSliceCountForTest() == 2`. `compareFingerprints` as `WARN` only.
  Record wall time; if the case exceeds ~15 s, tag per the root `CLAUDE.md` `[long]` rule — **the
  partition set is never reduced**.

**Implement.** Plan §3.2: stable insertion sort of `EventSlot{clampOffset(…), static_cast<std::int32_t>(i)}`
(strict `>` shift so equal offsets keep list order) into `eventOrder_`; overflow queue for `count > 1024`
read from `data.inputEvents` in list order after the sorted queue, effective offset `max(clampOffset,
previous effective offset)` starting from the last sorted offset, tracked by a `std::size_t
overflowNext` and an `int32` running maximum (locals of `process()`); one inline "next pending event"
helper feeding both the dispatch loop and the slice-end computation. Explicit `static_cast`s everywhere
(no narrowing in brace init). No allocation.

**Verify.** `vorago_tests.exe "Vorago_MidiEventTranslation"` passes; whole binary green; wall times
recorded; zero warnings.

---

# GROUP 11

## T016 — `setActive`, zero allocation, corrupt-stream convergence (FR-032, FR-029) — SC-026, SC-007, SC-010 corrupt arm

**Files edited:** `processor.cpp` (`setActive`); `tests/unit/lifecycle_test.cpp`;
`tests/unit/state_roundtrip_test.cpp`.

**Test first.**

`Vorago_ProcessorLifecycle`:
- `SECTION("SetActiveClearsTail")` (SC-026): hold `NoteOn(48, 1.0f)` for **8 s** (512 blocks);
  precondition `peakOf(last 48 000 samples before deactivation) >= 1e-4`; `setActive(false)`;
  `{ TestHelpers::AllocationScope s; setActive(true); REQUIRE(TestHelpers::AllocationDetector::instance().getAllocationCount() == 0); }`
  (read **inside** the scope); render 1 s with no events → `REQUIRE(peak < 1.0e-6f)` on both channels.
  **Negative control:** a second fixture runs the identical 8 s script **without** the
  `setActive(false/true)` pair → its following 1 s `peak >= 1e-4`. Lengthen the hold if either
  precondition misses — never either threshold. **Fails** against the T005 stub `setActive`.
- `SECTION("ProcessDoesNotAllocate")` (SC-007, regression guard): liveness probe first —
  `{ AllocationScope s; auto* p = new int(1); REQUIRE(count >= 1); delete p; }`. Then prepare, one 512
  warm-up block, **pre-build** 188 `Krate::Test::EventList`s and 188 `VoragoTest::MultiParamChanges`
  (note-ons/offs at varied offsets, polyphony `0.0`/`1.0` flips, gain and macro changes), `reserveCapture`
  for 188 × 512, then render all 188 blocks inside **one** `AllocationScope` and read the count inside →
  `REQUIRE(count == 0)`. (If it fails, the defect is in the processor, not the test.)
- **FR-029 by inspection** (no test possible, plan §4.1): `grep -n "ScopedDenormalMode"
  plugins/vorago/src/processor/processor.cpp` shows it as the first statement of `Processor::process()`;
  cite the file:line in `compliance.md`.

`Vorago_StateRoundTrip`, `SECTION("CorruptStreamConverges")`: a prepared processor; stream with
version 1, gain bit pattern `+Inf`, polyphony `int32 99`, twelve valid macros → `setState == kResultOk`;
`globalParamsForTest().polyphony == 6`, gain unchanged; `c0 = setPolyphonyCallCountForTest()`; one
`process()` → `c0 + 1`; a second `process()` → still `c0 + 1` (the §2.3 convergence property).

**Implement.** Plan §2.5.5: `setActive(true)` sets only `snapGainPending_ = true`; `setActive(false)`
calls `engine_->silence()` and `cavern_->reset()` when non-null; always forwards to
`AudioEffect::setActive`.

**Verify.** Both cases pass; whole binary green; zero warnings.

---

# GROUP 12

## T017 — Controller registration, formatting, component state (FR-047, FR-048, FR-050, FR-052, FR-019) — SC-009 controller arm, SC-010.5

**Files edited:** `plugins/vorago/src/controller/controller.cpp` (`initialize`, `terminate`,
`setComponentState`, `getParamStringByValue`); `tests/unit/param_denorm_test.cpp`;
`tests/unit/state_roundtrip_test.cpp`.

**Test first.**

`Vorago_ParamDenormRoundTrip`, `SECTION("ControllerRegistersFourteen")`: heap `::Vorago::Controller`,
`initialize(nullptr)`: `getParameterCount() == 14`; each of the 14 IDs has a `getParameterObject(id) !=
nullptr`; walking `getParameterInfo(i)` for `i < 14` yields exactly the 14-ID set (no soft-limit, no
extra — FR-041); defaults: `kMasterGainId` `defaultNormalizedValue == 0.5` (plain `0.5 × 2 = 1.0`),
`kPolyphonyId` `toPlain(getDefaultNormalizedValue()) == 3.0` → 4 voices, `kMacroGravityId` `0.5`, other
macros `0.0`; `getParamStringByValue(kMasterGainId, 0.5, s)` starts `"0.0 dB"`;
`getParamStringByValue(kPolyphonyId, 0.6, s)` is `"4"`; `terminate() == kResultOk`.

`Vorago_StateRoundTrip`, `SECTION("ControllerLoadsComponentState")` (clause 5): stream 1 of
`ByteRoundTrip` (all 14 non-default) → `controller.setComponentState(s) == kResultOk`; every
`getParamNormalized(id)` within `1e-6` of the streamed normalized value (gain `g / 2`, polyphony
`(n - 1) / 5`, macros as stored); version-2 stream → `kResultFalse`; `nullptr` → `kResultFalse`.

**Implement.** Plan §2.6: `initialize` → `EditControllerEx1::initialize`, `registerGlobalParams`,
`registerMacroParams`, `presetManager_ = std::make_unique<Krate::Plugins::PresetManager>(makeVoragoPresetConfig(),
nullptr, this);` (**no `UpdateChecker`**, FR-052); `terminate` → `presetManager_ = nullptr;` then base;
`setComponentState` reads the version, rejects `> kCurrentStateVersion`, then both `…ToController`
helpers with `setParam = [this](ParamID id, double v) { setParamNormalized(id, v); }`;
`getParamStringByValue` → `formatGlobalParam`, then `formatMacroParam`, else base. No
`INoteExpressionController`, no `IMidiMapping` (FR-019).

**Verify.** Both cases pass; whole binary green; zero warnings.

---

# GROUP 13

## T018 — `createView`, the 14-control placeholder, preset-manager seam (FR-054, FR-055) — SC-012.1–2

**Files edited:** `plugins/vorago/src/controller/controller.cpp` (`createView`),
`plugins/vorago/resources/editor.uidesc` (T004's stub, now completed),
`tests/unit/controller/editor_lifecycle_test.cpp`.

**Test first** — in `Vorago_EditorLifecycle`:
- `SECTION("HarnessCycles")` (SC-012.1): heap controller, `initialize(nullptr)`;
  `Krate::TestSupport::exerciseEditorLifecycle(*controller, "editor", std::string(VORAGO_RESOURCES_DIR)
  + "/editor.uidesc")` completes 3 cycles. Then `REQUIRE(controller->presetManagerForTest() != nullptr)`.
- `SECTION("EditorBindsFourteenControls")` (SC-012.2): own `VSTGUI::VST3Editor(controller, "editor",
  "editor.uidesc")` (resolved via the resources dir exactly as the harness does), `attached(nullptr,
  nativePlatformType())`, recursive walk collecting `CControl`s with `getTag() >= 0` (a `CTextLabel` is
  a `CControl` with tag −1): `REQUIRE(size == 14)`; tag set equals `{0, 1, 100, …, 111}`;
  `REQUIRE(dynamic_cast<VSTGUI::COptionMenu*>(control with tag 1) != nullptr)`; `removed()`,
  `forget()`/`release()`. **Fails** against T004's stub (0 tagged controls).
- `createView("nonsense") == nullptr`.

**Implement.**
- `createView`: `FIDStringsEqual(name, Vst::ViewType::kEditor)` → `new VSTGUI::VST3Editor(this,
  "editor", "editor.uidesc")` (model `plugins/seraphis/src/controller/controller.cpp:434–436`), else
  `nullptr`. No `createCustomView` / `verifyView`, no raw view pointers held (FR-055).
- `editor.uidesc` (plan §2.10): keep T004's banner comment; add `<colors>` / `<fonts>` minimal sets;
  `<control-tags>` with `MasterGain`=0, `Polyphony`=1, `MacroDarkness`=100 … `MacroMass`=111; template
  `"editor"` 420 × 520 with thirteen `CSlider`s (master gain + twelve macros) and one `COptionMenu`
  (polyphony), each beside an untagged `CTextLabel`, using the attribute sets at
  `plugins/seraphis/resources/editor.uidesc:189–200`. No bitmaps, no custom class names.

**Verify.** `vorago_tests.exe "[lifecycle]" --list-tests` lists `Vorago_EditorLifecycle` (SC-012.4);
the case passes; the FR-054 class grep of plan §5.7 prints nothing; whole binary green; zero warnings.

---

# GROUP 14 — Two parallel tasks (disjoint files)

## T019 [P] — `Vorago_ProcessorCpu` (FR-067, FR-067a) — SC-014

**File owned:** `plugins/vorago/tests/integration/processor_cpu_test.cpp` (delete the scaffold).

**Test first (this *is* the deliverable).**
`TEST_CASE("Vorago_ProcessorCpu", "[vorago][.perf][performance]")` — hidden from a default run.
`#include VORAGO_PERF_BUDGET_HEADER` and use `Krate::DSP::TestUtils::Vorago::kReferenceNs` fully
qualified (single source; never re-typed). Plan §4.3 SC-014 exactly:
- **Arm P:** a `::Vorago::Processor` prepared through the fixture at 48 kHz / 512, polyphony 4; notes
  `{36, 40, 43, 47}` velocity `100/127.f` in an `EventList` on the **first warm-up block only**; the
  `ProcessData`, stereo bus and buffers built **once before timing**; every timed block has
  `inputEvents = nullptr`, `inputParameterChanges = nullptr`; the timed region contains only bare
  `proc->process(data)` calls (no capture, canary check, allocation or fixture call).
- **Arm D:** heap `VoragoEngine` (`setSeed(::Vorago::kEngineSeed)`, `prepare(48000,
  makeVoragoEngineConfig(2048))`, `setPolyphony(4)`, four `noteOn(n, 100)`), heap `CavernVerb`
  (`prepare(48000, makeVoragoCavernConfig(2048))`), local `OnePoleSmoother` configured at
  `kMasterGainSmoothMs` and snapped to `1.0f`; direct chain engine → cavern in place → gain loop →
  `processOutputStage`.
- 100 warm-up blocks per arm, discarded; then 16 trials **interleaved** P, D, P, D …, each timing 100
  blocks with `std::chrono::steady_clock`; best-of-16 per arm ÷ 100.
- `REQUIRE(P_best <= 1.05 * D_best)` (FR-067a, gating). `WARN` `P_best`, `D_best`, the ratio, and
  `P_best / kReferenceNs` (FR-067, recorded only).
- **Arm E** (`WARN`-recorded, not gated): fresh processor at 48 kHz / `maxBlock = 2048`, polyphony 4,
  one 2048 warm-up block with the four notes; 16 trials, each timing **one** 2048-sample `process()`
  whose pre-built `EventList` holds 1024 events in strictly **reverse** offset order (2047 down to
  1024, alternating `NoteOn(60)` / `NoteOff(60)`); best-of-16 ns and its ratio to `2048 / 48000 s`.

**Verify.** Builds (serialised); `vorago_tests.exe "[.perf]" --list-tests` lists `Vorago_ProcessorCpu`;
`vorago_tests.exe --list-tests` does **not** list it; the default run stays green. **Do not run the
timed case here** — T032 runs it alone.

## T020 [P] — Leaf `CLAUDE.md`, README, installers (FR-008, FR-009)

**Files created**

- `plugins/vorago/CLAUDE.md` — shape of `plugins/seraphis/CLAUDE.md`: type + AU identity (`aumu` /
  `Vrgo` / `KrAt`, bundle `com.krateaudio.vorago`), roadmap pointer, *"No DSP lives in this plugin"*,
  src skeleton, the three generated files, buses, the reserved param-ID table, both FUIDs marked
  **immutable**, `vorago_tests` invocation, pluginval path. **Plus the five decisions** (FR-009): (1)
  OQ-7 deferred to Phase 12 with the controller-FUID host-cache caveat; (2) preset categories grow only,
  `Drones` permanent, never renamed; (3) the soft-limit omission and why (`vorago_macro_matrix.h:968`
  rewrites `setOutputSaturation` every `apply()`); (4) polyphony "1".."6", the 30 % ceiling gates only
  the default 4, and 5–6 measured 37.99–43.41 % engine-only in Phase 10; (5) CC64 deferred to Phase 12
  as a wrapper-side note-off latch. And the P-10 rule: live macros are pushed **once per `process()`,
  never per slice**.
- `plugins/vorago/README.md` — one paragraph + build / test / pluginval commands.
- `plugins/vorago/installers/windows/setup.iss` — `plugins/seraphis/installers/windows/setup.iss` with
  `Seraphis`→`Vorago` and T001's **GUID 3** as `AppId`.
- `plugins/vorago/installers/linux/README.txt` — Seraphis's, retitled.

**Test first / verify** — the content checks of plan §5.7, each must print ≥ 1:
`grep -c "OQ-7"`, `grep -ciE "host-cache|host cache"`, `grep -c "Drones"`,
`grep -ciE "grow only|only grow|never rename"`, `grep -ciE "soft-limit|soft limit"`, `grep -c "37.99"`,
`grep -c "CC64"` — all over `plugins/vorago/CLAUDE.md`; `grep -c "Seraphis\|13374501" plugins/vorago/installers/windows/setup.iss` → 0.

---

# GROUP 15

## T021 — `.github/workflows/ci.yml` (FR-071)

**File edited:** `.github/workflows/ci.yml`. Each edit is a copy of its Seraphis twin with
`seraphis`→`vorago`, `Seraphis`→`Vorago`, `Srph`→`Vrgo`.

1. Lint-checked sites: detect-changes output, paths-filter, the `for p in` loop, the `$GITHUB_OUTPUT`
   echo, the three FetchContent `hashFiles` keys (`'plugins/vorago/CMakeLists.txt'`), all nine
   `for plugin_info in \` blocks and their `case` arms; the macOS build entries name
   `Vorago_AU:Vorago_AUV3` (as Seraphis's at `:551`).
2. Non-lint sites: Windows artifact upload (model `:474–479`), macOS `auval -v aumu Vrgo KrAt` step
   (model `:764–773`), macOS AUv3 bundle verification (`:824–830`), macOS artifact upload with `.vst3`,
   `.component`, `AUv3.app` (`:903–911`), Linux artifact upload (`:1176–1181`).

**Test first.** Before editing run `node tools/lint-plugin-roster.js` → it **fails** naming
`vorago`'s missing `ci.yml` sites (the directory exists since T003).

**Verify.** `node tools/lint-plugin-roster.js` reports no `ci.yml` finding (other files still fail until
G16–G18); `grep -c "auval -v aumu Vrgo KrAt" .github/workflows/ci.yml` → 1;
`grep -c "Vorago AUv3" .github/workflows/ci.yml` ≥ 1;
`grep -cE "name: Vorago-(Windows-x64|macOS|Linux-x64)" .github/workflows/ci.yml` → 3;
`grep -ci vorago .github/workflows/ci.yml` vs `grep -ci seraphis …` (51) — record both and explain any
difference in `compliance.md`.

---

# GROUP 16

## T022 — `release.yml` and `valgrind-nightly.yml` (FR-072, FR-073)

**Files edited:** `.github/workflows/release.yml` — `- vorago` in the dispatch choice list (`:41`),
`'plugins/vorago/CMakeLists.txt'` in the `hashFiles` key (`:138`);
`.github/workflows/valgrind-nightly.yml` — `vorago_tests` in the build list (`:276`) and in the
`for bin in` run list (`:283`, which filters by `'[lifecycle]'`).

**Test first.** `node tools/lint-plugin-roster.js` names both files for `vorago` before the edit.
**Verify.** It no longer does.

---

# GROUP 17

## T023 — Both clang-tidy scripts (FR-074, FR-075)

**Files edited:** `tools/run-clang-tidy.ps1` — `"vorago"` in the `ValidateSet` (`:60`), a `"vorago" { … }`
case adding `plugins/vorago/src` **and** `plugins/vorago/tests` (the Seraphis case at `:196–207` records
why both), both directories in the `all` case; `tools/run-clang-tidy.sh` — a `vorago)` case with the
same two directories, both in `all)`, and `vorago` in the usage text (`:63`).

**Test first.** The roster lint names both scripts before the edit. **Verify.** It no longer does.
(Running clang-tidy itself is T030.)

---

# GROUP 18

## T024 — `check-changelog-coverage.js`, `gen-specs-index.js`, `run-cpu-tests.js` (FR-076, FR-077, FR-081)

**Files edited**
- `tools/check-changelog-coverage.js` — `'vorago'` appended to `PLUGINS` (`:50`).
- `tools/gen-specs-index.js` — `['vorago', 'Vorago'],` in `SUBSYSTEMS` **before** `['spectral', …]`
  (place it directly after `['seraphis', 'Seraphis']`), with a comment in the style of `:20–22`:
  Vorago phase slugs contain `spectral` (`vorago-phase4-spectral-smear`) and would otherwise be filed
  under "DSP / Spectral".
- `tools/run-cpu-tests.js` — `'vorago_tests'` after `'seraphis_tests'` in `DEFAULT_TARGETS` (`:45–50`).

**Test first.** `node tools/lint-plugin-roster.js` still names `check-changelog-coverage.js`;
`grep -n "vorago-phase4-spectral-smear" specs/INDEX.md` shows it under "DSP / Spectral" today.

**Verify.** `node tools/lint-plugin-roster.js` exits **0** (every lint-checked site now present);
`node tools/check-changelog-coverage.js vorago` exits 0 (FR-010);
`grep -c "'vorago', 'Vorago'" tools/gen-specs-index.js` → 1;
`grep -c "'vorago_tests'" tools/run-cpu-tests.js` → 1. (INDEX.md is regenerated in T027.)

---

# GROUP 19

## T025 — Root `CLAUDE.md` rosters (FR-079)

**File edited:** `CLAUDE.md` — add Vorago at every roster site: the Project Overview list (a
**Vorago** bullet: dark-ambient drone instrument, `plugins/vorago/`, AU `aumu` / `Vrgo`), the Monorepo
Structure prose roster, the per-directory leaf list (`[vorago](plugins/vorago/CLAUDE.md)`), the pluginval
block (`# Vorago` + the `Vorago.vst3` command), the plugin test-target list
(`--target vorago_tests   # Vorago`), the "Built plugins are at" list, the clang-tidy `-Target` roster
(`…|seraphis|vorago`), and the Quick Reference rows (Add Vorago parameter, Add Vorago test, Change
Vorago UI).

**Test first.** Record `grep -ci vorago CLAUDE.md` before the edit.
**Verify.** The count rises; each of the eight named sites contains `vorago`/`Vorago` (check each by
`grep -n`); `plugins/vorago/CLAUDE.md` exists (SC-025.5).

---

# GROUP 20 — Integration (sequential; last)

## T026 — CMake registration audit (single task)

**Files checked (edit only if something is wrong):** `plugins/vorago/CMakeLists.txt`,
`plugins/vorago/tests/CMakeLists.txt`, root `CMakeLists.txt`.

1. The `smtg_add_vst3plugin` source list names **every** `.cpp`/`.h` under `plugins/vorago/src/` (FR-008;
   `find plugins/vorago/src -name "*.cpp" -o -name "*.h"` vs the list — no glob anywhere).
2. The `vorago_tests` list names all ten test TUs, the **second compilation** of every plugin `.cpp`
   (`processor.cpp`, `controller.cpp`; if a split past ~1500 lines added `processor_params.cpp` etc., it
   must be here too), the five SDK sources and `vstgui_test_stubs.cpp`; link order has `sdk` after
   `vstgui_support`; `VORAGO_RESOURCES_DIR` and `VORAGO_PERF_BUDGET_HEADER` are defined; the
   `-fno-fast-math` list is exactly the four TUs of T007.
3. **C4459 decision (plan §5.2):** if any T007–T019 build log showed C4459 on `vorago_tests`, add
   `if(MSVC) target_compile_options(vorago_tests PRIVATE /wd4459) endif()` with the same comment as the
   plugin's block; otherwise record "not needed" in `compliance.md`.
4. Root `CMakeLists.txt` has `add_subdirectory(plugins/vorago)` exactly once, after `plugins/seraphis`.
5. `grep -rn "allocation_operator_overrides" plugins/vorago/tests` → exactly one hit, `unit/test_main.cpp`.
6. Clean reconfigure: `"$CMAKE" --preset windows-x64-release` succeeds; `git status --porcelain
   plugins/vorago` is empty after the build in T028 (SC-020).

## T027 — Regenerated artifacts and generator checks (FR-078) — SC-017

```bash
node tools/gen-repo-map.js
node tools/gen-specs-index.js
node tools/gen-repo-map.js --check
node tools/gen-specs-index.js --check
node tools/gen-symbols.js --check          # dsp/include untouched -> no change
grep -n "vorago-" specs/INDEX.md            # every vorago-* slug under the "Vorago" heading
```
All `--check` runs exit 0; `specs/INDEX.md` files every `vorago-*` slug (including
`vorago-phase4-spectral-smear`) under "Vorago".

## T028 — Full-suite run, warning count, pluginval, bundle guard — SC-001 (Windows), SC-002, SC-003, SC-018, SC-020

```bash
# SC-001: target-scoped, clean objects first (touch every vorago source), log captured once
touch plugins/vorago/src/*.cpp plugins/vorago/src/*/*.cpp plugins/vorago/tests/unit/*.cpp plugins/vorago/tests/unit/controller/*.cpp plugins/vorago/tests/integration/*.cpp
"$CMAKE" --build build/windows-x64-release --config Release --target Vorago vorago_tests 2>&1 | tee <scratch>/sc001.log | tail -3
grep -cE "warning C[0-9]|warning:" <scratch>/sc001.log                            # 0, no path filter
build/windows-x64-release/bin/Release/vorago_tests.exe --list-tests               # the 8 non-hidden FR-066 names, no Vorago_Scaffold_*
build/windows-x64-release/bin/Release/vorago_tests.exe "[.perf]" --list-tests     # Vorago_ProcessorCpu
build/windows-x64-release/bin/Release/vorago_tests.exe 2>&1 | tee <scratch>/vorago_tests.log | tail -5   # All tests passed
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Vorago.vst3"  # exit 0, 0 failures
node tools/check-bundle.js build/windows-x64-release/VST3/Release/Vorago.vst3     # OK   Vorago.vst3: editor.uidesc + moduleinfo.json present
git status --porcelain plugins/vorago                                             # empty (SC-020)
```
Record the SC-006 / SC-008 (1) wall times from the log. `grep -c "Vorago_Scaffold_"` over the list
output must be 0 (every scaffold deleted by its owner).

## T029 — Portability, lints, registration greps, shared-code diff — SC-015, SC-017, SC-025, SC-027

```bash
node tools/check-portability.js $(git ls-files --cached --others --exclude-standard plugins/vorago | grep -E '\.(cpp|h)$')   # every file this phase adds, exit 0 (SC-015)
node tools/check-portability.js            # the default changed-vs-origin/main set also exit 0
for l in lint-layers lint-odr lint-arch-guarded-includes lint-float-bit-goldens lint-midi-timing-goldens \
         lint-platform-type-literals lint-allocation-operator-overrides lint-simd-aligned-loadstore lint-plugin-roster; do
  node tools/$l.js || echo "FAIL $l"; done                                        # nine lints, all exit 0 (SC-017, SC-025.1)
grep -c "auval -v aumu Vrgo KrAt" .github/workflows/ci.yml                        # 1
grep -c "Vorago AUv3" .github/workflows/ci.yml                                    # >= 1
grep -cE "name: Vorago-(Windows-x64|macOS|Linux-x64)" .github/workflows/ci.yml    # 3
grep -c "/plugins/vorago/" .gitignore                                             # 3
grep -c "'vorago', 'Vorago'" tools/gen-specs-index.js                             # 1
grep -c "'vorago_tests'" tools/run-cpu-tests.js                                   # 1
git diff --stat <phase-base>..HEAD -- dsp/ plugins/seraphis/ plugins/shared/      # empty (SC-027), plus: git status --porcelain dsp/ plugins/seraphis/ plugins/shared/ empty
grep -rn "std::isnan\|std::isinf\|std::isfinite" plugins/vorago                   # nothing (FR-062)
```
Portability reminders the gate enforces: no narrowing in brace init (designated initialisers for
`VoragoCavernTargets`; explicit `static_cast` for `int32`/`size_t`); every float literal
`f`-suffixed; no platform headers. If Linux/macOS doubt remains, run the libstdc++ syntax pass in WSL
over the same files (`g++ -std=c++20 -fsyntax-only …`).

## T030 — clang-tidy — SC-016

```powershell
./tools/run-clang-tidy.ps1 -Target vorago -BuildDir build/windows-ninja   # >= 1 file under plugins/vorago/src and >= 1 under plugins/vorago/tests analysed, 0 warnings
./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja      # file list includes both vorago directories
```
The ninja build dir must be regenerated first if `plugins/vorago` is missing from its
`compile_commands.json` (`clang-tidy-setup` skill). Fix every warning; none is "pre-existing". The `.sh`
script's coverage is proven statically by `lint-plugin-roster.js` sections 5–6 (T029).

## T031 — ASan editor lifecycle — SC-012.5

```bash
"$CMAKE" -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON
"$CMAKE" --build build-asan --config Debug --target vorago_tests 2>&1 | tee <scratch>/asan-build.log | tail -3
build-asan/bin/Debug/vorago_tests.exe "[lifecycle]" 2>&1 | tee <scratch>/asan-run.log | tail -5   # exit 0, no "AddressSanitizer" in the log
```
Record in `compliance.md` that the first nightly valgrind run of `Vorago_EditorLifecycle` is a
**follow-up**, not a gate.

## T032 — Isolated CPU run — SC-014 (run LAST, ALONE, nothing else executing)

```bash
node tools/run-cpu-tests.js vorago_tests 2>&1 | tee <scratch>/cpu.log
```
`REQUIRE(P_best <= 1.05 × D_best)` must pass. Record `P_best`, `D_best`, the ratio, `P_best /
kReferenceNs` and arm E's figure from the log. If the gate fails: confirm nothing else ran, let the
machine idle, re-run **this suite alone once**; only then treat it as a defect. Never relax 1.05, never
shrink the workload. A `P_best` above `kReferenceNs`, or arm E slower than real time, is **surfaced to
the user** as a finding.

**Out of local reach (CI):** SC-001 on macOS/Linux and SC-004 (`auval -v aumu Vrgo KrAt` →
`AU VALIDATION SUCCEEDED`) are verified by the CI legs T021 added; record them as pending-CI in
`compliance.md` until a run shows them.

---

## Compliance ledger (fill from actual output, never from memory)

When the phase reports complete, `specs/vorago-phase11-plugin-scaffold/compliance.md` has:

- **every FR row** citing a `file:line` under `plugins/vorago/` (or the edited roster file) that was
  **opened and read** — FR-029's `ScopedDenormalMode` row cites the grep line, not a test;
- **every SC row** citing the `TEST_CASE` (and SECTION) name **and the measured figure from the log**:
  SC-005 last-second peak and RMS; SC-006 peaks at gain 2.0 and at gain 1.0 (≥ 0.49 arm) and wall time;
  SC-008 (1) worst max-abs difference per partition and wall time; SC-013 the pre-prepare latency (1024
  expected) and the five 3072 readings; SC-014 `P_best`, `D_best`, ratio, `P_best / kReferenceNs`, arm E;
  SC-001 the warning count (0) from the target-scoped log; SC-003 the pluginval exit line; SC-018 the
  check-bundle `OK` line; SC-025's six grep outputs and the `ci.yml` vorago-vs-seraphis count with its
  explanation; SC-027's empty diff against the recorded phase base;
- SC-004 and the non-Windows legs of SC-001 marked pending-CI until a CI run shows them, and the
  valgrind nightly marked as a follow-up.

A table of ✅ without those numbers is worse than an honest ❌.
