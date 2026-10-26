# Tasks: Profundum Phase 13 — Factory Presets & Release Readiness

**Spec:** `specs/profundum-phase13-presets-release/spec.md` (Clarifications Q1–Q7, 2026-10-24) · **Plan:**
`specs/profundum-phase13-presets-release/plan.md` (§0–§13; rulings P-1 … P-11; §2.0 row amendments) · **Roadmap:**
`specs/Profundum-roadmap.md` Phase 13 (`:1052-1069`), Phase 12 handoff 13.1–13.5 (`:1043-1050`), constraints (`:1089-1112`)
· **Phase base:** `e6095b24` on `synth/profundum`
**Status:** Task list (2026-10-24). The build stage has not started. No commit tasks: commits happen outside this workflow.

**Deliverables.** The FR-057 pure code move of the Phase 9 audibility readers into
`dsp/tests/unit/systems/profundum_audibility_readers.h` (with the P-1 render shim and the P-10 lifts L1–L11); the Catch2-free
`PresetHost` and state drive; `tools/profundum_preset_defs.h` (53-column `Capability`, the 53-preset library);
`tools/profundum_preset_generator.cpp` with the targets `profundum_preset_generator` / `generate_profundum_presets`; six category
directories; the header preset button, the `PresetBrowserView` overlay and the `kMsgLoadPresetState` whole-stream load; the P-11
processor seam `setEngineSeamsForTest`; the processor-path probe, showcase, sweep, mono-sum, true-peak, distinctness,
determinism, loudness, load, RT and CPU cases; the audition set; `MANUAL.md`, `docs/`, CHANGELOG bullets and the roster edits.
**No `dsp/include` file is touched. No parameter, range or state version changes** (`kCurrentStateVersion = 3`,
`kStateV3Bytes = 574`). All paths are repo-relative to `f:/projects/iterum`.

---

## Read this first (every executor)

### Canonical order inside every task

1. Write the failing test **first**, in the named file and `TEST_CASE`, with the exact assertions given. A case that guards
   behaviour an earlier task built names its **non-vacuity arm** (the control that proves it can fail).
2. Implement the minimum that makes it pass, exactly as the cited plan section pins it.
3. Zero warnings (`warning C[0-9]|warning:` count 0). "Pre-existing" is not a category.
4. Tests pass at the group gate.

**Executors do not build or run tests**; the group gate does, after every group. The gate builds the dispatcher's targets
(`Profundum profundum_tests profundum_preset_generator`, plus `dsp_systems_tests` for G3) and runs the phase filter
`'[profundum]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]'` (`.claude/workflows/profundum-phase.js:551-552`).
**Every TEST_CASE of this phase carries `[profundum]`** plus its component tags (`[preset]`, `[browser]`, `[probe]`, …). Hidden
cases (`[.probe]`, `[.pilot]`, `[.listen]`, `[.install]`, `[.perf]`) and `[long]` cases are written by executors and run by the
main loop. The FR-057 move edits `dsp/tests/` only (not `dsp/include`, `dsp/src`, `plugins/shared` or `tests/test_helpers`), so
the workflow's "modified tracked shared file" rule (`profundum-phase.js:546-550`) does not widen any gate.

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target Profundum profundum_tests profundum_preset_generator -- /nr:false 2>&1 | tee F:/tmp/p13-build.log | tail -5
grep -cE "warning C[0-9]|warning:" F:/tmp/p13-build.log      # must print 0
build/windows-x64-release/bin/Release/profundum_tests.exe "[profundum]~[long]~[.]" 2>&1 | tail -5
```

Catch2 filters are positional; tags go in brackets. Never `ctest -R`. The post-build copy to
`C:/Program Files/Common Files/VST3/` may fail with a permission error; compilation still succeeded. Build with `-- /nr:false`
(MSBuild node reuse blocks probes).

### Parallelism

`[P]` tasks inside one group create **only brand-new files**, disjoint from every other task in the group. Every task that edits a
file that already exists — `processor.h/.cpp`, `controller.h/.cpp`, `plugin_ids.h`, `panel_sub_controller.h/.cpp`, `editor.uidesc`,
`profundum_preset_config.h`, any CMake list, `spec.md`, the Phase 9 TU, any test TU created by an earlier group (including the G5
placeholders), `tools/profundum_preset_defs.h` after G5, `preset_render_support.h` after G13 — is **alone in its group**. All
Profundum test TUs link into the one `profundum_tests` binary.

### Main-loop tasks (STOP AND SURFACE)

Any task whose work is a run longer than ~5 minutes (move-gate runs, probe, pilot, tree generation, `[long]` library lanes,
suites, clang-tidy, CPU, pluginval, release-readiness) writes its lane script and returns without running it. Where a **later
group depends on the run's result** (T002 baseline, T004 move verdict, T017 probe, T023 pilot, T026 tree, T027 library lanes) the
task returns status **`blocked`** with notes "main loop: run `<script>`; unblock `<Tnnn>` when `<condition>`", because a `done`
whose notes say "stop" is not honoured (`profundum-phase.js:592-600`). Close-lane tasks (G27) return `done` with notes "deferred
to the main loop at close: run `<script>`". The main loop records every reading in `evidence/run-log/<Tnnn>.txt`.

### Rules for every file of this phase

- **`dsp/` fence.** The only `dsp/` edits are T003's: `dsp/tests/unit/systems/profundum_engine_audibility_test.cpp` (shrinks) and
  the new `dsp/tests/unit/systems/profundum_audibility_readers.h`. No `dsp/include` edit and no `dsp/tests/CMakeLists.txt` edit
  (headers are not listed; the TU stays registered). Any other `dsp/` need: STOP AND SURFACE (FR-023).
- **`plugins/shared/` fence.** Not touched. `PresetManager`, `PresetBrowserView`, `OutlineBrowserButton` are used as they are.
- **No parameter is added, removed, retyped or re-ranged; no state-format change.** `kMsgLoadPresetState` is a message.
- Never `std::isnan` / `std::isinf` / `std::isfinite` / `std::fpclassify`. Use `Krate::DSP::detail::isFinite` or the bit test
  `(bits & 0x7F800000u) != 0x7F800000u`. Non-finite test inputs are built from bit patterns through a `volatile`.
- No brace-init narrowing; designated initialisers or copy-then-assign for `float` fields.
- Plugin types are `::Profundum::…`; plugin test code lives in `ProfundumPluginTest`. `Krate::DSP::ProfundumTest` is the dsp helper
  namespace (`param_path_audibility_helpers.h:88`). In Profundum tests the moved readers are reached only through
  `namespace auRd = Krate::DSP::ProfundumAudibility;`, never by `using namespace` (plan §4.7: the Phase 11 port defines
  same-named functions in `ProfundumPluginTest`).
- Every `Processor`, `Controller` and `ProfundumEngine` in a test or tool lives on the heap.
- **No bit-exact float goldens across binaries.** Same-binary comparisons only; renders compare through `render_fingerprint.h`
  (`kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4`). `memcmp` of two streams from one process is allowed.
- **ODR.** The names the plan claims (§4.1 table and shim, §4.3, §4.4, §4.6, §4.7, §8) are re-swept by T001. Any further new
  name: `grep -rnw <Name> dsp/ plugins/ tools/ tests/` first, 0 hits recorded in the run log. Never a roadmap hazard name
  (`CharacterProcessor`, `SubOscillator`, `StereoField`, `MorphEngine`, `MorphNode`, `TransientDetector`, `HarmonicCloud`,
  `SpectralState`, `MacroMapper`).
- **Real-time safety.** Nothing reachable from `process()` allocates, locks, throws or does I/O. The load message runs in
  `notify` on a non-audio thread; the audio thread sees only the Phase 11 restore hand-off (`processor.h:227-232`).
- **FR-023 stop.** A probe miss, a column that cannot be showcased from the shipped surface, a decoded field with no carrier, or a
  re-authoring loop that cannot reach a bar: STOP AND SURFACE with the measured value. Never edit a bar, a tolerance, F, the
  ±4 dB band, the 2× click factor, a CPU budget, or `dsp/`.
- Phase 9 evidence (`specs/profundum-phase9-voice-engine/evidence/`) is never left modified: any run that writes it is followed by
  `git checkout -- specs/profundum-phase9-voice-engine/evidence/`.

### Ruling table (plan §2 and §13; T001 blocks until each is recorded)

| Ruling | What the tasks build (the plan's reading) | If ruled otherwise | Needed before | Tasks |
|---|---|---|---|---|
| **P-1** render shim (one redirected `auPt` alias) | §4.1 edit 3 + the `render` shim | A byte-verbatim move leaves the processor path a per-row re-port, which FR-057 forbids: STOP, re-cut T003, T015–T018 | G3 | T001, T003, T015 |
| **P-10** lifts L1–L11 | §4.1 edit 4; arms call lifted functions only | Route (a) glue + equivalence arms under an amended FR-057: re-cut T003, T018 | G3 | T001, T003, T018 |
| **P-11** `setEngineSeamsForTest` | §2 P-11 code at `processor.cpp:276` | AT-13 is a G15 FR-023 stop; no AT-13 preset is authored | G10 | T001, T012, T017 |
| **P-2** AT-13 by stored variant | AT-13 primary Rectify (§7 row 13), Fracture secondary (row 9); both variants claimed | Both variants per claim needs a primary override, which C-5 forbids: STOP | G16 | T001, T018, T023, T024 |
| **P-3 / P-4** single-state claims store p = 0; the AT-33 claimant stores SPREAD / spread 1 / depth 1 | §6 rule 7 | Re-cut T018 arms | G16 | T001, T018, T023 |
| **P-5** steady components via `auSteady` | §5.8 components 10–12 | Re-cut `describePreset` | G16 | T001, T018 |
| **P-6** carrier-less pins | §6 rule 2, §5.2 | FR-023 stop on the first twin | G13 | T001, T015, T023 |
| **P-7** F = max(2.5 · t_max, 1.0), units | §5.8 | Re-cut `presetDistance` | G16 | T001, T018 |
| **CPU MONO arm** (Voice Mode forced Mono on Poly presets, recorded) | §5.11 | Re-cut T021 | G19 | T001, T021 |
| **§2.0 amendments** | Six row texts written verbatim, row first, then Clarifications | — | per row | T001 |

### Deviations from the plan made while cutting these tasks (surface them; do not silently revert)

1. **Test TUs are created as placeholders (G5, T007) and registered early (G6, T008); the last group's single CMake task (T030)
   audits every list.** CMake refuses a list that names a missing file, and a test cannot fail before its TU compiles (Phase 12
   tasks, Deviation 1, is the precedent). T030 remains the single registration task of the integration groups.
2. **Library-iterating per-push cases are written in the library group (G22), not in G1.** Plan §11 expects per-push cases "red
   only where the library is empty" during G1; the workflow fails a red gate. Those cases are therefore the failing tests of
   G22 (T024), written immediately before the library is authored (T025) and generated (T026) in the same group. Every per-push
   case written before G22 runs on a **test-local** definition, so it is non-vacuous.
3. **`PresetArmContext` gains `std::string stop;`.** The renderer records an FR-023 refusal there and returns empty taps;
   `processorRenderHook` turns a non-empty `stop` into `FAIL(ctx.stop)`. Per-push refusal tests can then assert the refusal
   text without aborting (plan §4.7 has no such field).
4. **The uidesc insert is one element and uses the Edit tool**, as the project's XSLT rule allows for edits of one or two
   controls (memory rule "XSLT for uidesc changes"); plan §4.6 says "XSLT". If the user wants XSLT anyway, T014 runs the same
   insert through `xslt3`.
5. **P-3, P-4 and P-7 get carrying rows.** Plan §2.0 lists no row for them; the project rule "every clarification needs an
   enforcing FR/SC" requires one. T001 adds P-3 / P-4 to the FR-007 amendment and P-7 to the C-7 / SC-020 amendment.
6. **`Profundum_PresetDistinctnessParams` lives in `preset_sweep_test.cpp`** (plan §8: "same" TU as the sound part).

### Verified API surface these tasks rely on (read this session at `e6095b24`)

| Fact | Source |
|---|---|
| `makeProfundumPresetConfig()` returns `{kProcessorUID, "Profundum", "Synth", {"Sub"}}`; additive-only banner | `plugins/profundum/src/preset/profundum_preset_config.h:4-25` |
| Controller builds `PresetManager(makeProfundumPresetConfig(), nullptr, this)` and sets no provider | `plugins/profundum/src/controller/controller.cpp:100-102` |
| `presetManagerForTest()` | `plugins/profundum/src/controller/controller.h:257` |
| `engine_->prepare(sr, makeProfundumEngineConfig());` | `plugins/profundum/src/processor/processor.cpp:276` |
| `Processor::notify`: if-chain over message IDs; the `kMsgEndpointName` branch returns `kResultOk` without a flush; unknown → `AudioEffect::notify` | `processor.cpp:700-745` |
| Test seams `engineForTest`, `setEngineObserverForTest`, `mutableEngineForTest`, `endpointRejectionsForTest` | `processor.h:107-121` |
| Message IDs `kMsgCopyEndpoint` … `kMsgEndpointName` | `plugins/profundum/src/plugin_ids.h:83-89` |
| `struct ProfundumEngineSeams { … bool anchorPhaseTap = false; }`; `ProfundumEngineConfig::seams`; copied at prepare | `dsp/include/krate/dsp/systems/profundum_engine_observer.h:31-40`; `profundum_engine.h:84-90`, `:150` |
| `kSessionTags` holds 13 tags, base 9000, highest 9300 | `plugins/profundum/src/ui/panel_sub_controller.h:66-86` |
| Header container children; the Glide readout ends at x 436, the page strip starts at x 560 | `plugins/profundum/resources/editor.uidesc:124-134` |
| Phase 9 TU: includes `:46-86`; `using namespace Krate::DSP;` `:88`; anonymous namespace `:90`; `auPt` alias `:92`; `auEvidenceDir` / `auWriteEvidence` `:180-190`; `auReport` `:653-666`; 11 `auShard*` at `:1065, 1117, 1134, 1303, 1426, 1494, 1640, 1800, 1914, 1979, 2129`; first namespace closes `:2264`; `auRunShard` `:2271-2281`; 16 TEST_CASEs `:2285-2409` (13 `[long]`, 3 smoke), all tagged `[profundum]` | `dsp/tests/unit/systems/profundum_engine_audibility_test.cpp` |
| Evidence written by that TU: `audibility-table/*.csv` (12 files), `h17-poly-gr.txt` (from the TEST_CASE at `:2307`), the sweep CSV (`:2205`), `motion-mass-swell*.txt` (`:2403`, `:2414`) | same TU; `specs/profundum-phase9-voice-engine/evidence/audibility-table/` listing |
| `voiceEngineAllocateTaps` `:203`, `VoiceEngineObserver` `:239`, `voiceEngineRender` `:377`, `voiceEngineTwinScript` `:438`, `voiceEngineRenderWithTwin(s, voice = -1)` `:449` | `dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h` |
| `ControllerFixture` `:477`, `ProcessorFixture` `:529`, `ConnectedPair` `:776`, `RecordingComponentHandler` `:400`, `memoryStreamOf` `:1013`, `writeV2Stream` `:1029`, `loadStateBytes` `:1041`, `patchU32` `:1047`, `withEndpoints` `:1065` | `plugins/profundum/tests/profundum_test_fixture.h` |
| `kPpPortTolerance = 0.10`; `ppRenderProcessor` `:576` | `plugins/profundum/tests/param_path_audibility_helpers.h:112` |
| `Krate::Test::ParameterChanges` (header-only) | `tests/test_helpers/vst_param_changes.h:31`, `:78` |
| `PresetManager::loadPreset` `:85`, `savePreset` `:88`, `deletePreset` `:100` | `plugins/shared/src/preset/preset_manager.h` |
| The test list is enumerated; the `-fno-fast-math` block; `processor_cpu_test.cpp` deliberately outside it; `krate_test_pch` last | `plugins/profundum/tests/CMakeLists.txt` (whole file) |
| Vorago generator block (sources, `KrateDSP KratePluginsShared sdk`, include dirs, `bin` output, warning flags, custom target) | root `CMakeLists.txt:678-743` |
| `GENERATORS = { seraphis, vorago }` | `tools/check-preset-generator-determinism.js:54-57` |
| `kSuiteShards` long `{ dsp_systems_tests: 8, vorago_tests: 2, seraphis_tests: 2 }`; `kFilter` | `tools/ci-test-shard.js:106-112` |
| The release skill's inputs and target table omit Profundum | `.claude/skills/release/SKILL.md:15-16`, `:22-31` |
| `## [0.1.0] - Unreleased` … `### Added` | `plugins/profundum/CHANGELOG.md:8-16` |
| `plugins/profundum/docs/` is empty; Vorago's holds `index.html`, `manual-template.html`, `assets/` | directory listings |

---

## Group and task map

| Group | Tasks | Kind | Gate / unblock condition |
|---|---|---|---|
| G1 | T001 | spec rows + census | blocked until `evidence/plan-rulings.md` records every ruling |
| G2 | T002 | move-gate tooling + baseline (main loop) | blocked until `evidence/move-gate/pre/` is complete |
| G3 | T003 | FR-057 move | `dsp_systems_tests` build; the TU's `[profundum]` smoke cases |
| G4 | T004 | move verdict (main loop) | blocked until `MOVE GATE GREEN` |
| G5 | T005 [P], T006 [P], T007 [P] | new headers + placeholder TUs | build |
| G6 | T008 | `tests/CMakeLists.txt` registration | build |
| G7 | T009 | category set | per push |
| G8 | T010 | `PresetHost`, drive, generator source | per push |
| G9 | T011 | root `CMakeLists.txt` generator block | build of `profundum_preset_generator` |
| G10 | T012 | load message + P-11 seam (processor) | per push |
| G11 | T013 | controller providers, browser, load stream | per push |
| G12 | T014 | header button, sub-controller tag, layout | per push |
| G13 | T015 | processor render support (renderer, hook adapter, carriers) | per push |
| G14 | T016 | probe cases + merge tool | per push (keys case) |
| G15 | T017 | probe run (main loop) | blocked until `probe-table.csv` has 102 rows and gates (a)–(c) pass |
| G16 | T018 | showcase arms, descriptor, distance | per push |
| G17 | T019 | sweep, mono, determinism, loudness, distinctness cases | per push (mechanism) |
| G18 | T020 | RT load, render guard, held-note load | per push |
| G19 | T021 | CPU case | hidden |
| G20 | T022 | audition case + script | hidden |
| G21 | T023 | pilot rows 51–53, rules, reference columns, pilot run (main loop) | blocked until `distinctness.csv` has F and both controls pass |
| G22 | T024 → T025 → T026 | library checks → rows 1–50 → tree (main loop) | blocked until 53 files exist; then per push |
| G23 | T027 | `[long]` library lanes (main loop) | blocked until `LIBRARY LANES GREEN` |
| G24 | T028 | MANUAL, docs, manual test | per push |
| G25 | T029 | rosters, CHANGELOG, CI shards, plugin `CLAUDE.md` | per push |
| G26 | T030 | CMake registration audit (single) | build |
| G27 | T031 [P] … T036 [P] | scope fence, suites lane, portability, tidy, CPU, release gate | main loop at close |

---

# GROUP 1 — Rulings

## T001 — Ruling gate, §2.0 row amendments, branch / base / ODR census (no code) — STOP AND SURFACE until ruled

**Files edited:** `specs/profundum-phase13-presets-release/spec.md`. **Files read:**
`specs/profundum-phase13-presets-release/evidence/plan-rulings.md` (written by the **main loop**, never by this task).
**Files created:** `evidence/run-log/T001.txt`.

1. **Census.** `git rev-parse --abbrev-ref HEAD` must print `synth/profundum`; `git merge-base --is-ancestor e6095b24 HEAD` must
   exit 0. Otherwise STOP AND SURFACE (branch safety).
2. **Ruling gate.** If `evidence/plan-rulings.md` is missing, or does not carry a decision for each of the ten plan §13 items
   (P-1, P-2, P-3/P-4, P-5, P-6, P-7, CPU MONO arm, P-10, P-11, §2.0 texts), return **`blocked`** with notes: "main loop: grill
   the user on plan §13 items 1–10; write each decision verbatim to evidence/plan-rulings.md; unblock T001". If any item is
   ruled **other** than the plan's reading, return `blocked` naming the tasks the ruling table above lists for re-cut.
3. **Row first, then Clarifications** (memory rule). For each approved ruling, write the plan §2.0 amended text **verbatim** into
   the named spec rows: FR-057 and SC-029 (P-1 + P-10); FR-012, SC-007, SC-006 (P-2); C-7 "Sound space" and SC-020 (P-5);
   C-9 and FR-007 (P-6); FR-045 and SC-022 (CPU MONO arm); SC-009 (P-11). Additionally (Deviation 5): FR-007 gains "… and the
   plan P-3 / P-4 claim rules: a claim of a single-state column (AT-11, 12, 13, 17, 19–23, 33–40, 43–46) stores Morph Position 0;
   the AT-33 claimant stores SPREAD, spread 1 and Field Motion depth 1"; C-7 "Threshold F" and SC-020 gain "F = max(2.5 · t_max,
   1.0) with the plan §5.8 units (dB ÷ 3, E_sub ÷ 2, D_br ÷ 0.25 oct, t₉₀ log₂ ms ÷ 1, side/mid clamped to [−40, 0] dB)".
   Then append a `### Session 2026-10-24 (plan rulings)` block to Clarifications, one line per ruling, each ending with its
   enforcing ids in brackets.
4. **ODR re-sweep.** For every name in the plan's claimed set (§4.1 lift table L1–L11 and shim: `ProfundumAudibility`,
   `ScriptRenderHook`, `ScriptRenderScope`, `ScriptRenderFn`, `profundum_audibility_readers`; §4.3 `ProfundumPresetDef`,
   `CapabilityInfo`, `kFactoryDefaultSeed`; §4.4 `PresetHost` (expect 1 hit, `VoragoTest`), `writeFactoryTree`; §4.6
   `kMsgLoadPresetState`, `ProfundumLoadPresetState`, `setEngineSeamsForTest`, `engineSeamsForTest_`, `kPresetBrowserTag`,
   `loadPresetStream`, `openPresetBrowser`, `makeProfundumPresetTabLabels`, `presetBrowserView_`; §4.7 `PresetArmContext`,
   `renderScriptThroughProcessor`, `effectiveScriptFor`, `processorRenderHook`, `PresetRenderReadings`, `ShowcaseArm`,
   `PresetSweepObserver`, `presetFloatIsFinite`, `fundamentalLossFlagged`, `presetReadings`, `showcaseColumn`, `describePreset`,
   `presetDistance`; §8 `PresetHostComponentHandler`): `grep -rnw <Name> dsp/ plugins/ tools/ tests/`. Record each count in
   `T001.txt`. Any hit beyond the spec / plan expectation: STOP AND SURFACE.

**Verified by:** the G1 gate (build only; no code changed). The FR-022 ordering of later groups depends on this task.

---

# GROUP 2 — Move-gate baseline

## T002 — Move-gate tooling and the pre-move baseline (FR-058, SC-029; plan §5.1 steps 1–2b) — **main loop**, blocks until the baseline exists

**Files created (all under `specs/profundum-phase13-presets-release/evidence/`):** `move-gate/run-cases.sh`,
`move-gate/compare.js`, `move-check.js`, `run-log/T002.txt`. **No source file is edited by this task, and the Phase 9 TU must not
be edited before the baseline is captured.**

- `move-gate/run-cases.sh <pre|post>`:
  1. Builds `dsp_systems_tests` once with `-- /nr:false`, log to `move-gate/<pre|post>/build.log`; counts
     `warning C[0-9]|warning:` and writes the count to `move-gate/<pre|post>/warnings.txt` (post must be 0).
  2. Launches the **16** TEST_CASEs of `profundum_engine_audibility_test.cpp` (`:2285-2409`, names taken from
     `dsp_systems_tests.exe --list-test-names-only "[voice-engine]"` filtered to the 16 `VoiceEngine_AudibilityTable*`,
     `VoiceEngine_MacroMonotone*`, `VoiceEngine_MotionMassSwell*` names) as **16 concurrent processes**, one per name, each
     to `move-gate/<pre|post>/<case>.log`, with its wall clock appended.
  3. After all exit: `git status --porcelain -- specs/profundum-phase9-voice-engine/evidence/` lists the written files; copy each to
     `move-gate/<pre|post>/evidence/<same relative path>`; then `git checkout -- specs/profundum-phase9-voice-engine/evidence/`.
  4. Prints `RUN <pre|post> DONE cases=16 wall=<s>`.
- `move-check.js` (Node): plan §5.1 step 2 exactly. Reads `git show e6095b24:dsp/tests/unit/systems/profundum_engine_audibility_test.cpp`,
  the new header and the new TU; strips mechanical edits 1–3 (namespace line, `inline ` prefixes, the `auPt` alias line); reads
  `evidence/move-lifts.json` (written by T003: per lift L1–L11 the function name, the source line range at `e6095b24`, the header
  line range); requires the remaining text outside lift regions identical line by line; writes the per-lift source-vs-lifted diff
  to `move-gate/lifts.diff`; fails on any lift region not in the manifest; prints `MOVE PURE changed=1 lifted=11` or
  `MOVE IMPURE <first difference>`. It also runs `git status --porcelain -- dsp/` and requires exactly
  ` M dsp/tests/unit/systems/profundum_engine_audibility_test.cpp` and `?? dsp/tests/unit/systems/profundum_audibility_readers.h`,
  writing the output to `move-gate/paths.txt` (step 2b).
- `move-gate/compare.js`: every `pre/evidence/**` file byte-identical to its `post/evidence/**` twin (same file set); every case
  log's last Catch2 summary line identical (same assertion and case counts); `post/warnings.txt` = 0; `move-check.js` printed
  `MOVE PURE`. Prints `MOVE GATE GREEN` or `MOVE GATE RED <reason>`.

**Executor:** write the three scripts, `bash -n` the shell script, `node --check` the two JS files. Then check whether
`move-gate/pre/` holds 16 case logs and the evidence copies. If not, return **`blocked`** with notes "main loop: run
`bash specs/profundum-phase13-presets-release/evidence/move-gate/run-cases.sh pre` (≈ 5 min build + ≈ 15 min wall, bound by the
Motion case, 884 s at the Phase 9 close; plan §10); quote its last line and every case's wall clock into run-log/T002.txt;
unblock T002". The full `dsp_systems_tests` suite and its other `[long]` cases are **never** run (FR-058).

---

# GROUP 3 — The move

## T003 — FR-057 move with the P-1 shim and the P-10 lifts (FR-021, FR-057; plan §4.1)

**Files:** create `dsp/tests/unit/systems/profundum_audibility_readers.h` and
`specs/profundum-phase13-presets-release/evidence/move-lifts.json`; edit
`dsp/tests/unit/systems/profundum_engine_audibility_test.cpp`. **Nothing else** (no CMake edit; plan §4.1 last paragraph).

**The test.** No test is added: the 16 existing TEST_CASEs are the test (T004 runs them pre vs post). The group gate builds
`dsp_systems_tests` and runs the TU's three per-push smoke cases (`VoiceEngine_AudibilityTableSmoke`,
`VoiceEngine_MacroMonotoneSmoke`, `VoiceEngine_MotionMassSwellSmoke`, all `[profundum]`).

**Implementation (plan §4.1, mechanical edits only):**
1. Move everything inside the first anonymous namespace (`:90` to its close at `:2264`) and the include block (`:46-86`, minus
   `<catch2/catch_test_macros.hpp>`, which the header also needs: keep it in both, because the moved `auReport` / `auWriteEvidence`
   use `CHECK` / `REQUIRE` / `INFO`) into the header, under `#pragma once`.
2. `namespace {` → `namespace Krate::DSP::ProfundumAudibility {`.
3. Prepend `inline` to every namespace-scope non-template function and every namespace-scope `constexpr` variable.
4. `namespace auPt = Krate::DSP::ProfundumTest;` → `namespace auPt = Krate::DSP::ProfundumAudibility::render;`.
5. Above the moved block, the `render` shim **exactly** as plan §4.1 prints it (`ScriptRenderFn`, `ScriptRenderHook`,
   `scriptRenderHook()` with a function-local `thread_local`, the two shadowing render functions with fall-through to
   `ProfundumTest::`, `ScriptRenderScope` RAII, non-copyable).
6. The eleven lifts L1–L11 of the plan §4.1 table, each with the exact signature in that table, placed immediately before the
   function that contained the region; captured variables become parameters; `cells.push_back(c);` → `return c;` where the
   region built one cell; each Phase 9 call site calls the lift with exactly the values the region used (the "Phase 9 call after
   the lift" column). L7: the ampStep render now runs before the two gate renders (fresh engine per render, plan note).
   L10 adds the leading `const MorphState& base` and moves `Row` to namespace scope as `AuNoteMemoryRow`, with
   `auNoteMemoryRows()` returning a function-local `static const std::array<AuNoteMemoryRow, 4>`.
7. The TU keeps: its header comment, `#include <catch2/catch_test_macros.hpp>`, `#include "profundum_audibility_readers.h"`,
   `using namespace Krate::DSP; using namespace Krate::DSP::ProfundumAudibility;` at TU scope, the second anonymous namespace
   (`auRunShard`) and the 16 TEST_CASEs, textually unchanged.
8. `move-lifts.json`: for L1–L11, `{ "id": "L1", "function": "auCellOnsetESub", "source": [1154, 1173], "header": [<from>, <to>] }`
   with the source ranges of the plan table at `e6095b24` (L1 `:1154-1173`, L2 `:1213-1238`, L3 `:1266-1286`, L4 `:1305-1327`,
   L5 `:1365-1385`, L6 `:1436-1457`, L7 `:1470-1489`, L8 `:1498-1521`, L9 `:1528-1549`, L10 `:1964`, `:1981-2002`, `:2005-2030`,
   L11 `:2076-2082`, `:2097-2127`); a lift with two source regions lists both.

Re-read the header: `KRATE_DSP_TESTS_DIR` stays referenced by `auEvidenceDir` (`:180-182`). No logic, constant, bar or string
changes. Record the header's line count and the TU's line count in `evidence/run-log/T003.txt`.

**Verified by:** G3 gate (`dsp_systems_tests` builds with 0 warnings; the three smoke cases pass), then T004.

---

# GROUP 4 — Move verdict

## T004 — Post-move run and verdict (FR-058, SC-029; plan §5.1 steps 2–5) — **main loop**, blocks until GREEN

**Files created:** `evidence/run-log/T004.txt` only.

**Executor:** run `node specs/profundum-phase13-presets-release/evidence/move-check.js` (seconds) and quote its output into
`T004.txt`. If it prints `MOVE IMPURE`, fix T003's header (never the check) and re-run it. Then check whether
`move-gate/post/` holds 16 logs and `compare.js` has printed `MOVE GATE GREEN`. If not, return **`blocked`** with notes "main loop:
run `bash …/move-gate/run-cases.sh post`, then `node …/move-gate/compare.js`; quote both last lines into run-log/T004.txt; on
`MOVE GATE GREEN` unblock T004; on RED, STOP: surface the first differing CSV line (plan R-1 / R-14), never tolerate it".

---

# GROUP 5 — New headers and placeholder TUs (parallel)

## T005 [P] — `tools/profundum_preset_defs.h` (FR-010, FR-011 data; plan §4.3)

**Files created:** `tools/profundum_preset_defs.h` only.

**Implementation.** Exactly the plan §4.3 declarations in `namespace Profundum::PresetDefs`: `ParamSetting`, `CapabilityGroup`
(12 enumerators ending `Reference`), `Capability` (AT01 … AT50, `RSolid`, `RLock`, `RSine`, `Count`), `kNumCapabilities` with
`static_assert(kNumCapabilities == 53, …)`, `CapabilityInfo`, `capabilityTable()` (function-local `static const std::array`),
`kFactoryDefaultSeed = 0xB455u`, `ProfundumPresetDef` (fields in plan order, `seed = kFactoryDefaultSeed`,
`seedDependent = false`), `kCategories` (`Sub, Resonant, Harmonic, Driven, Plucked, Animated`), `allPresets()` returning a
function-local `static const std::vector<ProfundumPresetDef>` that is **empty** (FR-022: no definition before the probe table),
and `buildProfundumInfoXml(name, category, description)` in the shape of `tools/vorago_preset_defs.h:1731-1755`, writing exactly the
seven attributes `MediaType="VstPreset"`, `PlugInName="Profundum"`, `PlugInCategory="Synth"`, `Name`, `MusicalCategory`,
`MusicalInstrument`, `Comment`.
- `capabilityTable()` labels: `"AT-nn <Group> <words>"` with the C-2.1 contrast words (for example `"AT-04 Character Tube"`,
  `"AT-29 Field Solid to Spread"`, `"AT-43 NoteMemory AlwaysRetrigger"`), and `"R-SOLID Reference Solid"`, `"R-LOCK Reference
  Lock"`, `"R-SINE Reference Sine"`; `atId` 1–50 for AT columns, 0 for references; groups per the spec C-2.1 table.
- Includes: `<array> <cstdint> <string> <string_view> <vector>`, `pluginterfaces/vst/vsttypes.h`, and the plugin's
  `plugin_ids.h` only (Vorago rules, `vorago_preset_defs.h:12-21`). Every function `inline`; no state layout.

**Verified by:** T009 / T010 cases (G7/G8) and the build.

## T006 [P] — `plugins/profundum/tests/preset_test_support.h` (Catch2-free decode; plan §4.7 first block)

**Files created:** `plugins/profundum/tests/preset_test_support.h` only.

**Implementation.** Namespace `ProfundumPluginTest`. Exactly the plan §4.7 declarations: `DecodedPresetState` (with `Note`,
`Morph`, `Runtime`, `names`), `decodePresetState(std::span<const std::uint8_t>)`, `decodedEqual(x, y, tol, where)`,
`presetFloatIsFinite(float)` (bit pattern), `fundamentalLossFlagged(const DecodedPresetState&)`, `SweepTimeline`,
`makeTimeline(fs, tailSamples)`.
- `decodePresetState`: `ok = false` unless the span is ≥ 4 bytes; reads version (int32 LE) and `bytes = span.size()`; decodes the
  global block (gain f32, mode i32, polyphony i32, ceiling f32; `global_params.h:182-187`), macros, the note block (priority,
  legato, porta ms f32, porta mode, glide mode, memory, retrigger, bend f32; `note_params.h:360-369`), the morph block (position
  f32, ramp f32, anchor mode i32, edit target i32; `morph_params.h:301-306`), seed at `kStateSeedBytes` (84–87), endpoints A / B at
  `kStateEndpointAOffset` (88) / `kStateEndpointBOffset` (289) through `deserializeMorphState` (`morph_state.h:574`), the runtime
  block at `kStateRuntimeOffset` (490) with the `kRt*Offset` fields (`plugin_ids.h:70-77`), and the two names at
  `kStateNamesOffset` (510; a length byte + bytes, `plugin_ids.h:55-57`). Every offset comes from `plugin_ids.h` constants, never a
  literal. `ok = true` only when version == `kCurrentStateVersion` and bytes == `kStateV3Bytes`.
- `decodedEqual`: ints / enums / names exact; floats `|x − y| ≤ tol · max(|y|, 1)`; MorphState leaf by leaf (`leafBitsOf`, float
  leaves with tol, others exact); first difference written to `where` as `"<field>: <x> vs <y>"`.
- `fundamentalLossFlagged`: an **active** endpoint (A at p = 0, B at p = 1, both for 0 < p < 1; plan §5.6) stores algorithm Rectify,
  Crush or Fracture at Amount ≥ 0.9 with Anchor Level < 0.25.
- `makeTimeline(fs, tail)`: `{fs, 512, llround(3.0 · fs), llround(3.0 · fs) + tail}` (C-6).
- Includes only the plugin's `plugin_ids.h`, the parameter pack headers it needs for field order, `<krate/dsp/systems/morph_state.h>`,
  STL. **No Catch2, no fixture.**

**Verified by:** T010 cases (G8).

## T007 [P] — Placeholder test TUs (Deviation 1)

**Files created** (each: a banner naming its owning task and the SC rows it will carry, `#include <catch2/catch_test_macros.hpp>`,
no TEST_CASE yet):
`plugins/profundum/tests/unit/preset/preset_library_test.cpp` (T009, T010, T024),
`plugins/profundum/tests/unit/manual_coverage_test.cpp` (T028),
`plugins/profundum/tests/integration/preset_browser_test.cpp` (T012, T013, T024),
`plugins/profundum/tests/integration/preset_rt_test.cpp` (T020),
`plugins/profundum/tests/integration/preset_probe_test.cpp` (T012, T015, T016),
`plugins/profundum/tests/integration/preset_showcase_test.cpp` (T018, T023),
`plugins/profundum/tests/integration/preset_sweep_test.cpp` (T019, T022, T024).

**Verified by:** G6 build.

---

# GROUP 6 — Test registration

## T008 — Register the seven TUs and the compile settings (plan §9)

**File edited:** `plugins/profundum/tests/CMakeLists.txt` only.
- Under a new `# Phase 13` comment in `add_executable(profundum_tests …)`, after the Phase 12 block: the seven T007 TUs.
- The same seven in the `-fno-fast-math -fno-finite-math-only` block under `# Phase 13` (bit-pattern finiteness).
  `integration/processor_cpu_test.cpp` stays outside the block.
- `target_include_directories(profundum_tests PRIVATE …)` += `${CMAKE_SOURCE_DIR}/tools` and `${CMAKE_SOURCE_DIR}/tests/test_helpers`.
- `target_compile_definitions(profundum_tests PRIVATE …)` += `KRATE_DSP_TESTS_DIR="${CMAKE_SOURCE_DIR}/dsp/tests"` and
  `PROFUNDUM_PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../resources/presets"`. (The first is what `auEvidenceDir` needs once the
  moved header is included; plugin TUs never call `auReport`.)
- `krate_test_pch(profundum_tests)` stays last.

**Verified by:** G6 gate (build, 0 warnings; the existing `[profundum]` per-push set stays green).

---

# GROUP 7 — Category set

## T009 — Six categories and tab labels (FR-001; C-1; plan §4.2)

**Files edited:** `plugins/profundum/src/preset/profundum_preset_config.h`,
`plugins/profundum/tests/unit/preset/preset_library_test.cpp`.

**Failing tests first** (`[profundum][preset]`):
- `Profundum_PresetCategoryConfig`: `makeProfundumPresetConfig().subcategoryNames` equals
  `{"Sub","Resonant","Harmonic","Driven","Plucked","Animated"}` element for element (size 6, `[0] == "Sub"`);
  `makeProfundumPresetTabLabels()` equals `{"All","Sub","Resonant","Harmonic","Driven","Plucked","Animated"}`; the config's
  `pluginName == "Profundum"`, `pluginCategoryDesc == "Synth"`; `subcategoryNames` equals `PresetDefs::kCategories` in order.
- `Profundum_PresetCategoryNameRule` (C-1, FR-001): build the forbidden set from a heap `ControllerFixture`
  (`profundum_test_fixture.h:477`): for each of the 84 registered IDs, `getParameterInfo` title, every list display string
  (`getParamStringByValue` at each step of a stepped parameter) and the title's leading word; plus the 11 group names
  `Core, Character, Anchor, Pitch, Envelope, Mass, Field, Motion, Morph, NoteMemory, Macros`. A category name fails when it equals
  (case-insensitive) a title, a list string, a group name, or — unless it is one of the continuum words `Sub, Resonant, Harmonic,
  Distorted` — a leading word. Assert: all six names pass. **Non-vacuity:** `Body`, `Wide`, `Drift`, `Mass`, `Drop` each fail the
  same predicate; `Harmonic` passes although "Harmonic Fold" leads with it.

**Implementation.** `subcategoryNames = {"Sub","Resonant","Harmonic","Driven","Plucked","Animated"}` (Q1; keep the additive-only
banner and add "Phase 13 (Q1): six names, permanent once shipped"); add `[[nodiscard]] inline std::vector<std::string>
makeProfundumPresetTabLabels()` returning `"All"` then the subcategories (copied from `vorago_preset_config.h:40-48`).

**Verified by:** `profundum_tests` `"Profundum_PresetCategory*"`.

---

# GROUP 8 — Host, drive, generator source

## T010 — `PresetHost`, `buildPresetComponentState`, `writeFactoryTree`, generator `main` (FR-024, FR-027, FR-047, FR-049; C-4; plan §4.4, §4.5, P-8)

**Files:** create `plugins/profundum/tests/profundum_preset_host.h` and `tools/profundum_preset_generator.cpp`; edit
`plugins/profundum/tests/unit/preset/preset_library_test.cpp`.

**Failing tests first** (`[profundum][preset]`; all definitions here are **test-local** `ProfundumPresetDef` values built in the
TU, never added to `allPresets()`):
- `Profundum_PresetHostDrive`: def `T1` = endpointA `{kMassId → 0.80}` plus one Character Amount E-parameter → 0.5, endpointB
  `{kMassId → 0.20}`, controls `{kMorphPositionId → 0.0, 100 → 0.6}`, names `"Deep Floor"` / `"Bright Edge"`, seed `0x00001234`.
  `buildPresetComponentState(T1, comp, why)` returns true; `decodePresetState(comp)`: `ok`, version 3, 574 bytes;
  `morph.editTarget == 0`; `seed == 0x1234`; `names == {"Deep Floor","Bright Edge"}`; `a.mass` within 2.5e-4·100 of the plain
  value of 0.80 through the registered law and `b.mass` of 0.20; `macros[0]` within 2.5e-4 of 0.6; `runtime.target` and
  `runtime.position` within 2.5e-4 of `morph.position` (= 0) (FR-047). Def `T2` = T1 with `endpointB` empty: decoded `b`
  equals decoded `a` leaf for leaf. Def `T3` = T1 with no seed: bytes 84–87 decode to `kDefaultSeed` (0xB455).
  **Seed step non-vacuity:** without step 6 the seed would read 0xB455; T1 asserts 0x1234.
- `Profundum_PresetHostValidation`: each of these defs returns false with `why` containing the preset name and the point:
  a normalized value built from bits `0x7FC00000` (NaN, via `volatile`); a value 1.5; a Global ID (3) in `controls`; a Sound ID in
  `controls`; ID 0, 2, 3, 417, 4–7 or 1001 in `controls` (one def each); a non-Sound ID in `endpointA`; a duplicated ID in one list;
  name A `"A"`; name B `"B"`; a 32-byte name; a name that `sanitizeEndpointName` changes.
- `Profundum_GeneratorSmoke` (mechanism part): `writeFactoryTree(temp, written, why)` into a fresh
  `std::filesystem::temp_directory_path() / "profundum-gen-smoke-<pid>"` returns true; `written == allPresets().size()`; the six
  `kCategories` directories exist; a second call into a second temp dir gives a byte-identical file set (`memcmp`, same
  process); both temp dirs are removed by an RAII guard. (T024 adds `written == 53`.)

**Implementation (plan §4.4 / §4.5 exactly):**
- `PresetHost` copied in shape from `plugins/vorago/tests/vorago_preset_host.h` (heap `::Profundum::Processor`, buffers sized
  once at `prepare`), with the plan §4.4 public API: `prepare`, `processParamsOnly` (numOutputs 0, P-8 — never render audio before
  `getState`), `process`, `sendEndpointName` (`HostMessage` with `kMsgEndpointName`, int `endpoint`, binary `name`), `loadState`,
  `saveState`, `outL`, `outR`, `processor()`. `static_assert(PresetDefs::kFactoryDefaultSeed == Krate::DSP::ProfundumEngine::kDefaultSeed)`.
- `buildPresetComponentState`: validation list of plan §4.4, then drive steps 1–7 of plan §4.4 (Edit Target A block, Edit Target B
  block with `endpointA` copied when `endpointB` is empty, controls + `1001 → 0.0`, names, `saveState(s1)`, patch bytes 84–87 LE
  with `def.seed`, `loadState(s1)`, `saveState(comp)`, `decodedEqual(decode(comp), decode(s1), 0.0, where)` bitwise or false).
  Uses `Krate::Test::ParameterChanges` (`vst_param_changes.h:78`). Includes `preset_test_support.h`, `profundum_preset_defs.h`;
  **never `profundum_test_fixture.h` or Catch2**.
- `writeFactoryTree`: creates the six directories in `kCategories` order; writes `<category>/<name>.vstpreset` in definition
  order with the Vorago `writeVstPreset` layout (`tools/vorago_preset_generator.cpp`: 48-byte header, `Comp`, `Info` from
  `buildProfundumInfoXml`, a List with 2 entries) and class ID `kProcessorUID.toString`; no timestamp, path or RNG in any byte.
- `tools/profundum_preset_generator.cpp`: `void* moduleHandle = nullptr;`; `main` = `outputBase = argc > 1 ? argv[1] :
  "plugins/profundum/resources/presets"`, `writeFactoryTree`, print `wrote <n> presets`, exit 0 only without failure (print `why`
  to stderr otherwise).

**Verified by:** `profundum_tests "Profundum_PresetHost*" "Profundum_GeneratorSmoke"`.

---

# GROUP 9 — Generator target

## T011 — Root `CMakeLists.txt`: `profundum_preset_generator` and `generate_profundum_presets` (FR-024 – FR-026; plan §4.5)

**File edited:** root `CMakeLists.txt` only, a new block immediately after the Vorago block (after `:743`).

**Test.** SC-010's build arm: the G9 gate builds `profundum_preset_generator` with 0 warnings. The executor also adds to
`evidence/run-log/T011.txt` the grep the main loop uses at close: the generator's link line (`build.log`, or the `.vcxproj`'s
`AdditionalDependencies`) contains no `vstgui` and no `Catch2`.

**Implementation.** The plan §4.5 CMake text: sources `tools/profundum_preset_generator.cpp`,
`plugins/profundum/src/processor/processor.cpp`, `plugins/profundum/src/processor/goertzel_bank_simd.cpp` (spec finding 3), the
five SDK sources of the Vorago block, `plugins/profundum/tests/vstgui_test_stubs.cpp`; `target_link_libraries(… PRIVATE KrateDSP
hwy KratePluginsShared sdk)` (no `vstgui_support`, no Catch2); include dirs `plugins/profundum/src`, `plugins/profundum/tests`,
`tests/test_helpers` (header-only use; never the `test_helpers` target, which links Catch2 INTERFACE — the Vorago comment at
`CMakeLists.txt:704-705`), `tools`, `${vst3sdk_SOURCE_DIR}`; `cxx_std_20`; `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"`;
the Vorago warning flags verbatim (`:716-732`); `add_custom_target(generate_profundum_presets COMMAND profundum_preset_generator
"${CMAKE_SOURCE_DIR}/plugins/profundum/resources/presets" DEPENDS profundum_preset_generator COMMENT "Generating Profundum factory
presets (53 presets across 6 categories)" VERBATIM)`. A banner comment copies the Vorago one about the fixed `release.yml`
contract.

**Verified by:** G9 gate: `"$CMAKE" --build … --target profundum_preset_generator` with 0 warnings.

---

# GROUP 10 — Processor: load message and the P-11 seam

## T012 — `kMsgLoadPresetState`, `notify` branch, `setEngineSeamsForTest` (FR-031 step 2, FR-032; plan §4.6, P-11)

**Files edited:** `plugins/profundum/src/plugin_ids.h`, `plugins/profundum/src/processor/processor.h`,
`plugins/profundum/src/processor/processor.cpp`, `plugins/profundum/tests/integration/preset_browser_test.cpp`,
`plugins/profundum/tests/integration/preset_probe_test.cpp`.

**Failing tests first:**
- `Profundum_LoadPresetStateMessage` (`[profundum][browser]`, in `preset_browser_test.cpp`): a heap processor (`ProcessorFixture`),
  prepared at 48 kHz / 512. Build `comp` = `withEndpoints(writeV2Stream(fresh), X, Y)` with two distinct valid MorphStates, seed
  patched to `0x0BADF00D`, names patched to `"Load A"` / `"Load B"`. Send a `HostMessage` with ID `kMsgLoadPresetState` and binary
  `"state"` = `comp` to `notify`: returns `kResultOk`; decode `getState` equals decode of a second fresh processor's
  `setState(comp)` → `getState`, through `decodedEqual(…, 0.0, where)`, with the seed, both names and the runtime block checked by
  name. **Refusals** (each returns `kResultFalse` or `kInvalidArgument` and leaves `getState` byte-equal to before): no `"state"`
  attribute; size 0; size 4097. A `kMsgEndpointName` message still works afterwards (no regression of `:723-739`).
- `Profundum_EngineSeamsForTest` (`[profundum][probe]`, in `preset_probe_test.cpp`): a processor with a `VoiceEngineObserver`
  attached (`setEngineObserverForTest`), taps allocated for 1 tapped voice, E1 note-on at 0, 1 s rendered at block 512.
  (a) Default (no seam call): over samples [24000, 25024) of `voices[0].anchorPhase`, the count of n with `ph[n] != ph[n−1]` is
  ≤ 1024 / 16 (staircase). (b) After `setEngineSeamsForTest({.anchorPhaseTap = true})` and a re-`setupProcessing` / `setActive`:
  the count is ≥ 1000. (c) After `setEngineSeamsForTest({})` and re-prepare: back to ≤ 64. The case prints the three counts.

**Implementation:**
- `plugin_ids.h`, beside `:84-89`: `inline constexpr const char* kMsgLoadPresetState = "ProfundumLoadPresetState";   // ctl -> proc,
  binary "state" (<= 4096 B), Phase 13 FR-031`.
- `processor.h`, in the FR-067 seam block (`:116-121`): the P-11 setter and the private member `engineSeamsForTest_{}`, verbatim
  from plan §2 P-11.
- `processor.cpp:276`: `Krate::DSP::ProfundumEngineConfig cfg = makeProfundumEngineConfig(); cfg.seams = engineSeamsForTest_;
  engine_->prepare(sr, cfg);` (shipping behaviour unchanged with the default member).
- `processor.cpp` `notify`, a new `else if (FIDStringsEqual(id, kMsgLoadPresetState))` branch before the final `else`: read
  `getBinary("state", data, size)`; refuse when `data == nullptr || size == 0 || size > 4096`; otherwise a `MemoryStream`, write,
  seek 0, `return setState(stream);` (plan §4.6 "Processor"; `setState` already flushes and does the restore hand-off).

**Verified by:** `profundum_tests "Profundum_LoadPresetStateMessage" "Profundum_EngineSeamsForTest"`, the whole `[profundum]`
per-push set (processor.cpp changed).

---

# GROUP 11 — Controller: providers, browser view, load stream

## T013 — State / load providers, `loadPresetStream`, browser lifecycle, `createCustomView` (FR-030, FR-031, FR-032, FR-059; plan §4.6)

**Files edited:** `plugins/profundum/src/controller/controller.h`, `plugins/profundum/src/controller/controller.cpp`,
`plugins/profundum/tests/integration/preset_browser_test.cpp`.

**Failing tests first** (`[profundum][browser]`):
- `Profundum_ControllerLoadPresetStream`: `ConnectedPair` (`profundum_test_fixture.h:776`) with a `RecordingComponentHandler`.
  `comp` as in T012 (seed `0x0BADF00D`, names `"Load A"` / `"Load B"`, Mass on endpoint A at a non-default value).
  `controller->loadPresetStream(memoryStreamOf(comp))` returns true; the pair's controller → processor recorder counts exactly one
  `kMsgLoadPresetState`; decode `proc->getState` equals decode of a fresh `setState(comp)` (0 mismatches; seed, both names and
  runtime block by name); `restartFlags` contains `kParamValuesChanged`; the controller's normalized value of `kMassId` equals the
  processor's mirrored value. **Refusals:** a 4097-byte stream → false and 0 messages; a stream whose first int32 is
  `kCurrentStateVersion + 1` → false and 0 messages; with no host context (`allocateMessage()` returns nullptr, a bare controller
  without `initialize(hostContext)`) → false and the controller's parameters unchanged.
- `Profundum_PresetBrowserSave` (FR-031 state provider; plan §8 row): a `ConnectedPair` whose component handler is the TU-local
  `PresetHostComponentHandler` (anonymous namespace; an `IComponentHandler` whose `queryInterface(IComponent::iid)` returns
  `fx.proc` with `addRef`). Load `comp` through `loadPresetStream`, then `presetManagerForTest()->savePreset("Profundum Save Probe
  <pid>", "Sub")` returns true; the written file's `Comp` decodes as version 3, 574 bytes, and `decodedEqual(…, 0.0)` against
  decode of `fx.proc->getState`. An RAII guard deletes the file through `deletePreset`. **Non-vacuity:** with a plain
  `RecordingComponentHandler`, `savePreset` returns false.
- `Profundum_PresetBrowserViewLifecycle`: `createCustomView("PresetBrowserButton", …)` returns a non-null
  `Krate::Plugins::OutlineBrowserButton`; the tab labels the controller passes equal `makeProfundumPresetTabLabels()` (7 entries).
  The existing `Profundum_EditorLifecycle` (`unit/controller/editor_lifecycle_test.cpp:110`) must stay green (open / close with
  the overlay added in `didOpen`, removed in `willClose`).

**Implementation (plan §4.6 "Controller", "View factory"):** after `controller.cpp:101-102`, `setStateProvider` (body of Vorago
`controller.cpp:153-168`, `createComponentStateStream()`) and `setLoadProvider([this](IBStream* s, const PresetInfo&) { return
loadPresetStream(s); })`. Public `bool loadPresetStream(Steinberg::IBStream*)`: read the chunk into `std::vector<std::byte>`,
refuse > 4096 B or version > `kCurrentStateVersion`; send `kMsgLoadPresetState` with `setBinary("state", …)` (false when
`allocateMessage()` is null); `setComponentState` on a `MemoryStream` of the same bytes; `componentHandler->restartComponent(
kParamValuesChanged)`; return true iff `setComponentState` returned `kResultOk`. Member `Krate::Plugins::PresetBrowserView*
presetBrowserView_ = nullptr;`. `didOpen`: create `PresetBrowserView(frame->getViewSize(), presetManager_.get(),
makeProfundumPresetTabLabels())` once and `frame->addView` it. `willClose`: `close()` it if open, null the pointer (the frame owns
it). Public `void openPresetBrowser()`. `createCustomView`: `if (std::strcmp(name, "PresetBrowserButton") == 0) return new
Krate::Plugins::OutlineBrowserButton(viewRect, nullptr, -1, "PRESETS");`. No current-preset name, no prev / next, no new state
(FR-059).

**Verified by:** `profundum_tests "Profundum_ControllerLoadPresetStream" "Profundum_PresetBrowser*" "Profundum_EditorLifecycle"`.

---

# GROUP 12 — Header button, sub-controller tag, layout

**Re-cut 2026-10-24 (main loop, option A, the Vorago pattern).** `didOpen` builds the `PresetBrowserView` overlay, as the plan says. The overlay carries tagged child controls (Save 1, Search 2, Import 3, Delete 4, Close 5, 10–31; preset_browser_view.h:35–40). Every Profundum test tree walker therefore stops descending at a `PresetBrowserView`, as `plugins/vorago/tests/unit/controller/editor_lifecycle_test.cpp:78–110` does. Before it recurses, each walker adds `if (dynamic_cast<Krate::Plugins::PresetBrowserView*>(view) != nullptr) continue;` with a one-line comment. **Additional files edited by T013:**
- `plugins/profundum/tests/unit/controller/editor_lifecycle_test.cpp` (`collectBoundControls`);
- `plugins/profundum/tests/ui_tree_helpers.h` (`collectBoundControls` and `detail::collectViewsOfTypeInto`);
- `plugins/profundum/tests/unit/controller/editor_layout_test.cpp` (`walkBuilt`);
- `plugins/profundum/tests/unit/controller/macro_ui_test.cpp` (the text walk near :866, only if it walks the frame).
Teeth: `Profundum_PresetBrowserViewLifecycle` asserts that the overlay exists after `didOpen`, and that the walker over the same frame returns no tag in 1–5 or 10–31. Profundum_EditorLifecycle, Profundum_UidescBindsEverySurfaceId and the walkBuilt cases stay green with their counts unchanged (80 surface tags).

## T014 — `kPresetBrowserTag`, `verifyView` / `valueChanged`, the uidesc element, `Profundum_UiLayout` (FR-030, FR-059; SC-013 layout arm; plan §4.6 "Sub-controller", "uidesc")

**Files edited:** `plugins/profundum/src/ui/panel_sub_controller.h`, `plugins/profundum/src/ui/panel_sub_controller.cpp`,
`plugins/profundum/resources/editor.uidesc`, `plugins/profundum/tests/unit/controller/editor_layout_test.cpp`.

**Failing tests first** (extend `Profundum_UiLayout`, `editor_layout_test.cpp:950`, tags unchanged `[profundum][ui]`, plus a new
section): at each zoom step 100 / 125 / 150 / 200 %, the `header` container holds **exactly one** `OutlineBrowserButton`, at
origin (452, 24) size (96, 24) scaled by the step; it overlaps no other header child's rect (in particular the Glide readout
ending at x 436 and the page strip starting at x 560); every other ratified header child keeps its rect (compare against the
rects read from the uidesc before the insert, hard-coded in the test from `editor.uidesc:125-133`); no `CTextLabel` /
`CParamDisplay` bound to a preset name and no previous / next control exists in the header; `Profundum::UI::kSessionTags.size()
== 14` and contains 9400; clicking the button (`valueChanged` with the tag) calls `openPresetBrowser` (observe
`PresetBrowserView::isOpen()` true after a frame open). `Profundum_UidescTagIntegrity` (`:521`) stays green with the new session
tag.

**Implementation:** `inline constexpr std::int32_t kPresetBrowserTag = 9400;` appended after `kZoomTag`; `kSessionTags` →
`std::array<std::int32_t, 14>` with `kPresetBrowserTag` last; `verifyView` gives the `custom-view-name="PresetBrowserButton"`
control that tag and the sub-controller as listener (Vorago `panel_sub_controller.cpp:89-95`); `valueChanged` calls
`owner_->openPresetBrowser()` for it (Vorago `:145-148`). uidesc (Edit tool, Deviation 4): one new last child of the `header`
container: `<view class="CView" custom-view-name="PresetBrowserButton" origin="452, 24" size="96, 24" tooltip="Browse and load
factory and user presets."/>`. No other uidesc change.

**Verified by:** `profundum_tests "[ui]" "Profundum_EditorLifecycle"`.

---

# GROUP 13 — Processor render support

## T015 — `preset_render_support.h`: `effectiveScriptFor`, `renderScriptThroughProcessor`, `processorRenderHook` (FR-021, C-5 twin rule; plan §5.2, §5.3, P-1, P-6, P-11)

**Files:** create `plugins/profundum/tests/preset_render_support.h`; edit `plugins/profundum/tests/integration/preset_probe_test.cpp`.

**Failing tests first** (`[profundum][probe]`):
- `Profundum_ScriptRenderShim`: with no hook, `auRd::render::scriptRenderHook().fn == nullptr` and
  `auRd::render::voiceEngineRender(s)` equals `Krate::DSP::ProfundumTest::voiceEngineRender(s)` per sample (same binary, `memcmp`
  of `outL` / `outR`) for a 0.25 s E1 script; inside `ScriptRenderScope(counterFn, &n)` a call increments `n` to 1 and returns the
  counter's taps; after the scope ends the hook is restored (nested scopes restore in LIFO order).
- `Profundum_ProcessorScriptRender` (probe context): script = 1.5 s, 48 kHz, host block 512, E1 on at 0, `a = b =` the moved
  `auRd::auStateAs()`, position 0, neutral macros, Mono. Processor render vs engine-direct `ProfundumTest::voiceEngineRender`:
  `compareFingerprints(fingerprintRender(proc.sumL), fingerprintRender(direct.sumL), kMetricTolerance, kSampleTolerance)` passes
  for L and R; `endpointRejectionsForTest() == 0`. A script with a `MorphPosition` event at sample 24000 (not a multiple of 512)
  places the point on its sample (block split; the fingerprint comparison passes).
- `Profundum_ScriptRenderRefusals`: each sets `ctx.stop` and returns empty taps — a `PitchBend` event (stop contains
  `"no processor carrier"`); `config.placement` ≠ default (`"no processor carrier: config"`); `config.seams.forceLinearCore = true`
  on the processor path (`"seam"`); preset context with empty `allowedEndpoints` and with endpoints not in it (`"hard-codes its
  base"`); a decoded state with Output Gain ≠ 1.0, ceiling ≠ −1 dBTP, bend ≠ 2 st or Edit Target B (P-6; each `"pin"`). The seam
  arm: a script with `anchorPhaseTap` increments `ctx.anchorPhaseTapRenders` by 1 and leaves the processor's seams default after.
- `Profundum_EffectiveScriptCarriers` (preset context): decoded macros `{0.1, 0.2, 0.3, 0.4}` replace the script's macros unless
  `macrosFromScript`; decoded `voiceMode`, `polyphony` (4) and `seed` replace the script's; the seven note-behaviour fields and
  1000 / 1002 become sample-0 events **before** `s.events` (a script event at sample 0 of the same kind wins after the stable sort:
  assert the final order and that the script's `PortamentoTimeMs` value is the one applied).

**Implementation** (plan §4.7 second block, §5.2, §5.3 steps 1–7): Catch2 header; includes the fixture,
`profundum_audibility_readers.h`, `band_level_metric.h`, `render_fingerprint.h`, `low_frequency_metrics.h`,
`preset_test_support.h`, `profundum_preset_defs.h`; namespace aliases `auRd`, `auPt` only. `PresetArmContext` as plan §4.7 plus
`std::string stop;` (Deviation 3). `renderScriptThroughProcessor`: steps 1, 1a, 2 (probe: `writeV2Stream(fresh)`; preset:
`ctx.comp`; `withEndpoints`, seed patch, `loadStateBytes`), 3 (one parameter-only block with 105, 100–103, 1, 2 = (polyphony−1)/7,
the sample-0 carriers through their laws, 1001 = 0), 4 (config rows; `setEngineSeamsForTest(e.config.seams)`; observer;
`prepare`; `enableFTZDAZ()`; seams reset after), 5 (events: NoteOn/Off as host events, carrier kinds as parameter points, blocks
split at every carrier sample, other kinds refused), 6 (capture, `REQUIRE(endpointRejectionsForTest() == 0)`, copy Sum to
`ctx.lastSumL/R`, detach after `setActive(false)`), 7 (twin engine-direct via `voiceEngineTwinScript` / `voiceEngineRenderWithTwin`;
preset-context fidelity arm appended to `ctx.fidelity`). `processorRenderHook` casts `ctx`, calls the renderer, `FAIL(ctx.stop)`
when non-empty.

**Verified by:** `profundum_tests "Profundum_ScriptRender*" "Profundum_ProcessorScriptRender" "Profundum_EffectiveScriptCarriers"`.

---

# GROUP 14 — Probe cases

## T016 — `Profundum_PresetAudibilityProbe_<Group>` ×11 and the merge tool (FR-020 – FR-022; SC-009; plan §5.7)

**Files:** edit `plugins/profundum/tests/integration/preset_probe_test.cpp`; create
`specs/profundum-phase13-presets-release/evidence/merge-csv.js`.

**Failing test first (per push):** `Profundum_PresetAudibilityProbeKeys` (`[profundum][probe]`): parse
`specs/profundum-phase9-voice-engine/evidence/audibility-table.csv` (path from `KRATE_DSP_TESTS_DIR "/../../specs/…"`, quoted-CSV
parser); the `gated` cells number exactly **102**; they cover exactly the 50 ids AT-01 … AT-50; AT-32 has exactly 1 gated cell
(E1); AT-13 has exactly 4 (Fracture and Rectify × E1 and C3); AT-26 has exactly 3; every other id has exactly 2 (E1 and C3). The
per-shard key counts equal plan §1 (Anchor 20, Character 12, Core 6, Envelope 4, Field 9, Macros 8, Mass 7, Morph 4, Motion 14,
NoteMemory 8, Pitch 10).

**The hidden cases:** eleven `TEST_CASE("Profundum_PresetAudibilityProbe_<Group>", "[profundum][probe][.probe]")`, Group ∈ {Core,
Character, Anchor, Pitch, Envelope, Mass, Field, Motion, Morph, NoteMemory, Macros}, each per plan §5.7 steps 1–5: for each note in
`auRd::kAuNotes`, (1) under `auRd::render::ScriptRenderScope(processorRenderHook, &probeCtx)` (probe context) call the moved
`auRd::auShard<Group>(cells, note)` unchanged; (2) the same call with no hook (live engine-direct); (4) join on the CSV's gated keys
(row, note, base text) and write `evidence/probe-table/<Group>.csv` with columns `at_id, variant, note, base, processor_value,
bar, pass, live_value, csv_value, rel_dev, anchor_phase_tap_renders`; (5) gates: (a) every gated processor cell passes; (b)
`rel_dev = |live − csv| / max(|csv|, 1e-12) ≤ kPpPortTolerance` (0.10); (c) Anchor group only: `probeCtx.anchorPhaseTapRenders`
increases by exactly 4 per note across the AT-13 cells. A miss in (a) prints `FR-023 STOP <at_id> <note> <value> <bar>`. Each case
asserts its expected key count (the numbers above).
`merge-csv.js <dir> <out> <expectedRows>`: concatenates the per-group CSVs under one header and exits non-zero unless the row
count equals `expectedRows` (102 for the probe; reused by T018 for `coverage-matrix.csv`).

**Verified by:** `profundum_tests "Profundum_PresetAudibilityProbeKeys"` (per push); the hidden cases run in T017.

---

# GROUP 15 — Probe run

## T017 — Run the probe and record `probe-table.csv` (FR-020, FR-022, FR-023; SC-009) — **main loop**, blocks until recorded

**Files created:** `evidence/probe/run-probe.sh`, `evidence/run-log/T017.txt`.

`run-probe.sh`: build `profundum_tests` with `-- /nr:false` (0 warnings), then launch the 11 hidden cases as **11 concurrent
processes** (`profundum_tests.exe "Profundum_PresetAudibilityProbe_<Group>"`), each to `evidence/probe/<Group>.log` with wall
clock; then `node evidence/merge-csv.js evidence/probe-table evidence/probe-table.csv 102`;
`git checkout -- specs/profundum-phase9-voice-engine/evidence/`; print `PROBE GREEN rows=102` when all 11 exit 0 and the merge
passes, else `PROBE RED` with the first failing line. Expected cost: Motion ≈ 30 min wall, the others < 10 min (plan §10).

**Executor:** write the script, `bash -n` it; if `evidence/probe-table.csv` does not exist with 102 data rows and `T017.txt` does
not quote `PROBE GREEN`, return **`blocked`** with notes "main loop: run `bash …/evidence/probe/run-probe.sh`; quote each group's
wall clock and the merge line into run-log/T017.txt; on PROBE GREEN unblock T017 (FR-022: the probe table is recorded before any
ProfundumPresetDef exists; the main loop notes its commit hash for SC-009); on PROBE RED, FR-023 STOP: surface the column, note
and value; no preset is built on it until the user rules".

---

# GROUP 16 — Showcase arms, descriptor, distance

## T018 — `showcaseColumn`, `describePreset`, `presetDistance`, showcase shards (FR-012 – FR-014, FR-043; C-5, C-7; plan §5.4, §5.8, P-2 – P-5, P-7, P-10)

**Files edited:** `plugins/profundum/tests/preset_render_support.h`, `plugins/profundum/tests/integration/preset_showcase_test.cpp`.

**Failing tests first:**
- `Profundum_ShowcaseArmMechanics` (`[profundum][showcase]`, per push): a test-local preset P whose endpoint A = B =
  `auRd::auStateAs()` with Anchor level 1, Sine, LOCK, p = 0, serialised with `withEndpoints(writeV2Stream(fresh), …)` (P-6 pins
  hold by default). `showcaseColumn(decode(P), P, Capability::AT10, true)` returns exactly 2 arms (E1, C3) with `descriptor ==
  "E_sub"`, `bar == 2.0`, both `pass` (Phase 9 bar ≥ 2 dB), `overrides.empty()`. **Endpoint guard non-vacuity:** the same call
  with an arm whose declared `allowedEndpoints` omits A(P) makes the hook `FAIL` (checked through `renderScriptThroughProcessor`
  with `ctx.stop` containing `"hard-codes its base"`). **Primary override:** a P storing Image Mode SOLID claiming AT-31 (needs
  SPREAD) as a **primary** yields arms with non-empty `overrides` and `pass == false`; as a **secondary** the same arms carry
  `overrides == "kFieldImageModeId=Spread"` and are measured.
- `Profundum_PresetDistanceMath` (`[profundum][distinct]`, per push, pure): two constructed `PresetDescriptor` pairs; component
  units 3 dB (0–9, 10, 14), 0.25 oct (11), 2 dB (12), 1 (13); side/mid clamped to [−40, 0] before scaling; a component with
  `valid == false` in either preset is masked and counted (`masked` = 3 for the constructed case); distance = Euclid over unmasked
  components of both notes; assert values to 1e-12 against a hand computation in the test. `F = max(2.5 · t_max, 1.0)`: t_max 0.1
  → F 1.0; t_max 0.8 → F 2.0.
- `Profundum_PresetShowcase_<k>of8` for k = 0…7 (`[profundum][showcase][long][profundum-showcase]`): `REQUIRE(!allPresets().empty())`;
  for each preset index i with `i % 8 == k`, build `comp` through `buildPresetComponentState`, then `showcaseColumn` for the primary
  and every secondary; every arm `pass`, every `fidelityPass`; primary arms `overrides.empty()`; write
  `evidence/coverage-matrix/shard-<k>.csv` (columns: preset, column, primary|secondary, variant, note, value, bar, margin,
  window_ms, overrides, descriptor, tap).

**Implementation** (plan §5.4 table, row by row; call-only composition under `ScriptRenderScope(processorRenderHook, &ctx)`):
`showcaseColumn(p, comp, c, primary)` builds lo / hi from P per the table's "Arms" column, declares `ctx.allowedEndpoints` (§5.4
"Endpoint declaration", including the internally built lo states of `auFieldMotionCell` / `auMotionCell`), calls exactly the listed
moved entry point (`auSteadyCell`, `auFloorRule`, L1–L11, `auFieldMotionCell`, `auMotionCell`, `auEnvRetriggers`) per gated note
(E1 and C3; AT-32 E1 only; AT-26 / AT-27 one cell per note; AT-13 on the stored variant only, P-2), and copies the returned
`AuCell`'s `value` / `pass` / `barText` into `ShowcaseArm` — **no arithmetic on readings**. Secondary-claim preconditions set on both
arms and recorded in `overrides`; setting-defined windows (AT-11 / AT-12) recorded in `windowMs`; reference reverse arms as plan
§5.4 (R-LOCK L1 with lo = A(P), hi = PUNCH 6 dB / 40 ms; R-SINE `auSteadyCell DCx` lo Sine / hi Triangle; R-SOLID `auSteadyCell DSt`
+ `auFloorRule`, lo SOLID / hi SPREAD spread 1). `describePreset` per plan §5.8 components 0–14 (P-5: components 10–12 from
`auSteady(S(P), note, d)` under the preset hook, component 12 = `eSubDb` minus the mid total of `ctx.lastSumL/R` over
`readWindow`); descriptor window [1.0 s, 3.0 s) or [t₉₀, t₉₀ + 2^16) fallback, a window still below −60 dBFS marks the descriptor
invalid (SC-020 fail). `presetDistance` per P-7 units and masking.

**Verified by:** `profundum_tests "Profundum_ShowcaseArmMechanics" "Profundum_PresetDistanceMath"`; the shards run in T027.

---

# GROUP 17 — Sweep-family cases

## T019 — Sweep, rates, max Mass, mono sum, determinism, loudness, distinctness (FR-038 – FR-044, FR-061, FR-063, FR-064; plan §5.5, §5.6, §5.8, §5.9, §5.12)

**File edited:** `plugins/profundum/tests/integration/preset_sweep_test.cpp` (and `preset_render_support.h` only for
`PresetRenderReadings` / `presetReadings`, if T018 did not add them — check first; this task is alone in its group either way).

**Failing tests first — mechanism, per push (`[profundum][sweep]`), on a test-local preset T (endpoints `auStateAs()`, sustain 1):**
- `Profundum_SweepTimeline`: `makeTimeline(48000, tail)` → noteOff 144000, total 144000 + tail, block 512; at 44100 → noteOff
  132300; at 96000 → 288000. `getTailSamples()` read after `setupProcessing` and `setState` equals the Phase 11 formula
  `ceil((R_eff + 1250) · fs / 1000) + 64` for T's release.
- `Profundum_PresetMonoSumMechanics`: on T's 48 kHz E1 render: `bandReading` on W₁ = [1.5 s, 1.5 s + 2^16) below the recorded
  crossover is valid with ρ ≥ 0.99 and |Q| ≤ 1.0 dB; the anchor mismatch count of `PresetSweepObserver` is 0; **non-vacuity:**
  R' = −L on a copy fails the ρ bar; a SOLID-stored T reads L == R bitwise from s₀ = 544 samples (64 + llround(10 ms · 48 kHz)).
- `Profundum_PresetDeterminismMechanics`: a test-local DRIFT → Mass preset renders twice under its seed within 5e-4 per sample and
  differs by > 5e-4 somewhere under `seed ^ 0x5A5A`; T (seed-independent) under `seed` and `seed ^ 0x5A5A` within 5e-4 per sample.

**The library cases** (iterate `allPresets()`; `REQUIRE(!allPresets().empty())`):
- `Profundum_PresetSweep` (`[long]`): E1 and C3 at 48 kHz, + the E1 / B1 / E2 chord (MIDI 28, 35, 40, asserted) for presets storing
  Poly: 0 non-finite; held RMS over [0.2 s, 3.0 s) > −60 dBFS; tail RMS over the last llround(0.25 fs) samples ≤ −100 dBFS.
- `Profundum_PresetSweepRates` (`[long]`): E1 at 44.1 and 96 kHz: the same checks + true peak ≤ −0.9 dBTP; worst margin printed.
- `Profundum_PresetTruePeakMaxMass` (`[long]`): four parameter-only blocks before `setupProcessing` ({1001 → 0, 104 → 1.0},
  {1001 → 1, 104 → 1.0}, {1001 → 0}, Mod Wheel 6 = 1.0); E1 and C3; true peak ≤ −0.9 dBTP; 0 non-finite.
- `Profundum_PresetMonoSum` (`[long]`): plan §5.6 on the 48 kHz E1 sweep render; W₁ then W₂ fallback; ungated count 0 (a preset
  invalid in both windows fails); anchor mismatches 0; SOLID-active presets L == R from s₀; the R = −L control fails.
- `Profundum_PresetDeterminism` (`[long]`): plan §5.9 for every preset, in both directions of `seedDependent`; a contradicting
  measurement fails naming the flag.
- `Profundum_PresetLoudnessMatch` (`[long]`): plan §5.12; |L − median| ≤ 4 dB; median and every deviation printed.
- `Profundum_PresetDistinctness` (`[long]`): every pair d ≥ F (F read from `evidence/distinctness.csv`); closest pair, d and masked
  count printed; every dNl twin's fidelity arm passes; no descriptor window below −60 dBFS.
- `Profundum_PresetDistinctnessPilot` (`[profundum][distinct][.pilot]`): plan §5.8 pilot steps 1–5 (twins: seed `kDefaultSeed ^
  0x5A5A` over seed-independent presets, block 37 vs 512 over all; F = max(2.5 · t_max, 1.0); near-variant "Bedrock Sine" with
  Mass (104) on endpoint A + 0.05 normalized reads d < F; X₁ / X₂ from `showcasePair()` as test-only `withEndpoints` Comps read
  d ≥ F); writes `evidence/distinctness.csv` (F, t_max, the twin that set it, the near-variant row, the control row).
- `Profundum_PresetDistinctnessParams` (per push, Deviation 6): for every pair of `allPresets()`, ≥ 3 registered parameters differ by
  ≥ 0.1 normalized or by list index (each endpoint's 60 E-parameters counted separately: 120 + 24 slots), from the decoded Comps
  re-normalized through the laws. With fewer than 2 presets the loop is empty; its **non-vacuity** arm: two test-local defs
  differing in exactly 2 parameters fail the predicate, 3 pass.

**Implementation:** `PresetSweepObserver final : Krate::DSP::ProfundumEngineObserver` modelled on `MspObserver`
(`mono_sum_params_test.cpp:93-132`; counts `anchorL != anchorR` per voice, records the resolved crossover; no per-sample vectors);
the render cache `presetReadings(index, note, fs, variant)` (function-local static map; readings kept, audio dropped; plan §4.7, R-4).

**Verified by:** `profundum_tests "Profundum_SweepTimeline" "Profundum_PresetMonoSumMechanics" "Profundum_PresetDeterminismMechanics"
"Profundum_PresetDistinctnessParams"`; the rest in T023 / T027.

---

# GROUP 18 — Load safety and the per-push render guard

## T020 — `Profundum_FactoryPresetLoadRtSafe`, `Profundum_PresetRenderGuard`, `Profundum_PresetLoadUnderHeldNote` (FR-037, FR-046, FR-048; SC-015, SC-027, SC-028; plan §5.5, §5.10)

**File edited:** `plugins/profundum/tests/integration/preset_rt_test.cpp`.

**Failing tests first** (`[profundum][preset][rt]`, per push, never `[long]`). Each case runs on the test-local preset T (T018's
mechanics preset) **and** on every `allPresets()` entry (empty before G22), and prints `presets=<n>`:
- `Profundum_FactoryPresetLoadRtSafe`: prepare at block 512 holding E1; `setState(Comp)` outside the measured region; one
  `process(512)` inside `TestHelpers::AllocationScope` reads `getAllocationCount() == 0`.
- `Profundum_PresetRenderGuard`: E1, 48 kHz, block 512, note-off at 0.5 s, total 0.75 s; stored and at max Mass (the T019 four-block
  prelude); 0 non-finite; true peak ≤ −0.9 dBTP (`measureTruePeakDb`); the case's wall clock is printed (`std::chrono::steady_clock`)
  and `CHECK(seconds <= 15.0)`; if T027 measures it above 15 s with the full library, T027 splits it into `_0of2` / `_1of2` (R-11; re-split into `_0of4` to `_3of4` at the close, then into `_0of8` to `_7of8` after the CI wall clock, both 2026-10-25, SC-027 row).
- `Profundum_PresetLoadUnderHeldNote`: default-state processor holds E1 from 0; at L = 12288 `setState(Comp)` between two
  `process()` calls; render to L + llround(0.1 fs) + 4096 + 512; 0 non-finite; `step(L, L + 20 ms) ≤ 2 · max(step(L − 4096, L),
  step(L + 100 ms, L + 100 ms + 4096))`; worst ratio printed. **Non-vacuity:** a copy of T's render with a 0.5 full-scale step
  injected at L + 100 samples fails the bound.

**Verified by:** `profundum_tests "[rt]"`.

---

# GROUP 19 — CPU case

## T021 — `Profundum_PresetCpu` (FR-045, FR-062; SC-022, SC-032; plan §5.11)

**File edited:** `plugins/profundum/tests/integration/processor_cpu_test.cpp` (stays outside the `-fno-fast-math` block).

**Test** `TEST_CASE("Profundum_PresetCpu", "[profundum][.perf][performance]")`: `REQUIRE(!allPresets().empty())`; per preset the
four arms of plan §5.11 (MONO with Voice Mode forced to Mono on Poly presets and the override recorded; POLY4 for Poly presets on
the stored state; D and D' through `mutableEngineForTest()`), notes E1, B1, E2, G#2 (MIDI 28, 35, 40, 44) asserted and printed, at
block 0; 640 trials × 10 blocks of 512 at 48 kHz, reusing `kTrials`, `ProcessorArm`, `DirectArm`, `timeArm` and the interleaving
(`:66-330`); control best(D) / best(D') ∈ [1/1.05, 1.05] or the run is INVALID (printed, not read); MONO ≤ 8 %, POLY4 ≤ 25 % of the
block period; every reading and the maximum written to `evidence/cpu.csv`.

**Verified by:** build only here; the reading is T035's lane (alone, after an idle).

---

# GROUP 20 — Audition

## T022 — `Profundum_PresetAuditionRenders` and `evidence/listen/render.sh` (FR-050; SC-023; C-9 rule 5; plan §6 rule 5, §8 "Audition")

**Files:** edit `plugins/profundum/tests/integration/preset_sweep_test.cpp`; create
`specs/profundum-phase13-presets-release/evidence/listen/render.sh` (copied from `specs/profundum-phase12-ui/evidence/listen/render.sh`).

**Hidden case** `TEST_CASE("Profundum_PresetAuditionRenders", "[profundum][.listen]")`: `REQUIRE(!allPresets().empty())`; for each
preset `evidence/listen/<category>/<name>_E1.wav` and `_C3.wav` (48 kHz, 24-bit or float WAV as the Phase 12 case writes); gesture
WAVs `_morph` (Morph 0 → 1 over 2 s, for AT-41 / 42 claimants), `_macro` (the claimed macro 0 → 1, AT-47–50), `_legato` (E1 → E2
legato pair, AT-23 / 43–46); `manifest.txt` (one line per WAV: file, preset, note, gesture); `listening-sheet.md` (one block per
preset with an empty `Verdict (user):` line; an agent never fills it); for every preset where `fundamentalLossFlagged` holds,
Δ_fund = E(stored) − E(Amount 0) with `auRd::auWindowESubDb` over the §5.8 descriptor window of the E1 48 kHz render (reference:
Character Amount 0 on both endpoints via `withEndpoints`), written to `evidence/listen/fundamental-loss.csv` and as that preset's
note in the sheet.
`render.sh`: builds `profundum_tests` (`-- /nr:false`), runs the hidden case, asserts manifest lines = 2N + gesture count and the
sheet holds N `Verdict (user):` lines and exactly one Δ_fund note per flagged preset; prints `LISTEN GREEN|RED`.

**Executor:** `bash -n` the script. **Verified by:** build; the run is a close step (T027 runs it with the library lanes).

---

# GROUP 21 — Pilot

## T023 — Pilot rows 51–53, `Profundum_PresetDefsRules`, `Profundum_ReferenceColumns`, the pilot run (FR-007, FR-013, FR-043; SC-006 rules, SC-008, SC-020 pilot; plan §6, §7 rows 51–53, §5.8 pilot) — **main loop**, blocks until F is recorded

**Precondition:** T017 unblocked (FR-022: `evidence/probe-table.csv` recorded with 102 rows, PROBE GREEN). If not, return `blocked`.

**Files edited:** `tools/profundum_preset_defs.h`, `plugins/profundum/tests/unit/preset/preset_library_test.cpp`,
`plugins/profundum/tests/integration/preset_showcase_test.cpp`. **Created:** `evidence/pilot/run-pilot.sh`, `evidence/run-log/T023.txt`.

**Failing tests first (per push):**
- `Profundum_PresetDefsRules` (`[profundum][preset]`, in `preset_library_test.cpp`): for every `allPresets()` def, built through
  `buildPresetComponentState` and decoded, plan §6 rules 1–10: (1) on both endpoints amp attack ≤ 1000 ms, attack + decay ≤ 2000
  ms, release ≤ 4000 ms, Morph ramp ≤ 2000 ms; (2) Output Gain normalized 1.0, ceiling −1 dBTP, bend 2 st, polyphony 4, Edit
  Target A; (3) ENV Motion → Unipolar unless the Comment contains "resting offset"; (4) never Rectify + Character Tone 1 + Fold 100
  + WIDE; (5) print the `fundamentalLossFlagged` set, and assert the predicate is true on one constructed flagged state and false on
  one unflagged state; (6) names present, ≤ 31 bytes, sanitised, not "A" / "B"; (7) a single-state claim stores Morph Position 0;
  an AT-33 claim stores SPREAD, spread 1, depth 1; (8) a primary stores every precondition its row names (plan §5.4 column 5);
  (9) `seedDependent` true when an active endpoint stores DRIFT, Field Motion DRIFT with depth > 0, or a random Cycle Shape;
  (10) exactly one primary per def. **Non-vacuity:** a test-local def violating each of rules 1–4, 6–10 fails the rule's
  predicate (one def per rule).
- `Profundum_ReferenceColumns` (`[profundum][showcase]`, in `preset_showcase_test.cpp`): the three reference presets (R-SOLID
  "Mono Bedrock", R-LOCK "Locked Floor", R-SINE "Bedrock Sine") decode as SOLID / LOCK / Sine on **both** endpoints; their reverse
  arms (T018) at E1 and C3 clear AT-29 / AT-11 / AT-14 bars; "Mono Bedrock" on its own short render (E1, 48 kHz, block 512,
  note-off 0.5 s, total 0.75 s) reads L == R bitwise from s₀ = 544.

**Implementation:** author plan §7 rows 51–53 in `allPresets()` (names, categories Sub, Comments naming their primary in words —
"Reference Solid", "Reference Lock", "Reference Sine" — endpoint A / B parameter lists realising the "Intended sound" column, names
A / B, `seedDependent = false`). Only these three rows (FR-022 is satisfied: the probe is recorded).
`run-pilot.sh`: build `profundum_tests`, run `"Profundum_PresetDistinctnessPilot"` and `"Profundum_ReferenceColumns"` to
`evidence/pilot/pilot.log`, print `PILOT GREEN F=<F>` when `evidence/distinctness.csv` exists and both controls pass.

**Executor:** write rows, tests and the script; if `evidence/distinctness.csv` is absent or `T023.txt` lacks `PILOT GREEN`, return
**`blocked`** with notes "main loop: run `bash …/evidence/pilot/run-pilot.sh`; quote F, t_max, the near-variant and control rows
into run-log/T023.txt; unblock T023 on PILOT GREEN; on a failing control STOP (F is never lowered)".

---

# GROUP 22 — The library

## T024 — Library checks first (FR-001 – FR-006, FR-008, FR-009, FR-011, FR-024, FR-032 – FR-036, FR-060; SC-001 – SC-006, SC-010, SC-012 – SC-014, SC-030)

**Files edited:** `plugins/profundum/tests/unit/preset/preset_library_test.cpp`, `plugins/profundum/tests/integration/preset_browser_test.cpp`.

**Failing tests** (per push unless stated; `[profundum][preset]` / `[profundum][browser]`; files read from `PROFUNDUM_PRESETS_DIR`):
- `Profundum_PresetCategoriesMatchFilesystem` (SC-001): the directory set under `PROFUNDUM_PRESETS_DIR` equals the six FR-001 names
  both ways; every regular file is `*.vstpreset` directly inside one of them; 0 stray files (no `.gitkeep`).
- `Profundum_FactoryPresetContainer` (SC-002): every file parses (48-byte header, List, `Comp`, `Info`); class ID == `kProcessorUID`;
  the `Info` attributes compared with **independent** expectations (never `buildProfundumInfoXml`): `MediaType == "VstPreset"`,
  `PlugInName == "Profundum"`, `PlugInCategory == "Synth"`, `Name ==` file stem, `MusicalCategory == MusicalInstrument ==`
  `path.parent_path().filename()`; `Comment` non-empty and containing, case-insensitively, every word of the primary column's
  `capabilityTable()` label after its "AT-nn " / "R-…" prefix; exactly 7 attributes.
- `Profundum_FactoryLibraryShape` (SC-003): 6 categories, each ≥ 3 presets; N ≥ 53 (expect 53); 0 duplicate names; 0 names failing
  `PresetManager::isValidPresetName`.
- `Profundum_FactoryPresetStateFormat` (SC-004): every `Comp`: version 3, 574 bytes, 0 non-finite floats (`presetFloatIsFinite`),
  Edit Target 0, `|runtime.target − morph.position| ≤ 2.5e-4` and `|runtime.position − morph.position| ≤ 2.5e-4`, seed == the def's.
- `Profundum_FactoryPresetRoundTrip` (SC-005): `PresetHost` load → save → `decodedEqual(…, 2.5e-4)`; names byte-equal and ≠ "A"/"B".
- `Profundum_CoverageMatrixComplete` (SC-006): 53 / 53 columns have ≥ 1 primary; every def exactly 1 primary; AT-13 claimed by a
  preset storing Rectify **and** by one storing Fracture (P-2).
- `Profundum_GeneratorSmoke` gains `CHECK(written == 53)` (SC-010).
- `Profundum_CommittedPresetTreeMatches` (SC-012): `writeFactoryTree(temp)` against the committed tree: same relative paths, `Info`
  byte-equal, `decodedEqual(…, 2.5e-4)` on every `Comp`.
- `Profundum_FactoryPresetCeiling` (SC-030): decoded `outputCeilingDb` == −1.0 (±1e-6) for 100 % of presets.
- `Profundum_PresetBrowserLoad` (SC-013, in `preset_browser_test.cpp`): `ConnectedPair` + `RecordingComponentHandler`; for every
  preset, `presetManagerForTest()->loadPreset(PresetInfo{path, …})` returns true; decode of `proc->getState` equals decode of a
  fresh `setState(Comp)`, 0 mismatches, with the seed, both names and the runtime block named; `restartFlags` contains
  `kParamValuesChanged`; `kMsgLoadPresetState` count == 1 per load.
- `Profundum_FactoryPresetScan` (SC-014): a `PresetManager` with `factoryDirOverride = PROFUNDUM_PRESETS_DIR` scans N = 53, each
  under its directory's category, 0 with an empty subcategory.
- `Profundum_InstalledPresetTree` (`[profundum][browser][.install]`, Windows only, `#ifdef _WIN32`): `getFactoryPresetDirectory()`
  equals `%PROGRAMDATA%\Krate Audio\Profundum`; a scan with no override lists 53 presets under exactly the six categories.

These cases are red until T026 lands the tree (same group; the gate runs after T026).

**Verified by:** the G22 gate, after T026.

## T025 — Author plan §7 rows 1–50 (FR-004, FR-007, FR-009, FR-012, C-2.2, C-9; plan §7, §6)

**Files edited:** `tools/profundum_preset_defs.h`; **deleted:** `plugins/profundum/resources/presets/Sub/.gitkeep` (FR-002).

**Implementation:** add rows 1–50 of plan §7 to `allPresets()` in table order before rows 51–53 (category counts: Sub 12, Driven 11,
Harmonic 8, Animated 8, Plucked 7, Resonant 7). Each def: the table's name, category, primary and "Notable secondaries" (row 9 claims
AT-13 as a secondary on the Fracture variant; row 13 is the Rectify primary); a Comment naming the primary column in words (the
`capabilityTable()` label words); endpoint A / B lists realising the "Intended sound and lever" column; every precondition of its
primary stored (plan §5.4 column 5); p = 0 wherever the table says so; P-6 pins (no ID 0, 2, 3, 417 in `controls`); A / B names
≤ 31 bytes, never "A" / "B"; `seedDependent = true` for row 40 (DRIFT) and any row storing DRIFT or a random Cycle Shape; no
Rectify + Tone 1 + Fold 100 + WIDE (rows 6, 18, 30 keep Tone ≤ 0.6 or SOLID / SPREAD); ENV rows Unipolar (row 39). Rows 41, 42,
48 author a distinct endpoint B. The hi values are intentions (plan §7): T027 measures them.

**Verified by:** T024 cases after T026.

## T026 — Generate and commit-stage the factory tree (FR-002, FR-024, FR-025) — **main loop**, blocks until the tree exists

**Files created:** `evidence/gen-tree.sh`, `evidence/run-log/T026.txt`.
`gen-tree.sh`: build `generate_profundum_presets` (`-- /nr:false`, 0 warnings); then `find plugins/profundum/resources/presets -type f
| sort` must list exactly 53 `*.vstpreset` and no other file; print `TREE GREEN n=53` or `TREE RED`.

**Executor:** write the script; if the tree does not hold 53 files, return **`blocked`** with notes "main loop: run `bash
…/evidence/gen-tree.sh`; quote its output into run-log/T026.txt; unblock T026 on TREE GREEN (the G22 gate then runs the T024
cases)". A T024 red at the gate is an authoring defect of T025's rows: re-author (never a check).

---

# GROUP 23 — `[long]` library lanes

## T027 — Showcase shards, sweep family, distinctness, audition — **main loop**, blocks until green (SC-007, SC-016 – SC-021, SC-023, SC-027 wall clock, SC-031)

**Files created:** `evidence/library/run-library-lanes.sh`, `evidence/run-log/T027.txt`.

`run-library-lanes.sh` (detached, status file `evidence/library/status.txt`, one line per lane: name, exit, wall clock): one build of
`profundum_tests` (`-- /nr:false`, 0 warnings); then **concurrently** (each to its own log): the eight
`Profundum_PresetShowcase_<k>of8` processes; one process `"Profundum_PresetSweep*" "Profundum_PresetTruePeakMaxMass"
"Profundum_PresetMonoSum" "Profundum_PresetLoudnessMatch"` (shares the render cache); one `"Profundum_PresetDeterminism"`; one
`"Profundum_PresetDistinctness"`; `bash evidence/listen/render.sh`. Then `node evidence/merge-csv.js evidence/coverage-matrix
evidence/coverage-matrix.csv <claims>` (claims = Σ over defs of gated cells of primary + secondaries, printed by the shards);
`git checkout -- specs/profundum-phase9-voice-engine/evidence/`; the `Profundum_PresetRenderGuard` wall clock from a per-push run
with the full library (split if > 15 s, R-11). Prints `LIBRARY LANES GREEN` or `RED` with the failing lane's first FAILED line.
Expected cost: showcase ≈ 10–15 min wall on 8 shards, sweep set ≈ 10 min (plan §10).

**Executor:** write the script; if `T027.txt` lacks `LIBRARY LANES GREEN`, return **`blocked`** with notes "main loop: run
`bash …/evidence/library/run-library-lanes.sh` detached; quote each lane's last lines and wall clock into run-log/T027.txt; a red
showcase / mono / loudness / distinctness reading is an authoring defect: re-author the preset in tools/profundum_preset_defs.h,
regenerate (T026's script), re-run only the affected lane; a column that cannot reach its bar from the shipped surface is an FR-023
STOP; unblock T027 on GREEN".

---

# GROUP 24 — Manual and site

## T028 — `MANUAL.md`, `docs/`, `Profundum_ManualCoversSurface` (FR-051, FR-052; SC-024; plan §4.8)

**Files:** create `plugins/profundum/MANUAL.md`, `plugins/profundum/docs/index.html`, `plugins/profundum/docs/manual-template.html`,
`plugins/profundum/docs/assets/` (copied from `plugins/vorago/docs/assets/`, re-texted); edit
`plugins/profundum/tests/unit/manual_coverage_test.cpp`.

**Failing test first** `TEST_CASE("Profundum_ManualCoversSurface", "[profundum][docs]")`: read `PROFUNDUM_RESOURCES_DIR "/../MANUAL.md"`;
for every registered parameter ID except 4–7, its title (from a heap `ControllerFixture`) appears verbatim; the six category names
and all 53 preset names appear; `docs/index.html` exists and contains `MANUAL` (a link to the manual). **Non-vacuity:** the same
predicate on a copy of the text with one title removed reports that title missing.

**Implementation:** the Vorago section order (Introduction, Signal Flow, Interface Overview, Macros, Header Controls, one section per
page, Presets, Performance Notes, Cross-Platform, Tips & Tricks); every user-visible registered title verbatim; the Anchor / Mass
split, Morph, Edit Target and Compare; the macro table from Phase 12 D3 (`specs/profundum-phase12-ui/design/macro-mapping.md`); the
library by category with each preset's one-line Comment; the header PRESETS button. No hype, no transient machine state.

**Verified by:** `profundum_tests "Profundum_ManualCoversSurface"`.

---

# GROUP 25 — Rosters and changelog

## T029 — Release-skill roster, determinism script, CI shard table, CHANGELOG, plugin `CLAUDE.md` (FR-028, FR-053, FR-054; plan §4.8, §9)

**Files edited:** `.claude/skills/release/SKILL.md` (`profundum` in Inputs `:15-16`; row `| profundum | profundum_tests |
Profundum.vst3 |` in the table `:22-31`); `tools/check-preset-generator-determinism.js` (`profundum: 'profundum_preset_generator',`
in `GENERATORS` `:54-57`, header comment mention); `tools/ci-test-shard.js` (long-lane `kSuiteShards` += `profundum_tests: 4`; the
per-push lane only if T027 measured the render guard needing `profundum_tests: 2`); `tools/ci-test-times.json` (the measured seconds
from T027's status file); `plugins/profundum/CHANGELOG.md` (three bullets under `[0.1.0] - Unreleased` → `### Added`: the 53-preset
factory library in six categories, the header preset browser, the manual; **`version.json` not touched**);
`plugins/profundum/CLAUDE.md` (presets, browser, `kMsgLoadPresetState`, `setEngineSeamsForTest`, the new test TUs).

**Test.** `node tools/check-preset-generator-determinism.js --help` (or its usage path) lists `profundum` (seconds; quote into
`evidence/run-log/T029.txt`). The SC-011 run itself (build + two generator runs) is T036's lane.

**Verified by:** G25 gate (no code; per-push unchanged).

---

# GROUP 26 — CMake registration audit (single)

## T030 — CMake registration and audit (plan §9; Deviation 1)

**Files edited:** `plugins/profundum/tests/CMakeLists.txt` and root `CMakeLists.txt` only for fixes found by the audit.

**Audit** (counts into `evidence/run-log/T030.txt`): each of the seven Phase 13 TUs appears exactly once in `add_executable(
profundum_tests …)` and once in the `-fno-fast-math` block; `integration/processor_cpu_test.cpp` in no flag block; `tools` and
`tests/test_helpers` in the include dirs; `KRATE_DSP_TESTS_DIR` and `PROFUNDUM_PRESETS_DIR` defined; `krate_test_pch` last;
`dsp/tests/CMakeLists.txt` unchanged (`git diff e6095b24 -- dsp/tests/CMakeLists.txt` empty); the root generator block names
`goertzel_bank_simd.cpp` and `hwy` and neither `vstgui_support` nor `Catch2`; no `/wd`, `-Wno-` or `pragma warning` added
(`git diff e6095b24 -- '*.txt' '*.h' '*.cpp' | grep -cE "^\+.*(/wd|-Wno-|pragma warning)"` = 0, the copied Vorago flag block
excepted and named).

**Verified by:** G26 gate: `Profundum profundum_tests profundum_preset_generator` build with 0 warnings.

---

# GROUP 27 — Integration lanes (main loop at close)

## T031 [P] — Scope fence, ODR re-sweep, greps (FR-057 scope, non-goals)

**Files created:** `evidence/close/scope-fence.js`, `evidence/run-log/T031.txt`.
`scope-fence.js`: the union of `git diff --name-only e6095b24` and `git status --porcelain --untracked-files=all` must match the
allow-list `plugins/profundum/**`, `tools/profundum_preset_defs.h`, `tools/profundum_preset_generator.cpp`, root `CMakeLists.txt`,
(FR-065, 2026-10-24) `plugins/shared/src/preset/preset_manager.cpp`, `plugins/shared/src/ui/preset_browser_view.h`,
`plugins/shared/src/ui/preset_browser_view.cpp`, the T037 test files under `plugins/shared/tests/` and `plugins/shared/tests/CMakeLists.txt`,
`tools/check-preset-generator-determinism.js`, `tools/ci-test-shard.js`, `tools/ci-test-times.json`, `.claude/skills/release/SKILL.md`,
`dsp/tests/unit/systems/profundum_engine_audibility_test.cpp`, `dsp/tests/unit/systems/profundum_audibility_readers.h`,
`specs/profundum-phase13-presets-release/**`, `specs/Profundum-roadmap.md` (`.serena/project.yml` excluded by name). Any
`dsp/include`, `dsp/src`, `plugins/shared` or other plugin path is RED. Greps: every claimed name of T001 step 4 defined exactly
once; `grep -rnE "std::isnan|std::isinf|std::isfinite|std::fpclassify"` over the new files = 0; `kMsgLoadPresetState` used only
in `plugin_ids.h`, `processor.cpp`, `controller.cpp` and tests. Prints `SCOPE GREEN|RED`.
**Executor:** write and run (seconds); quote the output.

## T032 [P] — Suites lane and pluginval — **deferred to the main loop** (SC-013 lifecycle, SC-025 build/tests/pluginval)

**Files created:** `evidence/close/suites.sh`.

**Lane scope (encoded 2026-10-18, Phase 6 ruling G9-4).** Executables: `profundum_tests` (every Phase 13 TU and the edited Phase 12
TU) and `dsp_systems_tests` (consumer of the modified Phase 9 TU — but FR-058 limits it to the moved cases, already proven by T004).
`suites.sh` runs detached and writes `evidence/run-log/close/status.txt` (name, exit, wall clock per lane):
1. **Change-set scope.** `git diff --name-only e6095b24 -- dsp/include dsp/src plugins/shared tests/test_helpers` must print
   nothing (no shared header modified): then no other instrument's suite and no consumer full suite runs. Anything printed is RED
   and stops the script.
2. One build of `Profundum profundum_tests profundum_preset_generator dsp_systems_tests` with `-- /nr:false` to `build.log`;
   warning count 0.
3. Concurrently: (a) `profundum_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"` → `All tests passed`;
   (b) `dsp_systems_tests.exe "[voice-engine]~[long]"` (the TU's per-push smoke cases only; FR-058 forbids the full suite);
   (c) the `[long]` cases of the **phase's own TUs only**: `profundum_tests.exe --list-test-names-only "[profundum][long]"` filtered
   to the Phase 13 names of this file (`Profundum_PresetShowcase_*of8`, `Profundum_PresetSweep`, `Profundum_PresetSweepRates`,
   `Profundum_PresetTruePeakMaxMass`, `Profundum_PresetMonoSum`, `Profundum_PresetDeterminism`, `Profundum_PresetLoudnessMatch`,
   `Profundum_PresetDistinctness`) — skipped when T027's record for the same commit is GREEN, otherwise one process per name; a
   listed `[long]` name outside the set is reported, not run; (d) `--list-tests` shows every SC case name of this file;
   (e) `tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Profundum.vst3` → exit 0;
   (f) `node tools/check-preset-generator-determinism.js --plugin profundum` → exit 0 (SC-011); (g) on Windows,
   `profundum_tests.exe "Profundum_InstalledPresetTree"` after the `Profundum` build's install step (SC-014 installed arm).
4. A summary block with every lane's verdict and wall clock.
**Executor:** write, `bash -n`, return done with notes "deferred to the main loop at close: run `suites.sh` detached; quote each
lane's last lines into evidence/run-log/T032.txt; a Phase 13 red is owned (fix, re-run only the affected case); a timing-budget
assertion of an untouched case that fails under the concurrent lanes is recorded as load, not re-run".

## T033 [P] — Portability and Apple-globals lint (FR-056; SC-026)

**Files created:** `evidence/close/portability.sh`, `evidence/run-log/T033.txt`.
1. The `.cpp` list = `git diff --name-only e6095b24` ∪ untracked, filtered to `\.cpp$` (expected: `processor.cpp`, `controller.cpp`,
   `panel_sub_controller.cpp`, `tools/profundum_preset_generator.cpp`, the seven Phase 13 TUs, `editor_layout_test.cpp`,
   `processor_cpu_test.cpp`, the Phase 9 TU). `node tools/check-portability.js <files…>` with a 300 s ceiling; required
   `check-portability: all clear -- N compiled.` with N equal to the list length.
2. `node tools/lint-apple-globals.js` over the new headers and TUs (read its usage line first).
3. `wsl --shutdown`. Output to `evidence/run-log/close/portability.log`, last line `PORTABILITY GREEN|RED`.
**Executor:** write and run once. If it fails with `wsl: Failed to translate` or exceeds 5 minutes, stop it and return done with
notes "deferred to the main loop at close: run `bash …/evidence/close/portability.sh` unsandboxed".

## T034 [P] — clang-tidy — **deferred to the main loop** (FR-056; SC-026; plan P-9)

**Files created:** `evidence/close/tidy.sh`.
Reconfigure `build/windows-ninja` inside vcvars64 (test CMake and root CMake changed; the stale compile-database hazard); confirm the
new TUs and `tools/profundum_preset_generator.cpp` are in `compile_commands.json`; `./tools/run-clang-tidy.ps1 -Target profundum
-BuildDir build/windows-ninja`; then the generator TU and the Phase 9 TU **by file list** (the `profundum` target covers only
`plugins/profundum/{src,tests}`, `run-clang-tidy.ps1:229-239`), each to its own log under a 900 s wall-clock ceiling; last line
`TIDY GREEN|RED` (0 warnings). Never run right before T035.
**Executor:** write, `bash -n`, return done with notes "deferred to the main loop at close: run `tidy.sh` concurrently with
`suites.sh`; quote into evidence/run-log/T034.txt".

## T035 [P] — CPU lane, alone — **deferred to the main loop** (FR-045, FR-062; SC-022, SC-032)

**Files created:** `evidence/close/cpu-lane.sh` (`node tools/run-cpu-tests.js profundum_tests` to `evidence/run-log/close/cpu.log`;
last line `CPU GREEN|RED`).
Precondition: T032, T033, T034 finished; the machine idle; **never straight after the clang-tidy lane** (roadmap handoff 13.5: one
such run read every arm 10–19 % slow).
**Executor:** write, `bash -n`, return done with notes "deferred to the main loop at close: run `cpu-lane.sh` last and alone; quote
every preset's MONO / POLY4 reading, the control ratio and the maximum into evidence/run-log/T035.txt". A red reading is re-run once
after the machine has idled; still red → a defect reported to the user. No budget relaxed, no workload shrunk.

## T036 [P] — Release gate (FR-055; SC-025)

**Files created:** `evidence/close/release-gate.md` (the command and its expected matrix).
The `release-readiness` workflow for `profundum` (`.claude/workflows/release-readiness.js:23` maps it): build, `profundum_tests`,
pluginval strictness 5, version.json / CHANGELOG sync. Expected: all four green, with `version.json` unchanged (the open
`[0.1.0] - Unreleased` entry takes the bullets; memory rule "unreleased version takes new changes").
**Executor:** write the file; return done with notes "deferred to the main loop at close: run the release-readiness flow with
args {plugins:["profundum"]} after T032; quote the matrix into evidence/run-log/T036.txt. Cutting the release (bump, tag, push) is
the user's decision and is not part of this phase".

---

## Close procedure (main loop, in this order)

1. T001: grill the user on plan §13, write `evidence/plan-rulings.md`, unblock T001 (rows amended first, then Clarifications).
2. T002: `run-cases.sh pre` → unblock. G3 gate. T004: `run-cases.sh post` + `compare.js` → `MOVE GATE GREEN` → unblock.
3. G5 – G14 gates as the workflow runs them.
4. T017: `run-probe.sh` → PROBE GREEN → record the probe-table commit (FR-022) → unblock.
5. G16 – G20 gates. T023: `run-pilot.sh` → PILOT GREEN → unblock.
6. G22: T026 `gen-tree.sh` → TREE GREEN → unblock; the gate runs T024's cases (re-author on red).
7. T027: `run-library-lanes.sh` detached → re-author until LIBRARY LANES GREEN → unblock; hand the user the audition WAVs and
   `listening-sheet.md` (verdicts are the user's).
8. G24 – G26 gates.
9. T032 `suites.sh` detached + T034 `tidy.sh` concurrently (+ T033 `portability.sh` if deferred); T036 release-readiness after T032.
10. T035: the CPU lane, alone, after the machine has idled.
11. `node tools/clean-test-output.js --apply` (memory rule: clean up test output at every close).
12. Comply quotes the run-log files. The macOS / Linux CI legs are **PENDING-CI** until the user pushes.

## Open questions carried from the plan (built at the plan's reading; surface, do not decide)

- Plan §13 items 1–10 (P-1, P-2, P-3/P-4, P-5, P-6, P-7, CPU MONO arm, P-10, P-11, §2.0 texts): T001 blocks until ruled.
- This task list, Deviation 2: library-iterating per-push cases land in G22, not G1.
- This task list, Deviation 4: the one-element uidesc insert uses the Edit tool rather than XSLT.
- This task list, Deviation 5: P-3 / P-4 / P-7 carrying rows (FR-007, C-7, SC-020) added by T001.

## Defects found

(Empty at task-cut time. Gates, fixers and the main loop record entries here: task, case / section, verbatim reading, and triage.)
- **G22 gate, T020 `Profundum_PresetLoadUnderHeldNote` (SC-028), FIXED by re-authoring T025 row 9.** Verbatim: `preset Fault Line: step(L, L + 20 ms) = 0.420299, reference = 0.166022, ratio = 2.53158 (bar 2)`. Triage: the 0.42 edge sits at L + 605 and is identical to six digits with Character Amount 0 or Tube, so it is the un-fractured core edge (Edge 1, Drive 1, Fold 80, SHAPE 0.5) heard while the Character selection transitions mid-stream (Phase 11 FR-074: "Restored mid-stream, the engine transitions as for any setter change"), measured against the lower fractured steady edge. No processor or `dsp/` defect; the bar is unchanged. Fix: Fault Line Fold SHAPE 0.5 -> 1 on both endpoints (`tools/profundum_preset_defs.h` row 9). Re-read: ratio 1.1218 (step 0.220666, reference 0.196706); AT-09 primary 26.78 / 29.10 dB (bar 3) and AT-13 Fracture secondary pass at E1 and C3, read through `showcaseColumn` in a throwaway diagnostic case. Worst held-load ratio now 1.50655 (Biting Reed) over 54. Tree regenerated (`wrote 53 presets`).
- **G22 gate, T020 `Profundum_PresetRenderGuard` (SC-027) wall clock, triaged to T027 (G23, R-11).** Verbatim: `CHECK( seconds <= kRtGuardSecondsBar )` with `21.2846296 <= 15.0` (21.0356 on the first gate run). Two readings agree, so this is the full library's cost, not load. T020 assigns the `_0of2` / `_1of2` split to T027 when the full library measures above 15 s; it is not done early here, and the bar is not touched.

## T037 — Preset descriptions: read `Comment` back, show it in the browser (FR-065, SC-033; ruled 2026-10-24) — main loop

**Files edited:** `plugins/shared/src/preset/preset_manager.cpp` (`readMetadata`), `plugins/shared/src/ui/preset_browser_view.h`,
`plugins/shared/src/ui/preset_browser_view.cpp`, `plugins/shared/tests/CMakeLists.txt` and one Profundum library test.
**Files created:** shared test file(s) holding `Krate_PresetManagerReadsDescription` and `Krate_PresetBrowserShowsDescription`.
Run-log `T037.txt`.
- Failing tests first (SC-033 a, c), then `Profundum_PresetDescriptions` (SC-033 b) over the generated tree.
- `readMetadata`: bounds-checked chunk-list walk (header 'VST3', version, 32-byte class ID, int64 list offset; 'List', int32 count,
  then {id[4], int64 offset, int64 size}). It reads the `Info` chunk, finds `id="Comment" value="..."` and unescapes the five entities.
  Nothing else is read.
- Browser: a description `CTextLabel` one row high between the list and the button bar. The list loses that height; nothing
  else moves. The label is set in `onPresetSelected` and cleared wherever `selectedPresetIndex_` resets.
- Scoped gate as FR-065 states. Runs at the Phase 13 close together with the other lanes.
