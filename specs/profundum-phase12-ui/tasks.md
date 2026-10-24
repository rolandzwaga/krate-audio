# Tasks: Profundum Phase 12 — UI

**Spec:** `specs/profundum-phase12-ui/spec.md` (Clarifications OQ-1 – OQ-10 and CQ-1 – CQ-8, 2026-10-23) · **Plan:**
`specs/profundum-phase12-ui/plan.md` (§0 – §10, Open Questions 1 – 7, Appendix A) · **Roadmap:** `specs/Profundum-roadmap.md`
Phase 12 (lines 837–1027) and the Phase 11 → 12 handoff 12.1 – 12.4 (lines 827–835) · **Phase base:** `40f6ebca` on
`synth/profundum`
**Status:** Task list (2026-10-23). The build stage has not started. No commit tasks: commits happen outside this workflow.

**Two stages, one task list (FR-001, SC-001).** Groups G1 – G5 are **stage 12a**: they write only under
`specs/profundum-phase12-ui/` (plus the one test-only render TU of T010, and only if the user rules plan Open Question 1 yes).
G5 (T011) is the **ratification gate**: it returns `blocked` until the user has ratified D1 – D7 in the spec's Clarifications.
Groups G6 – G25 are **stage 12b** and run only after the main loop resumes the stage with `unblock: ["T011"]`.

**Deliverables (12b).** Six `const noexcept` accessors on `ProfundumEngine` and five on `ProfundumVoice` (append-only, FR-095);
the plugin-local `ProfundumSpectrumFrame` / `ProfundumSpectrumProducer` (64-partial Goertzel bank, K = 8 periods) and its
DataExchange transport; state v3 (574 bytes, endpoint names, `kMsgEndpointName`); the controller as `VST3EditorDelegate` +
`IDataExchangeReceiver`; `ProfundumPanelSubController`, `HarmonicBarView`, `SpreadCurveView`, `macro_ui_map.h`,
`endpoint_descriptor.h`; `editor.uidesc` replaced wholesale (Main + Advanced, 80 visible IDs bound); 11 new test TUs in
`profundum_tests` (plus T010's hidden render TU), 1 in `dsp_systems_tests`, 2 test-side headers; doc edits. **`plugins/shared/`
is not touched. No parameter is added, removed, retyped or re-ranged (FR-096).** All paths are repo-relative to
`f:/projects/iterum`.

---

## Read this first (every executor)

### Canonical order inside every task

1. Write the failing test **first**, in the named file and `TEST_CASE`, with the exact assertions given. A section that guards
   behaviour an earlier task already built says so and names its **non-vacuity arm** (the control that proves it can fail).
2. Implement the minimum that makes it pass, exactly as the cited plan section pins it.
3. Zero warnings (`warning C[0-9]|warning:` count 0). "Pre-existing" is not a category.
4. Tests pass at the group gate.

**Executors do not build or run tests**; the group gate does, after every group. The gate builds the group's targets and runs
the phase filter `[profundum]` minus the timing tags (`.claude/workflows/profundum-phase.js:554–562`). **Every TEST_CASE of this
phase carries `[profundum]`** (plus its layer / component tags), including the one in `dsp_systems_tests` (T012), so the gate's
tag filter reaches it. Hidden cases (`[.listen]`, `[.][probe]`, `[.perf]`) are written by executors and run by the main loop.

**Gate cost after G6.** T012 modifies two tracked `dsp/` headers. From then on the gate's own rule
(`profundum-phase.js:558–561`) runs the per-push sets of the consuming executables (`dsp_systems_tests`, `profundum_tests`), not
only the `[profundum]` filter. That is the workflow's rule; do not work around it.

Commands for anyone who does run them (Windows, full CMake path; capture to a file once, never re-run a slow suite for output):

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target Profundum profundum_tests dsp_systems_tests -- /nr:false 2>&1 | tee F:/tmp/p12-build.log | tail -5
grep -cE "warning C[0-9]|warning:" F:/tmp/p12-build.log          # must print 0 (SC-021)
build/windows-x64-release/bin/Release/profundum_tests.exe "[profundum]~[long]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "Profundum_EngineDisplayAccessorNeutral" 2>&1 | tail -5
```

Catch2 filters are positional; tags go in brackets. Never `ctest -R profundum_tests`. The post-build copy to
`C:/Program Files/Common Files/VST3/` may fail with a permission error; compilation still succeeded.

### Parallelism

`[P]` tasks inside one group create **only brand-new files**, disjoint from every other task in that group. Every task that edits
a file that already exists — `processor.h/.cpp`, `controller.h/.cpp`, `plugin_ids.h`, `entry.cpp`, `editor.uidesc`,
`profundum_test_fixture.h`, a test TU created by an earlier group (including the G7 placeholders), `spec.md`, any CMake list, any
`dsp/` header — is **alone in its group**. All Profundum test TUs link into the one `profundum_tests` binary.

### Rules for every file of this phase (plan §4, §6 conventions, §8 risks; Phase 10 / 11 rules)

- **Stage fence (FR-001).** Before T011 is unblocked, no task writes outside `specs/profundum-phase12-ui/`, except T010's two
  plugin-test edits and only under a recorded "P-1 yes" ruling.
- **`dsp/` fence (FR-095).** The only `dsp/` edits of the phase are T012's: the accessors of plan §4.1 appended to the public
  sections of `systems/profundum_engine.h` and `systems/profundum_voice.h`, the new TU
  `dsp/tests/unit/systems/profundum_engine_display_test.cpp` and its line in `dsp/tests/CMakeLists.txt`. `bass_stereo_field.h`
  and `profundum_core.h` are **not** modified (plan §0: the controller computes the SPREAD curve). Any other `dsp/` need: STOP AND
  SURFACE (return `blocked` with the reading).
- **`plugins/shared/` fence.** Not touched (FR-095, OQ-3 ruling). The shared view headers (`arc_knob.h`, `bipolar_slider.h`,
  `adsr_display.h`, …) are included, never edited.
- Never `std::isnan` / `std::isinf` / `std::isfinite` / `std::fpclassify`. Use `Krate::DSP::detail::isFinite`
  (`dsp/include/krate/dsp/core/db_utils.h`) or an explicit bit-pattern test. Non-finite test values are built from bit patterns
  through a `volatile` (R-5).
- No brace-init narrowing (R-10). `ProfundumMacroValues`, `BassStereoField::Controls` and frame fields are filled by
  copy-then-assign or designated initialisers; never `{double}` into a `float`.
- Plugin types are spelled `::Profundum::…` and `::Profundum::UI::…`; the test namespace is `ProfundumPluginTest`.
- Every `Processor`, `Controller`, `ProfundumEngine` and `VST3Editor` in a test lives on the heap.
- **No bit-exact float golden across binaries.** Every equality is same-binary, same-process: two live renders, or a value against
  an independent double-precision computation with a stated tolerance. `memcmp` of two streams or two renders of one process is
  allowed.
- **New UI names live in `Profundum` or `Profundum::UI`.** No anonymous-namespace `Rect`, `Point`, `Style` or `MAC` (R-9; the macOS
  `MacTypes` hazard). `tools/lint-apple-globals.js` runs in T037.
- **ODR.** The claimed names are those of plan §9 plus `Profundum_SpectrumProducerMath`, `Profundum_UiViewsUnit` and
  `Profundum_MacroUiMap` (checked by T001). Any other new type or free-function name: run `grep -rnw <Name> dsp/ plugins/ tools/
  tests/` first and record the 0-hit result in the run log. Never `HarmonicFrame` (`dsp/include/krate/dsp/processors/harmonic_types.h:54`),
  never a roadmap hazard name (`CharacterProcessor`, `SubOscillator`, `StereoField`, `MorphEngine`, `MorphNode`,
  `TransientDetector`, `HarmonicCloud`, `SpectralState`, `MacroMapper`).
- **Real-time safety (FR-071, SC-012).** Nothing reachable from `process()` allocates, locks, throws or does I/O. The names mutex
  (plan §4.4) is never taken on the audio thread.
- **Ratified values.** Every value plan §3.1 *proposes* (designed size 960 × 600, the D7 minima and colours, the 300 ms bar decay,
  the descriptor table, the tooltip strings, the 100 ms sync period, no image-mode mirror) is built **as ratified in the design
  document the task names**. The plan's value is the expected default. If the ratified document differs, build the ratified value
  and record "ratified value used: <doc §, value>" in the task notes.
- Display buffers are `char8 text[64]`. Parameter titles, units and list strings are unchanged (FR-036).

### Precondition table (plan items the tasks build at the plan's reading)

| Item | What the tasks build | If ruled otherwise | Task(s) |
|---|---|---|---|
| **Plan OQ-1 / P-1 (BLOCKING for T010)** | A hidden, test-only TU `integration/main_page_render_set_test.cpp` (`[.listen]`) and its lines in `tests/CMakeLists.txt` land **before** ratification, as an FR-001 exception the user rules into the FR-001 row; the same ruling covers running Phase 9's `evidence/close/listen.sh` | "No": T010 returns `blocked` and is re-cut; FR-006's render set needs another mechanism the user names | T001, T010 |
| **Plan OQ-2 / P-2 (checked by T011)** | SC-004's row amended to cover the `kStateV2Bytes` → `kStateV3Bytes` length assertions on `getState` results (plan §2 P-2); T019 runs the P-2 audit | If the user keeps SC-004 as written, T019's audit edits contradict it: T011 returns `blocked` | T011, T019 |
| Plan OQ-3 / P-5 | **RULED 2026-10-23: one-sided ring**, plugin-local `OffsetRingKnob` (`ArcKnob` subclass in `plugins/profundum/src/ui/`) drawing from the value to value + offset; FR-061 row carries it. Re-cut below: T016 builds the view, T023 feeds it, T027 binds it, T029 tests it | — | T011, T016, T023, T027, T029 |
| Plan OQ-4 | Copy A → B / B → A copies the sound only, **not** the name (plan §4.5; `applyPendingCopy`, `processor.cpp:647` per plan) | Copying names adds a name write to the copy path: T019 / T028 re-cut | T011, T019, T028 |
| **Plan OQ-5 (the ratification)** | The §3.1 proposals, as ratified in D1 / D2 / D7 | Built as ratified (rule "Ratified values" above) | T011 → every 12b task |
| **Plan OQ-6 / P-8 (checked by T011)** | FR-032's row amended: "a display-only `CParamDisplay` bound to a continuous ID is allowed when the same page also binds that ID to a conforming writing control" | Without the ruling the 105 / 412 readouts violate FR-032: T011 returns `blocked` | T011, T027 |
| Plan OQ-7 / P-9 | **No** header preset field in 12b (Appendix A not built) | A "yes" needs a new FR / SC row first, then Appendix A becomes a task: T011 returns `blocked` "re-cut: add the preset-field task" | T011 |
| Plan P-3 | SC-030 counts windows, not Hz; the C3 cadence is 16.35 Hz (plan §5.4) | — (spec text correction only; T009 lists it for the user) | T009 |
| Consumer `[long]` scope (Deviation 6) | The close lane runs the **per-push** sets of the consumers of the two modified `dsp/` headers (FR-095), not their 46 `[long]` cases | If the user wants them, T036 gains a sharded lane over the 46 listed names | T009, T036 |

### Deviations from the plan made while cutting these tasks (surface them; do not silently revert)

1. **Plugin `.cpp` and test-TU registration comes early (G8, T018); the last group's single CMake task (T034) lists the new
   plugin headers and audits every list.** CMake refuses a source list that names a missing file, a test cannot fail before its TU
   is compiled, and the plugin target `Profundum` stops linking as soon as `controller.cpp` constructs a `HarmonicBarView` whose
   `.cpp` is not in `plugins/profundum/CMakeLists.txt` ("Every new .cpp must appear in BOTH lists", `plugins/profundum/CMakeLists.txt:19–21`).
   Phase 11 deviation 1 is the precedent. The `dsp/` TU is registered by T012 itself (alone in its group), for the same reason.
2. **One `TEST_CASE` per name, one TU per case.** Plan §6.1 lists `Profundum_SpectrumFrameSanitise`, `Profundum_SpectrumFrameCadence`
   and `Profundum_EndpointNames` in two TUs each; Catch2 rejects a duplicate test-case name. The SC-named cases live in one TU each
   (`integration/spectrum_frame_test.cpp`, `unit/state_v3_test.cpp`); the producer's arithmetic is gated by the extra case
   `Profundum_SpectrumProducerMath` in `unit/spectrum_producer_test.cpp` (T013).
3. **One extra TU, `unit/controller/ui_views_test.cpp`, with `Profundum_UiViewsUnit`** (T016), and one extra case
   `Profundum_MacroUiMap` (T015): the new views and the constexpr map are new files whose behaviour needs a test in the same `[P]`
   task. No SC moves: SC-026 step 3, SC-016, SC-014 and SC-009 still run in their named cases.
4. **`SpreadCurveModel` is declared in `src/ui/spread_curve_view.h`** (plan §4.6.8 shows it in the controller section). The view
   draws it and the controller fills it; declaring it with the view lets T016 create the view without a controller dependency.
5. **SC-007, SC-014, SC-024, SC-027 and SC-028 are written across tasks** as the paths they need appear: controller arms first
   (T019, T022, T023, T024, T025), built-tree / UI arms after the uidesc exists (T028, T029, T030).
6. **Consumer `[long]` cases.** Plan §7.3 lane 3 runs the `[long]` cases of the Profundum `dsp/` TUs that include the two
   modified headers. FR-095 says "the full **per-push** suites of every consumer", and the 2026-10-18 rule runs `[long]` cases of
   the phase's own TUs only. The accessors are proven output-neutral by SC-022 (max |Δ| = 0). T036 therefore runs the consumers'
   per-push sets and **lists** the consumer `[long]` names (46 at `40f6ebca`) without running them; surfaced to the user by T009.
7. **Placement / label checks read the uidesc XML; binding / class / visibility checks read the built tree.** `uidesc-label` is a
   parse-time attribute (the Vorago panel reads it in `verifyView` from `UIAttributes`,
   `plugins/vorago/src/ui/panel_sub_controller.cpp:28, :61`), not a property of a built `CView`, so the container checks of SC-005,
   SC-019 and SC-031 use an XML scan copied from Vorago's `XmlNode` / `XmlScan` / `scanUidesc`
   (`plugins/vorago/tests/unit/controller/editor_layout_test.cpp:324, :338, :391`), as Vorago's own layout test does.

### Verified API surface these tasks rely on (read this session at `40f6ebca`)

| Symbol | File:line |
|---|---|
| `kCurrentStateVersion = 2`, `kStateV1Version = 1`, `kStateV2Bytes = 510`; `static_assert(kCurrentStateVersion == 2 && …)` | `plugins/profundum/src/plugin_ids.h:22, :31, :38, :48–49` |
| `kMsgCopyEndpoint`, `kMsgCompare`, `kMsgEndpoint`, `kMsgCompareMode`, `kMsgRequestSync` | `plugin_ids.h:70–74` |
| Parameter IDs 0–7, 100–105, 200–208 (`kCoreDepthId` … `kCoreExcitationAsymmetryId`), 300–306, 400–403, 410–417, 420–426, 500–501, 600–602, 700–701, 800–809, 900– | `plugin_ids.h:100–208` |
| `Processor::setState` v1 branch `if (version < kCurrentStateVersion)` | `plugins/profundum/src/processor/processor.cpp:504` |
| `Controller::setComponentState` v2 branch `else if (version >= kCurrentStateVersion)` | `plugins/profundum/src/controller/controller.cpp:117` |
| `ProfundumEngine`: `public:` :96, `processBlock` advancing `pos_` / `gridPhase_` :399–405, `oversamplerStateFinite()` :422, `private:` :424, reset of `pos_` / `gridPhase_` / `monoActiveNote_` / `rendering_` :623–628, MONO / POLY note-on :768–784 | `dsp/include/krate/dsp/systems/profundum_engine.h` |
| `ProfundumVoice`: `currentCoreF0()` :693, `stateFinite()` :695–698, `private:` :700, `k_ = 0` in `beginInterval` :379, `mainL_` / `anchorL_` writes :864 / :871, observer taps `.charL = tap(mainL_)` :941 and `.anchorL = tap(anchorL_)` :943, scratch arrays :991, `std::size_t k_` :1053 | `dsp/include/krate/dsp/systems/profundum_voice.h` |
| `enum class ProfundumMacro`, `enum class ProfundumMacroTarget` (16 targets in Weight / DarkBright / TightLoose / CleanViolent order) | `dsp/include/krate/dsp/systems/profundum_macro_matrix.h:37, :40–47` |
| Consumers of `profundum_engine.h` / `profundum_voice.h` (`grep -rlE`): 13 Profundum TUs + `mass_dynamics_oversampling_test.cpp` under `dsp/tests/unit/systems/`, and `plugins/profundum/` only | this session's grep |
| `dsp/tests/CMakeLists.txt` Profundum Phase 9 block (comment + enumerated TUs, last `mass_dynamics_oversampling_test.cpp`) | `dsp/tests/CMakeLists.txt:787–816` |
| `profundum_tests` source list, SECOND-compilation block, `-fno-fast-math` block, `krate_test_pch` last | `plugins/profundum/tests/CMakeLists.txt` (read whole) |
| Plugin source list `smtg_add_vst3plugin(...)` | `plugins/profundum/CMakeLists.txt:23–64` |
| `Profundum_EditorLifecycle` sections `HarnessCycles` :151 (3 cycles), `EditorBindsSurface` :163 (`controlCount == 8u` :199), `CreateViewNames` :205; headless editor = `new VST3Editor(controller, "editor", path)` + `attached(nullptr, nativePlatformType())` :169–172; `collectBoundControls` :71–91 (at 40f6ebca; corrected 2026-10-24, T001 M3) | `plugins/profundum/tests/unit/controller/editor_lifecycle_test.cpp` |
| `Profundum_ProcessorCpu` `[profundum][.perf][performance]` :214; `kMonoPctBar = 8.0` :71, `kPolyPctBar = 25.0` :72 | `plugins/profundum/tests/integration/processor_cpu_test.cpp` |
| Fixture: `CountingConnectionPoint` :220, `RecordingConnectionPoint::hold` :263 / `releaseHeld()` :308, `RecordingComponentHandler` :379, `ControllerFixture` :456, `ProcessorFixture` :508, `ConnectedPair` :755, `kV2GlobalsAt` :982 … `kV2SeedAt` :986, `writeV2Stream` :1007, `withEndpoints` :1043, `v1Stream` :1062, `withFields` :1091 | `plugins/profundum/tests/profundum_test_fixture.h` |
| `kStateV2Bytes` sites: 36 lines in 7 files (`state_v2_test.cpp` 26, `state_roundtrip_test.cpp` 4, fixture 2, `determinism_test.cpp`, `state_restore_render_test.cpp`, `lifecycle_test.cpp`, `phase11_alloc_test.cpp` 1 each) | this session's grep |
| Vorago producer `connect` / `disconnect` (block size rounded up to 32, `numBlocks = 4`, `onConnect(other, getHostContext())`) | `plugins/vorago/src/processor/processor.cpp:262–303` |
| Vorago consumer `queueOpened` / `queueClosed` / `onDataExchangeBlocksReceived` / `notify` → `dataExchangeReceiver_.onMessage` | `plugins/vorago/src/controller/controller.cpp:303–339` |
| Vorago `kQueueMayBeUnavailable` / `queueAvailable(opened, step)` | `plugins/vorago/tests/integration/ecosystem_frame_test.cpp:108–131` |
| Phase 9 handoff 12.1 (16 macro rows), 12.2 (AT-03 / AT-10 / AT-26 smoke; `audibility-table.csv` 174 rows), 12.3 (8 listening rows), 12.4 (Bipolar ENV rest) | `specs/profundum-phase9-voice-engine/evidence/phase-handoffs.txt:155–210` |
| Engine test helpers: E1 = 28 :89, C3 = 48 :91, `VoiceEngineScript` :139, `VoiceEngineObserver` :239, `voiceEngineIssue` :342, `voiceEngineRender` :377 | `dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h` |
| Plugin `CHANGELOG.md` `## [0.1.0] - Unreleased` (open entry, version.json 0.1.0) | `plugins/profundum/CHANGELOG.md:8`, `plugins/profundum/version.json` |

Every other `file:line` in a task is quoted from plan §1 – §6, which its author read at `40f6ebca`. Each executor re-opens the
lines its task cites before editing; a mismatch is a `blocked` report naming the row.

---

## Group and task map

| Group | Tasks | Theme |
|---|---|---|
| G1 | T001 | Preflight: branch, base, scope, ruling census, ODR sweep |
| G2 | T002 – T008 [P] | **12a** design documents D1 – D7 (`design/*.md`) |
| G3 | T009 | **12a** spec.md Clarifications scaffolding (FR-097 table, ratification checklist, open plan questions) |
| G4 | T010 | **12a** FR-006 render-set TU (P-1-gated) + its CMake lines; listen script (main loop) |
| G5 | T011 | **12a ratification gate** — STOP AND SURFACE until SC-001 holds |
| G6 | T012 | **12b** `dsp/` display accessors + `Profundum_EngineDisplayAccessorNeutral` (SC-022 engine arm) |
| G7 | T013 – T017 [P] | New files: frame + producer; names + descriptor; macro map; views; placeholders, panel stub, test UI helper |
| G8 | T018 | Early CMake registration (plugin `.cpp`s, test TUs, `-fno-fast-math`) |
| G9 | T019 | State v3 + endpoint names + P-2 audit (SC-004, SC-027 (a) API arm, (b), (c), (e)) |
| G10 | T020 | Processor spectrum producer, sub-slicing, seam arms (SC-010 – SC-012, SC-025, SC-026, SC-030 seam) |
| G11 | T021 | Controller DataExchange consumer + connected arms + SC-023 |
| G12 | T022 | Compare read path, `pressCompare` (SC-007 (a)(b)(c)(e) controller arms) |
| G13 | T023 | Macro rings + endpoint captions (SC-028 controller arm, SC-027 (f)) |
| G14 | T024 | SPREAD model (SC-014 model arm) |
| G15 | T025 | `ADSRDisplay` adapter (SC-024 adapter arm) |
| G16 | T026 | Editor delegate, panel sub-controller, sync timer, zoom, view-creator includes, `entry.cpp` banner (SC-015) |
| G17 | T027 | `editor.uidesc` replaced + layout / label / placement / zoom / lifecycle (SC-002 – SC-005, SC-016, SC-017, SC-019, SC-031, SC-032) |
| G18 | T028 | Morph component UI arms, dimming, portamento readout, name commit (SC-007 UI, SC-008, SC-018, SC-027 (a) UI arm) |
| G19 | T029 | Macro highlight, rings on the built tree, MIDI learn (SC-009, SC-028 knob arm, SC-029) |
| G20 | T030 | Built-tree arms of SC-014 (view) and SC-024 (wired display) |
| G21 | T031 | `Profundum_MainPageSanity` (SC-006 (c)) |
| G22 | T032 | SC-013 producer-on CPU arms (written here; run only by T038) |
| G23 | T033 | Docs: plugin `CLAUDE.md`, `CHANGELOG.md`; D3 / D4 regeneration script |
| G24 | T034 | CMake registration (single task: header lists + audit) |
| G25 | T035 [P] – T038 [P] | Scope fence + ODR re-sweep; suites lane (main loop); portability; CPU lane alone (main loop) |

**Run-log convention.** Every task writes the commands it ran (or the checks it performed) and their verbatim last lines to
`specs/profundum-phase12-ui/evidence/run-log/T0NN.txt`. Compliance quotes those files, never memory.

---

# STAGE 12a — DESIGN (spec folder only; FR-001)

# GROUP 1 — Preflight

## T001 — Branch, base, scope, ruling census, ODR sweep (no code)

**Files created:** `specs/profundum-phase12-ui/evidence/run-log/T001.txt`.

1. `git branch --show-current` prints `synth/profundum`; `git merge-base --is-ancestor 40f6ebca HEAD` exits 0;
   `git status --porcelain` lists only ` M .serena/project.yml` and paths under `specs/profundum-phase12-ui/`. Anything else: STOP
   and return `blocked`.
2. **Scope baseline.** `git diff --name-only 40f6ebca -- dsp/ plugins/ tests/` prints nothing.
3. **Ruling census (non-blocking here; T010 and T011 enforce).** For each of plan §10 Open Questions 1 – 7, search `spec.md`
   (Clarifications and the owning row: FR-001 for OQ-1, SC-004 for OQ-2, FR-061 for OQ-3, FR-047 for OQ-4, SC-001 for OQ-5, FR-032
   for OQ-6, a new FR for OQ-7) and record one line per question: `OQ-n: RULED <quote + line>` or `OQ-n: OPEN`.
4. **ODR sweep** (plan §9). Each name prints 0 lines with `grep -rnw <Name> dsp/ plugins/ tools/ tests/`:
   `ProfundumSpectrumProducer ProfundumSpectrumFrame kProfundumFrameUserContextId HarmonicBarView SpreadCurveView
   ProfundumPanelSubController SpreadCurveModel kMacroTargetParamIds describeEndpoint kMsgEndpointName kEndpointNameMaxBytes
   kEndpointNameBlockBytes kStateNamesOffset publishSpectrumFrame kEditorSyncIntervalMs hasEditorSyncTimerForTest
   SpectrumQueueState spectrumQueueOpenForTest v2StreamOf portaReadoutInvalidationsForTest kneeMarksForTest lowerKneeHz upperKneeHz
   kD6UiOnlyTags controlGridPhase voiceForDisplay voiceMidiNote voiceRenderedThisInterval appliedVoiceMode characterTapL
   characterTapR anchorTapL anchorTapR intervalSamplesRendered EndpointName sanitizeEndpointName defaultEndpointName
   writeEndpointName readEndpointName HeadlessEditor Profundum_SpectrumProducerMath Profundum_UiViewsUnit Profundum_MacroUiMap`.
   `kStateV3Bytes` hits only `namespace Vorago` (`plugins/vorago/src/plugin_ids.h` and its tests; plan §9). A hit anywhere else:
   `blocked`.
5. Re-read every row of "Verified API surface" above and confirm it; a mismatch is a `blocked` report naming the row.

**Verify.** `T001.txt` holds every command and its output. No source file changes.

---

# GROUP 2 — 12a design documents (all [P]; each task creates exactly one new file under `specs/profundum-phase12-ui/design/`)

Common rules for T002 – T008 (FR-002):
- Markdown, text / ASCII wireframes (or inline SVG), kept in the repo. **Every control in every wireframe is labelled with its
  parameter ID or with "UI-only (session tag N)"**, using the session-tag table of plan §4.7.
- Each document opens with `**Status:** PROPOSED (plan §3) — awaiting user ratification (SC-001)`. No document says "ratified";
  only the user's Clarifications entry ratifies.
- Every statement about engine or plugin behaviour cites `file:line` (copied from the spec / plan citation and re-opened to
  confirm).
- These tasks write no code and no test. **Verified by:** the T011 review (presence and content checks named per task).

## T002 [P] — D1 `design/main-page.md` (FR-003, FR-006, FR-011, FR-012, FR-020, FR-022, FR-080; plan §3.1 "Main page (D1)", §3.3)

**Files created:** `specs/profundum-phase12-ui/design/main-page.md`.

Content, in this order:
1. **Designed size**: 960 × 600 px at 100 %; zoom steps 100 / 125 / 150 / 200 % (FR-014; plan §3.1).
2. **Wireframe** (ASCII) of plan §3.1: header (title PROFUNDUM, voice mode 1 two-segment, glide 412 `ArcKnob` + display-only
   `CParamDisplay` 412 readout whose unit follows 414, page strip UI-only 9100, zoom UI-only 9300); upper row **THE MASS**
   (`uidesc-label="mass-group"`): `HarmonicBarView` #1 (`bar-variant="main"`) with the ANCHOR lane; `ArcKnob`s Depth 200, Body 201,
   Edge 204, Fold 500, Mass 104, Drop 400; macros Weight 100 (`ArcKnob`), Dark ↔ Bright 101 and Clean ↔ Violent 103
   (`BipolarSlider`, neutral 0.5); Motion source 900 (four-segment). Lower row `morph-anchor` holding exactly two children:
   `anchor-group` (level 302 `ArcKnob`, character 300 `COptionMenu`, the text MORPH BEHAVIOUR and the 1000-bound three-segment)
   and `morph-band` (A name 9210 + caption 9220, the 105 `CSlider` + the 105 readout, B name 9211 + caption 9221, compare 9200,
   Copy 9201 / 9202, Edit Target 1001 two-segment).
3. **Control table**: one row per Main control: ID or session tag, class, label text, tooltip text. The bound-ID set equals the
   FR-011 set exactly: {1, 100, 101, 103, 104, 105, 200, 201, 204, 300, 302, 400, 412, 500, 900, 1000, 1001} (17 IDs). State
   "205, 102 and 301 are not on the Main page".
4. **FR-022 container rule** verbatim from plan §3.1 ("the nearest ancestor of a 105-bound control that carries
   `uidesc-label="morph-anchor"` …").
5. **Image-mode mirror on Main: no**, with the plan's reason (FR-080; SC-031 (a) follows this line).
6. **Forbidden-word ruling (FR-020)**: "No Main-page label or tooltip contains oscillator, filter, LFO or partial; no word is ruled
   in." (If the user rules a word in at ratification, it is listed here.)
7. **Concept §14 delta table** (FR-003): exactly the 11 rows of plan §3.1 (BASS title, spectrum centrepiece, the Weight, Body,
   Edge, Mass, Anchor, Shift, Fold and Drop knobs, the CLEAN ─ MORPH ─ VIOLENT bar), each with verdict, where in D1, and reason;
   source `specs/Profundum-concept.md:647–678`.
8. **FR-006 render set** (plan §3.3): base = the default patch with every Advanced-only ID at its registered default; one render
   per Main control at its registered min, default and max (Edit Target 1001 and voice mode 1 at both list values instead);
   four combined "patch" renders, each listing every Main control's value (proposal: *Deep sub* — Weight 0.8, Mass 30, Fold 0,
   Clean ↔ Violent 0.3; *Round bass* — Body 0.7, Edge 0.3, Mass 50; *Growl* — Edge 0.7, Fold 60, Clean ↔ Violent 0.8, Mass 70;
   *Morph and motion* — Bass Morph 105 at 0.5, Motion 900 = Cycle); notes E1 (MIDI 28) and C3 (MIDI 48), 48 kHz, 3 s held + 1 s
   tail. T010 and T031 read this list.

## T003 [P] — D2 `design/advanced-page.md` (FR-004, FR-013, FR-024, FR-025, FR-044, FR-048, FR-050 – FR-052, FR-070, FR-073, FR-074, FR-076, FR-080; plan §3.1, §4.3.2, §4.6.8, §4.9, §4.12)

**Files created:** `specs/profundum-phase12-ui/design/advanced-page.md`.

Content:
1. **Sections** exactly as FR-004, each with its container label: `section-core-spectrum` (200, 201, 202, 203, 204, 205, 500, 102,
   `HarmonicBarView` #2 `bar-variant="core"`), `section-sub-anchor` (302, 300, 301 under the text ANCHOR MODE, 303, 304, 305, 306,
   1000, the "Held at A" label), `section-excitation-fold` (207, 206, 208, 500), `section-shape-character` (501, 600, 601, 602),
   `section-mass` (700, 701), `section-pitch-envelopes` (400, 401, 402, 403, the `ADSRDisplay` for 420 – 426 plus `ArcKnob`s 420 –
   426, 410, 411, 412, 413, 414, 415, 416, 417), `section-stereo-output` (800, the `SpreadCurveView`, 801 – 809, 0, 2, 3),
   `section-morph-behaviour` (1000, 1002, compare 9203, Copy 9204 / 9205), and the MODULATION tab `tab-modulation` (900 – 914).
   Tabs `tab-sections`, `tab-modulation`, `tab-settings` inside `page-advanced` (FR-013); tab strip UI-only 9110; SETTINGS holds zoom
   9300 and the note "No host run loop: the spectrum view stays empty" (FR-076). Fold 500 and Anchor Morph Mode 1000 are bound in
   both sections FR-004 names.
2. **Wireframe** per section; every control labelled with ID / session tag and class (plan §4.12 class table).
3. **Spectrum drawing** (plan §3.1): vertical dB re the frame's strongest component (Anchor included), 0 … −60 dB; horizontal
   log₂ n for n = 1 … 64; shading Sub (n = 1), Body (2 – 8), Presence (9 – 64); Anchor bar in its own lane labelled ANCHOR;
   **silence: linear decay of drawn height to 0 over 300 ms** (the value SC-026 measures); pitch-unstable frames drawn normally;
   the bars show harmonic structure measured before the amp envelope (plan P-7); the SPREAD curve, rings and dimming describe the
   Edit-Target endpoint, the spectrum the resolved, modulated signal (plan P-6).
4. **Focus rule (FR-074)** quoted verbatim from plan §4.3.2 (the boxed sentence and the MONO / POLY bullets).
5. **Tooltip strings (exact UTF-8 literals; SC-005 compares them):**
   - 809 contains `Synced rates above 1 Hz fold into 0.5–1 Hz.`
   - 911 contains `Bipolar ENV rests at −amount between notes; Unipolar rests at the set value.`
   - each of 800 – 809 contains the mono guarantee `Width is pan-only and constant-power; the generator's sub band stays at ρ ≥ 0.9989. Gravity 0 still attenuates side content below 2·f_c by −29 dB.`
   - each of 600 – 602 contains the WIDE caveat `In WIDE the sub-band correlation depends on the Character offset pair.`
   - the Tight ↔ Loose 102 tooltip names `Compression` (FR-060: CompressionAmount has no control).
   The full tooltip of every Advanced control is listed (one table), so T027 copies, never composes.
6. **SPREAD curve** (FR-080; plan §4.6.8, §4.9): w(f) = c(f)·m on 64 log points over 20 Hz – 10 kHz, vertical 0 (MONO) … 1
   (WIDE); knee marks `lowerKneeHz = f_c·2^(−kLowerKneeMaxOctaves·(1 − G))` and `upperKneeHz = 4·f_c`; Motion excursion band **not
   drawn**; in WIDE the label `WIDE: Character offsets not drawn`.
7. **Endpoint descriptor table** (FR-048) exactly as plan §3.1 (Shape s = 0.5·(1 − depth) + 0.5·body → Round / Full / Dense;
   Colour s = 0.5·edge + 0.25·(shift + 1) → Dark / Warm / Bright; Force s = max(foldAmount / 100, characterAmount,
   excitation.mix · excitation.drive) → Clean / Driven / Aggressive; thresholds 0.33 / 0.66, each belonging to the upper band),
   with the `MorphState` field path of each input (re-read `dsp/include/krate/dsp/systems/morph_state.h`; cite lines), plus **six
   worked cases** for SC-027 (d): `MorphState{}`; Phase 8 showcase A and B (cite their definition in `dsp/tests/unit/systems/`);
   three constructed states with one score at exactly 0.33, exactly 0.66 and 0.6599 — each with its three expected words.
8. **Lock dimming (FR-044):** dimmed alpha 0.45, label `Held at A`, shown iff 1000 = Hold and 1001 = B; controls stay editable.
9. **Mono-compatibility statement** (FR-024): the controls that affect it (800 – 809, and 600 – 602 in WIDE), and the sentence
   "There is no low-end protection switch."

## T004 [P] — D3 `design/macro-mapping.md` (FR-005, FR-006, FR-021, FR-060, FR-061; plan §3.2, §4.6.5, §5.5)

**Files created:** `specs/profundum-phase12-ui/design/macro-mapping.md`.

1. The 16-row table of Phase 9 handoff 12.1 (`specs/profundum-phase9-voice-engine/evidence/phase-handoffs.txt:160–178`), drafted
   from `profundum_macro_matrix.h:75–92` (`kRows`), between `<!-- GENERATED-BEGIN macro-rows -->` and `<!-- GENERATED-END -->`;
   columns row, macro, target, amount, curve, unit, **registered ID** (plan §4.10 order: 200, 201, 302, 204, 205, 501, 602, 420,
   421, 423, 700, —, 206, 207, 500, 601), and flags `no control` (row 11 CompressionAmount) and `inert` (row 5 DarkBright → Shape,
   amount 0). One sentence: "T033's probe regenerates this block in 12b."
2. "Macro offsets act on the resolved state and **do not move the knobs** (`apply(MorphState&)`, `profundum_macro_matrix.h:204`);
   they are drawn as a one-sided ring from the knob's value toward value + offset (FR-061, P-5 ruling 2026-10-23)."
3. **Highlight sets** (FR-060): Weight {200, 201, 302}; Dark ↔ Bright {204, 205, 602} (501 excluded, amount 0); Tight ↔ Loose
   {420, 421, 423, 700} (Compression named in the macro's tooltip); Clean ↔ Violent {206, 207, 500, 601}.
4. **Ring arithmetic** (FR-061): plan §4.6.5 steps 1 – 4 and the log-time hand check of plan §5.5.
5. **SC-006 (a) audibility column**: each of the 17 Main IDs of T002 item 3 with its gated PASS row in
   `specs/profundum-phase9-voice-engine/evidence/audibility-table.csv` (AT-03 Fold, AT-10 Anchor level, AT-26 Mass given by plan
   §3.2; grep the others by contrast text) or a `Profundum_ParamPathAudibility` section name
   (`plugins/profundum/tests/integration/param_path_audibility_test.cpp`). A control with no gated row is written
   `NO GATED ROW — defect, rule before 12b`; nothing is invented.

## T005 [P] — D4 `design/morph-laws.md` (FR-007; plan §3.2)

**Files created:** `specs/profundum-phase12-ui/design/morph-laws.md`.

1. The four laws (linear, logarithmic, equal-power, stepped) in user words; the 0.52 / 0.48 stepped hysteresis
   (`bass_morph_engine.h:70–71`); the Anchor curves Lock c = 0, Blend c = p, Punch c = p³ (`:72`, `:188`), shown with the
   registered labels Hold / Blend / Yield (FR-036); Morph Ramp Time 1002 as "Morph smoothing" (its registered range and default,
   read from `plugins/profundum/src/parameters/morph_params.h`); "there is no user-selectable global curve".
2. The 62-row `kLawTable` (`morph_state.h:142–317`) between `<!-- GENERATED-BEGIN law-table -->` / `<!-- GENERATED-END -->`: leaf,
   law, axis, lo, hi, registered ID (`kSoundRows` / `kParamRoutes`, `plugins/profundum/src/parameters/sound_param_table.h`).
   Drafted from the headers; regenerated by T033's probe.

## T006 [P] — D5 `design/voice-note.md` (FR-008, FR-023; plan §3.5)

**Files created:** `specs/profundum-phase12-ui/design/voice-note.md`.

The four sections of plan §3.5 D5: stealing and retrigger, including the allocator's same-note retrigger
(`dsp/include/krate/dsp/systems/voice_allocator.h:237–242`); the POLY steal order (oldest released, then oldest active); the Note
Memory modes of 415 quoted as the registered strings (read `plugins/profundum/src/parameters/note_params.h`); a pointer to D2's
focus rule. Every statement cites its Phase 4 / Phase 9 source line. The document adds no behaviour.

## T007 [P] — D6 `design/interaction-automation.md` (FR-009, FR-041 – FR-043, FR-047, FR-049, FR-098; plan §3.5)

**Files created:** `specs/profundum-phase12-ui/design/interaction-automation.md`.

1. **Control classification** table: every control of D1 and D2 as `host parameter (ID)` or `UI-only (session tag)`. The UI-only
   rows are exactly plan §4.7's: 9100, 9110, 9200, 9201, 9202, 9203, 9204, 9205, 9210, 9211, 9220, 9221, 9300 (T027 copies this
   list into `kD6UiOnlyTags`).
2. Sections 2 – 5 of plan §3.5 D6: compare A or B sends one `kMsgCompare` and one begin / perform / end on 1001; A·B sends
   `kMsgCompare` only; touching 105 never changes compare (FR-049); Edit Target 1001 registered and non-automatable; macros
   100 – 103 and 105 automatable; host-level MIDI learn only, no `IMidiLearn`, `IMidiMapping` unchanged (FR-098); page, tab, zoom
   and highlight are session state, never persisted; endpoint names persisted in state v3 (FR-047); Copy copies the sound, not the
   name (plan OQ-4 default).

## T008 [P] — D7 `design/accessibility-scaling.md` (FR-010, FR-014, FR-076; plan §3.1 "D7 values", §5.6)

**Files created:** `specs/profundum-phase12-ui/design/accessibility-scaling.md`.

The values SC-019 gates, as named literals: designed size 960 × 600; zoom steps 1.0 / 1.25 / 1.5 / 2.0; no free resize
(`minSize = maxSize`); minimum font 10 px at 100 %; minimum hit target 20 × 20 px, segment cells 24 × 24 px; text contrast
≥ 4.5 : 1 (WCAG 2.x relative luminance, plan §5.6), ≥ 3 : 1 for bold text ≥ 14 px; every bound control has a tooltip; the colour
tokens `bg`, `panel`, `text`, `text-dim`, `accent`, `highlight`, `anchor`, `mass`, `track` with proposed `#RRGGBB` values, and the
computed ratio of every text-on-background pair the pages use (each ≥ its bound); the "no run loop → empty spectrum" note.

---

# GROUP 3 — Clarifications scaffolding

## T009 — `spec.md` Clarifications scaffolding (FR-097, SC-001; plan §2, §3.4, §10)

**Files edited:** `specs/profundum-phase12-ui/spec.md` (Clarifications section only).

Append under Clarifications, after the 2026-10-23 session:
1. `### Phase 9 listening verdicts (FR-097)` — exactly the 8-row table of plan §3.4 (H-1, H-2, H-8, H-10, H-14, H-19, H-28(1),
   H-28(2), with their `listening-sheet.md` row and verdict-line numbers); every status cell `pending`.
2. `### 12a ratification checklist (SC-001)` — one line per item, each ending `— user: ____ (date: ____)`: D1 with its §14 delta
   table; D2; D3; D4; D5; D6; D7; the FR-006 render-set listening verdict (SC-006 (b)); the FR-097 account; the D2-ruled strings and
   values (tooltips, descriptor table, 300 ms decay).
3. `### Plan questions awaiting a ruling` — plan §10 Open Questions 1 – 7 verbatim, each ending `— ruling: ____`; plus P-3 ("FR-075
   says about 33 Hz at C3; the cadence is 16.35 Hz (plan §5.4); SC-030 counts windows and is unaffected — text correction awaiting
   acknowledgment") and tasks.md Deviation 6 ("consumer `[long]` cases of the two modified `dsp/` headers are listed, not run —
   ruling: ____").

**Rules.** The agent writes `pending` and blanks only; it never writes "given", a verdict, a ruling or a date (FR-097, SC-001). No
FR / SC row text changes.

**Verify.** `T009.txt`: the new table has 8 `pending` rows; `git diff --stat` shows only `spec.md`.

---

# GROUP 4 — FR-006 render set (P-1-gated)

## T010 — `Profundum_MainPageRenderSet` (`[.listen]`) + its CMake lines + listen script — STOP AND SURFACE unless P-1 is ruled yes

**Re-cut 2026-10-23 (user ruling, spec FR-006 and FR-050 rows; design/main-page.md §8.4 "follow all recommendations").** The case
already exists (first run: 106 WAVs, render.log). Extend it, keeping every existing render and file name:
1. **B endpoint for the morph renders.** The renders of 105 (min / def / max), 1000 (every value) and 1001 (both values), and the
   *Morph and motion* patch, first write 1001 = B, then B's values from the *Growl* column (Edge 70 %, Fold 60, Mass 70) through
   their registered laws, then 1001 = A, then the rendered control's value (105 = 50 % for the patch). File names unchanged.
2. **Motion audible in *Morph and motion*.** That patch also sets Motion Amount 902 through its registered law to the D1 §8.3
   Motion value (or 1.0 if D1 gives none), so its Cycle source is audible; 902 stays off the Main page (FR-050). Record 902 in the
   manifest line.
3. **Legato pairs.** Add one render each for 412 at min and at max, and for Voice Mode 1 at both values: E1 held 1.5 s, C3 note-on
   at 1.0 s overlapping, both released at 3.0 s, 1 s tail; files `412_min_legato.wav`, `412_max_legato.wav`,
   `1_min_legato.wav`, `1_max_legato.wav`; manifest lines as for the others.
4. Finiteness CHECK per render as before. Not run by the executor: the main loop re-runs `render.sh` after the G4 gate.


**Files created:** `plugins/profundum/tests/integration/main_page_render_set_test.cpp`,
`specs/profundum-phase12-ui/evidence/listen/render.sh`. **Files edited:** `plugins/profundum/tests/CMakeLists.txt`.

0. **Ruling check (BLOCKING).** The FR-001 row of `spec.md` must carry the user's P-1 ruling allowing this TU before
   ratification (plan §2 P-1, §10 OQ-1). Absent or "no": return `blocked` with notes quoting plan §10 OQ-1 and "T010 needs the P-1
   ruling in the FR-001 row"; write no file.
1. **Test** (the only assertion is finiteness): `TEST_CASE("Profundum_MainPageRenderSet", "[profundum][ui][render][.listen]")`.
   For each render of D1 item 8 (`design/main-page.md`): a heap `ProcessorFixture` (`profundum_test_fixture.h:508`) at 48 kHz, block
   512, every ID at its registered default, the render's Main-control values written as parameter changes (normalised through each
   ID's registered law), MONO; note E1, then C3 in a separate render; 3 s held + 1 s tail. `CHECK` every sample finite
   (`Krate::DSP::detail::isFinite`). Write each render as a 32-bit float stereo WAV named `<id>_<min|def|max|patch>_<E1|C3>.wav`
   plus `manifest.txt` (one line per file listing every Main control's value) into `specs/profundum-phase12-ui/evidence/listen/`,
   located as `PROFUNDUM_RESOURCES_DIR "/../../../specs/profundum-phase12-ui/evidence/listen"`. The WAV writer is TU-local (as
   `dsp/tests/unit/systems/profundum_core_spectral_test.cpp:1561–1600` keeps its own; plan §3.3).
2. **CMake:** add `integration/main_page_render_set_test.cpp` to `add_executable(profundum_tests …)` under a `# Phase 12` comment and
   to the `-fno-fast-math` block. Nothing else.
3. **`render.sh`** (bash): one build of `profundum_tests` (`-- /nr:false`) to a log; `profundum_tests.exe
   "Profundum_MainPageRenderSet"` to `evidence/listen/render.log`; then `bash
   specs/profundum-phase9-voice-engine/evidence/close/listen.sh` (Phase 9's renders for the FR-097 verdicts, plan §3.4); each step's
   wall clock; last line `LISTEN RENDERS DONE|FAILED`. `bash -n` it.

**Executor:** write the three edits and return done with notes "deferred to the main loop: run
`bash specs/profundum-phase12-ui/evidence/listen/render.sh`, then hand the user the WAV list for the SC-006 (b) and FR-097 verdicts".

**Verified by:** the G4 gate compiles the TU (it is hidden and does not run there). Run-log `T010.txt`.

---

# GROUP 5 — Ratification gate

## T011 — 12a ratification gate (SC-001) — STOP AND SURFACE until every item holds

**Files created:** `specs/profundum-phase12-ui/evidence/run-log/T011.txt`. **No other file.**

Check, in `spec.md` and `design/`, and record each result with its line number:
1. All seven FR-002 files exist under `design/`; every wireframe control carries an ID or "UI-only".
2. Every line of the T009 checklist carries a user name and a date, including D1 with its 11-row §14 delta table, the SC-006 (b)
   listening verdict, and the D2 strings / descriptor table / decay.
3. The FR-097 table has 8 rows, each `pending` or `given (YYYY-MM-DD): …`, none blank; every change-requesting verdict also
   appears as a ruling in its owning FR / SC row.
4. Plan questions: OQ-2 ruled into the SC-004 row; OQ-6 ruled into the FR-032 row; OQ-3, OQ-4, OQ-7 and the Deviation 6 question
   each carry a ruling. An OQ-3, OQ-4 or OQ-7 ruling **against** the plan default → `blocked`, naming the tasks to re-cut
   (precondition table). A "run them" ruling on Deviation 6 → note "T036 gains the 46-case lane" (non-blocking; the main loop
   amends T036).
5. `git log --name-only 40f6ebca..HEAD -- plugins/ dsp/` shows no 12b-scope commit (T010's files excepted under P-1).
6. Lists, with `design/` line numbers, every ratified value later tasks read (designed size, zoom steps, D7 minima and colour
   tokens, tooltip literals, descriptor table and thresholds, decay time, sync period, mirror yes / no), so executors find them.

Any item missing → status **`blocked`**, notes listing every missing item (verbatim checklist line) and "resume the build stage
with `unblock: ["T011"]` after the user ratifies". All present → `done`, notes "12a ratified on <date>; 12b may start".

**Verified by:** `T011.txt`.

---

# STAGE 12b — IMPLEMENTATION (starts only after T011 is unblocked)

# GROUP 6 — `dsp/` display accessors

## T012 — `ProfundumEngine` / `ProfundumVoice` read accessors + `Profundum_EngineDisplayAccessorNeutral` (FR-075, FR-095; SC-022 engine arm; plan §4.1, §5.1)

**Files created:** `dsp/tests/unit/systems/profundum_engine_display_test.cpp`.
**Files edited:** `dsp/include/krate/dsp/systems/profundum_engine.h`, `dsp/include/krate/dsp/systems/profundum_voice.h`,
`dsp/tests/CMakeLists.txt`.

1. **Test first** — `TEST_CASE("Profundum_EngineDisplayAccessorNeutral", "[profundum][systems][display]")`. Include
   `profundum_voice_engine_test_helpers.h` (qualify its names; no `using namespace` of a test namespace). Two heap engines A and B,
   `prepare(48000.0)` with the default config, identical event scripts, and the **same** recording `ProfundumEngineObserver` type
   attached to both (the observer's presence is symmetric):
   - `SECTION("MonoBitIdentical")`: MONO, note E1 (28) on at sample 0, off at 24000, 48000 samples. A renders host blocks of a
     fixed pseudo-random size sequence (LCG seed 0x1234, sizes 1 … 517); B renders the same blocks, each cut into sub-calls of
     `min(remaining, 32 − controlGridPhase())`, and after each sub-call that returns with `controlGridPhase() == 0` reads every
     accessor. `REQUIRE(std::memcmp(outA, outB, bytes) == 0)` for L and for R.
   - `SECTION("PolyBitIdentical")`: POLY, polyphony 4, notes 28 / 33 / 40 on at 0 / 4800 / 9600, 33 off at 19200, polyphony set to 2
     at 21000 and note 45 on at 24000 (a steal), 48000 samples; the same two `memcmp`s.
   - `SECTION("TapsEqualObserver")`: in the MONO and POLY runs, at every completed interval and for every voice v with
     `voiceRenderedThisInterval(v)`: `voiceForDisplay(v).intervalSamplesRendered() == 32`, and `characterTapL/R()` and
     `anchorTapL/R()` are `memcmp`-equal to the observer's `charL/R` and `anchorL/R` spans recorded for voice v over the interval
     that starts at `samplePosition() − 32`.
   - `SECTION("ClockAndNotes")`: after every sub-call `controlGridPhase() == total % 32` and `samplePosition() == total`;
     `appliedVoiceMode()` equals the mode set (from the first grid point on); MONO: `voiceMidiNote(0) == 28` while held,
     `voiceMidiNote(1) == −1`; POLY: the set {`voiceMidiNote(v)` ≥ 0} equals the set of notes currently allocated;
     `voiceMidiNote(8) == −1`, `voiceRenderedThisInterval(8) == false`, `&voiceForDisplay(99) == &voiceForDisplay(7)`.
   Non-vacuity: the TU does not compile before the accessors exist, and `MonoBitIdentical` fails if a sub-call is cut anywhere but
   a grid point (a control arm `SECTION("OffGridCutIsDetected")` cuts B at sample 17 of an interval, then asserts the accessors
   report `controlGridPhase() == 17` — proving the clock read is live).
2. **Implement** exactly the plan §4.1 code blocks: the six engine accessors appended after `oversamplerStateFinite()`
   (`profundum_engine.h:422`, before `private:` :424); the five voice accessors appended after `stateFinite()`
   (`profundum_voice.h:695–698`, before `private:` :700). Banner `// Phase 12 FR-075 / FR-095: display read path. const,
   allocation-free, behaviour-neutral.` No new member, include or write. If `<span>` or `<algorithm>` is not already included,
   STOP AND SURFACE.
3. **CMake:** add `unit/systems/profundum_engine_display_test.cpp` after `unit/systems/mass_dynamics_oversampling_test.cpp`
   (`dsp/tests/CMakeLists.txt:816`) and a comment line in the Profundum block (`#   profundum_engine_display_test.cpp  Phase 12
   SC-022 accessor neutrality`). It uses `memcmp`, not bit-pattern finiteness, so it joins no `-fno-fast-math` block.

**Verified by:** G6 gate: `dsp_systems_tests "[profundum]"` and, per the gate rule, the per-push sets of `dsp_systems_tests` and
`profundum_tests` (tracked `dsp/` headers changed).

---

# GROUP 7 — New files (all [P]; every file is brand new and owned by exactly one task)

## T013 [P] — `spectrum_frame.h` + `spectrum_producer.h` + `Profundum_SpectrumProducerMath` (FR-071, FR-073 – FR-075; plan §4.2, §4.3, §5.2 – §5.4; Deviation 2)

**Files created:** `plugins/profundum/src/processor/spectrum_frame.h`, `plugins/profundum/src/processor/spectrum_producer.h`,
`plugins/profundum/tests/unit/spectrum_producer_test.cpp`.

1. **Test first** — `TEST_CASE("Profundum_SpectrumProducerMath", "[profundum][ui][spectrum]")`, driving `prepare` /
   `feedInterval` / `feedSilence` / `takeFrame` directly with synthetic 32-sample intervals (no engine):
   - `SECTION("SineReadsAmplitudeSquared")`: fs 48000, f0 = 41.20344 Hz (E1); L = R = 0.5·sin(2π·3·f0·t) + 0.25·sin(2π·10·f0·t);
     anchor L = R = 0.3·sin(2π·f0·t). Feed until a frame is staged. `frame.windowSamples == 9320` (round(8·48000 / f0));
     `frame.numPartials == 64`; `10·log10(partialPower[2] / 0.25)` and `10·log10(partialPower[9] / 0.0625)` within ±0.05 dB; every
     other partial ≤ −80 dB re 0.25; `10·log10(anchorPower / 0.09)` within ±0.05 dB; `sampleRate == 48000`, `f0Hz == f0` within 1e-4
     relative.
   - `SECTION("PartialCountAndNyquist")` (plan §5.3): (E1, 48000) → 64; (C3 130.8128 Hz, 48000) → 64; (C6 1046.502 Hz, 44100) → 21.
     f0 = 24000 at 48000, f0 = 0 and f0 = NaN (bit pattern through a `volatile`) each take the silence path: the staged frame has
     `numPartials == 0` and `flags & kFrameFlagSilent`.
   - `SECTION("CadenceOneFramePerWindow")`: E1 and C3 at 48000, 2 s of intervals, `takeFrame` after every 16 intervals (512
     samples): frames taken == `windowsCompleted()` == 10 at E1 (N = 9320) and 32 at C3 (N = 2936), each ± 1; `windowsCompleted()`
     advances by ≤ 1 per 512 samples.
   - `SECTION("NonFiniteNeverPublished")`: one NaN (bit pattern) inside a window → that window publishes nothing
     (`windowsCompleted()` does not advance across it); every float of every frame taken in the section passes `detail::isFinite`.
   - `SECTION("TinyInputsStayNormal")`: five windows of L, R and anchor at 1e-38: every staged power is finite and either exactly 0
     or normal (bit-pattern test: exponent field ≠ 0).
   - `SECTION("SilenceThenRepublish")`: after a frame, the first `feedSilence` stages one silent frame; further calls stage
     nothing until 12000 samples (0.25 s at 48 kHz) have passed, then exactly one more.
   - `SECTION("PitchUnstableFlag")`: f0 drifting by 2e-3 relative within one window sets `kFrameFlagPitchUnstable`; 5e-4 does not.
   - `SECTION("FocusChangeRestartsWindow")`: a `focusVoice` change mid-window publishes no frame for the partial window, and the next
     frame's `windowSamples` samples all come from the new focus.
   - The plan §4.2 `static_assert`s compile (size 288, % 32 == 0, trivially copyable, standard layout, offsets 24 / 32).
2. **Implement** `spectrum_frame.h` exactly as plan §4.2 (namespace `Profundum`; `kProfundumFrameUserContextId = 0x50524644u`;
   `kSpectrumMaxPartials = 64`; the three flags; the struct; the `static_assert`s; standard-library includes only).
   `spectrum_producer.h` exactly as plan §4.3 (class body; §4.3.1 – §4.3.6) and §5.2 – §5.4: double-precision periodic Hann from a
   rotating phasor renormalised every 4096 samples; Goertzel coefficients by the Chebyshev recurrence; partial-inner loop over
   contiguous `std::array<double, 64>`; P_n = 8(|L_n|² + |R_n|²) / N²; anchor (Σ aL² + Σ aR²) / N; N = clamp(round(8·fs / f0), 64,
   2²⁰); the §5.3 count rule; `onInterval` and the §4.3.2 focus rule against the T012 accessors (`#include
   <krate/dsp/systems/profundum_engine.h>`). Finite checks by `Krate::DSP::detail::isFinite` only. No allocation after
   construction; no transcendental per sample.

**Verified by:** `profundum_tests` once T018 registers the TU (G8 gate).

## T014 [P] — `endpoint_names.h` + `endpoint_descriptor.h` + `Profundum_EndpointNames` pure sections (FR-047, FR-048; SC-027 (d); plan §4.5, §4.11)

**Files created:** `plugins/profundum/src/parameters/endpoint_names.h`, `plugins/profundum/src/ui/endpoint_descriptor.h`,
`plugins/profundum/tests/unit/state_v3_test.cpp`.

1. **Test first** — `TEST_CASE("Profundum_EndpointNames", "[profundum][ui][morph][state]")` (T019, T023 and T028 add sections):
   - `SECTION("Sanitise")`: `sanitizeEndpointName("DEEP PRESSURE")` → length 13, same bytes; bytes 0x01, 0x1F, 0x7F removed; an
     invalid UTF-8 sequence (`"\xC3("`) dropped; a 40-byte ASCII string → length 31; eleven 3-byte code points ("€" × 11, 33 bytes) →
     length 30 (10 code points, cut at a code-point boundary); empty → length 0. `defaultEndpointName(Endpoint::A)` is "A", `(B)` is
     "B".
   - `SECTION("BlockRoundTrip")`: `writeEndpointName` writes exactly 32 bytes (u8 length + 31 bytes, zero-padded);
     `readEndpointName` on them returns true and the same name; a block whose length byte is 32 → false; a block with invalid UTF-8
     → false; a stream with 20 bytes left → false.
   - `SECTION("DescriptorTable")` (SC-027 (d)): for each of the six worked cases of `design/advanced-page.md` item 7,
     `::Profundum::UI::describeEndpoint(state)` returns the three ratified words (string compare); two identical states give the
     same words; raising `body` from the Round band into the Dense band changes word 1 exactly as the table says, and nothing else.
2. **Implement** `endpoint_names.h` (namespace `Profundum`) per plan §4.5 "`parameters/endpoint_names.h`": `struct EndpointName
   { std::array<char, 31> bytes{}; std::uint8_t length = 0; }`, `defaultEndpointName`, `sanitizeEndpointName`, `writeEndpointName`,
   `readEndpointName` (`IBStreamer`, `base/source/fstreamer.h`). To stay independent of T019, it declares its own
   `inline constexpr std::size_t kEndpointNameBytes = 31;` and `kEndpointNameBlock = 32;`; T019 adds the `static_assert`s that tie
   them to `kEndpointNameMaxBytes` / `kEndpointNameBlockBytes` in `plugin_ids.h`. `endpoint_descriptor.h` (namespace
   `Profundum::UI`): `[[nodiscard]] std::array<const char*, 3> describeEndpoint(const Krate::DSP::MorphState& s) noexcept`, the
   ratified D2 table with named constants `kDescriptorLowerThreshold` (0.33f) and `kDescriptorUpperThreshold` (0.66f) or the
   ratified values.

**Verified by:** `profundum_tests` once T018 registers the TU (G8 gate).

## T015 [P] — `macro_ui_map.h` + `Profundum_MacroUiMap` + `Profundum_UiContractTables` probe (FR-005, FR-007, FR-060; plan §3.2, §4.10)

**Files created:** `plugins/profundum/src/ui/macro_ui_map.h`, `plugins/profundum/tests/unit/controller/macro_ui_test.cpp`.

1. **Test first:**
   - `TEST_CASE("Profundum_MacroUiMap", "[profundum][ui]")`: `kMacroTargetParamIds` equals {200, 201, 302, 204, 205, 501, 602, 420,
     421, 423, 700, kNoParamId, 206, 207, 500, 601} element-wise; for each `ProfundumMacro` m, `highlightMask(m)[id]` is true exactly
     for the IDs of m's `kRows` rows with `amount != 0`, CompressionAmount excluded — Weight {200, 201, 302}, DarkBright {204, 205,
     602}, TightLoose {420, 421, 423, 700}, CleanViolent {206, 207, 500, 601}; `highlightMask(DarkBright)[501] == false`; the four
     sets are pairwise disjoint and their union has 14 IDs. The expected sets are derived in the test from `kRows` (not typed), and
     then also compared with the literal sets above.
   - `TEST_CASE("Profundum_UiContractTables", "[profundum][ui][.][probe]")`: prints two CSVs to stdout — D3 (row, macro, target,
     amount, curve, registered ID, `no-control`, `inert`) from `kRows` + `kMacroTargetParamIds`, and D4 (leaf, law, axis, lo, hi,
     registered ID) from `kLawTable` + `kSoundRows` / `kParamRoutes`; `REQUIRE` 16 and 62 rows printed.
2. **Implement** `macro_ui_map.h` exactly as plan §4.10 (namespace `Profundum::UI`; `kNoParamId = 0xFFFFFFFFu`; the array in
   `ProfundumMacroTarget` order; `constexpr highlightMask`; `static_assert`s: every non-Compression entry is a `RouteKind::Sound`
   route in `kParamRoutes`, the Compression entry is `kNoParamId`, `kRows[t].target == t` for every t).

**Verified by:** `profundum_tests` once T018 registers the TU (G8 gate).

## T016 [P] — `HarmonicBarView` + `SpreadCurveView` (+ `SpreadCurveModel`) + `Profundum_UiViewsUnit` (FR-070, FR-073, FR-080, FR-091; plan §4.8, §4.9; Deviations 3, 4)

**Files created:** `plugins/profundum/src/ui/harmonic_bar_view.h`, `plugins/profundum/src/ui/harmonic_bar_view.cpp`,
`plugins/profundum/src/ui/spread_curve_view.h`, `plugins/profundum/src/ui/spread_curve_view.cpp`,
`plugins/profundum/src/ui/offset_ring_knob.h`, `plugins/profundum/src/ui/offset_ring_knob.cpp`,
`plugins/profundum/tests/unit/controller/ui_views_test.cpp`.

**P-5 re-cut (2026-10-23).** `OffsetRingKnob` (namespace `Profundum`; ODR sweep: 0 hits) derives from the shared `ArcKnob`
(`arc_knob.h`, read-only, no shared-header change). `setOffset(float signedNormDelta)` / `getOffset()`; `ringSpan()` returns
the drawn normalised span `[min(v, v+d), max(v, v+d)]` clamped to [0, 1]; d = 0 draws no ring; the knob value is never moved.
`draw()` calls the base draw with the base modulation range at 0, then strokes the one-sided arc in the modulation colour.
Test arm in `Profundum_UiViewsUnit` `SECTION("OffsetRingOneSided")`: v = 0.4, d = +0.2 → span [0.4, 0.6]; d = −0.3 → [0.1, 0.4];
v = 0.9, d = +0.3 → [0.9, 1.0]; d = 0 → no span; `getValue()` unchanged in every arm. Register `OffsetRingKnob` for the uidesc
the same way as the other plugin views (T026's `createCustomView` / view-creator path).

1. **Test first** — `TEST_CASE("Profundum_UiViewsUnit", "[profundum][ui]")`, after `Krate::TestSupport::ensureVstguiInitialized()`:
   - `SECTION("BarHeightsFromFrame")`: frame fs 48000, f0 41.2, `numPartials` 64, `partialPower[0] = 1.0`, `[1] = 0.1`, `[2] = 1e-6`,
     `[3] = 1e-7`, `anchorPower = 0.5`, `sequence` 1; one `onTickForTest(0.033)`: h₁ = 1, h₂ = 50 / 60, h₃ = 0, h₄ = 0 (clamped),
     anchor = (10·log10(0.5) + 60) / 60, each within 1e-6; heights past `numPartials` are 0.
   - `SECTION("SilenceDecaysLinearly")` (the D2 decay, default 300 ms): after that frame, a silent frame (`sequence` 2,
     `kFrameFlagSilent`), then ticks of 0.033 s: no height ever increases; at elapsed 0.165 s, h₁ = 1 − 0.165 / 0.300 within 1e-6; after
     the tick that crosses 0.300 s every height is 0.
   - `SECTION("DisplayOnly")`: `onMouseDown` / `onMouseMoved` / `onMouseUp` at any point return `kMouseEventNotHandled`;
     `dynamic_cast<VSTGUI::CControl*>(view)` is null.
   - `SECTION("TimerOnlyWhileAttached")`: `hasTimerForTest()` false after construction, true after `attached(parent)` to a
     `CViewContainer`, false after `removed(parent)`, for 10 cycles.
   - `SECTION("SpreadViewMirrorsModel")`: a `SpreadCurveModel` with `w[i] = i / 63.0f`, crossover 120, lower knee 85, upper knee
     480: `sampledValuesForTest()` equals `w` element-wise; `kneeMarksForTest()` is {85, 480}; `wideLabelVisibleForTest()` is true
     iff `wide`.
2. **Implement** per plan §4.8 / §4.9, namespace `Profundum::UI`. `HarmonicBarView(const CRect&, const ProfundumSpectrumFrame* frame,
   Variant v)` with `enum class Variant { Main, Core }`; a 33 ms `CVSTGUITimer` from `attached` to `removed`; `onTick(dt)` reads
   `frame_->sequence`; dB conversion on the UI thread; the ratified decay as `kSilenceDecaySeconds`; `draw` per D2 item 3; test surface
   `barHeightsForTest()`, `anchorHeightForTest()`, `onTickForTest(dt)`, `hasTimerForTest()`. `spread_curve_view.h` declares
   `struct SpreadCurveModel` exactly as plan §4.6.8 and `SpreadCurveView(const CRect&, const SpreadCurveModel*)` with
   `sampledValuesForTest()`, `kneeMarksForTest()`, `wideLabelVisibleForTest()`; no timer. Include `processor/spectrum_frame.h`
   (T013's file, same group; plan §4.2 makes it the shared POD header).

**Verified by:** `profundum_tests` once T018 registers the TU and the `.cpp`s (G8 gate).

## T017 [P] — Placeholder TUs, panel stub, test UI-tree helper

**Files created** (placeholders hold only `#include <catch2/catch_test_macros.hpp>`, `#include "profundum_test_fixture.h"` and one
comment `// <TEST_CASE names> — written by T0NN (specs/profundum-phase12-ui/tasks.md)`; **no TEST_CASE**):

| File | Filled by |
|---|---|
| `plugins/profundum/tests/unit/controller/editor_layout_test.cpp` | T027 |
| `plugins/profundum/tests/unit/controller/morph_component_test.cpp` | T022, T028 |
| `plugins/profundum/tests/unit/controller/spread_curve_test.cpp` | T024, T030 |
| `plugins/profundum/tests/unit/controller/editor_sync_test.cpp` | T026 |
| `plugins/profundum/tests/unit/controller/custom_view_laws_test.cpp` | T025, T030 |
| `plugins/profundum/tests/integration/spectrum_frame_test.cpp` | T020, T021 |
| `plugins/profundum/tests/integration/main_page_sanity_test.cpp` | T031 |

Plus:
- `plugins/profundum/src/ui/panel_sub_controller.h` (banner `// ProfundumPanelSubController — written by T026`, `#pragma once`,
  nothing else) and `plugins/profundum/src/ui/panel_sub_controller.cpp` (only `#include "ui/panel_sub_controller.h"`), so T018 can
  register the `.cpp` and it compiles empty.
- `plugins/profundum/tests/ui_tree_helpers.h` (namespace `ProfundumPluginTest`, header-only, `inline`): `struct HeadlessEditor`
  (RAII: a heap `VST3Editor(controller, "editor", <PROFUNDUM_RESOURCES_DIR>/editor.uidesc)` attached with `attached(nullptr,
  Krate::TestSupport::nativePlatformType())` as `editor_lifecycle_test.cpp:169–172` does; `removed()` + `release()` in the
  destructor; accessors `editor()` and `frame()`), `collectBoundControls` (copied from `editor_lifecycle_test.cpp:71–91`),
  `template <class T> std::vector<T*> collectViewsOfType(CViewContainer*)`, and the XML scan `XmlNode` / `XmlScan` / `scanUidesc` /
  `findLabelled` / `isDescendantOf` / `boundNodes` copied from `plugins/vorago/tests/unit/controller/editor_layout_test.cpp:324–580`
  (Deviation 7; Vorago names replaced).

**Verified by:** compile at the G8 gate.

---

# GROUP 8 — Early registration

## T018 — CMake registration of the new plugin `.cpp`s and test TUs (Deviation 1)

**Files edited:** `plugins/profundum/CMakeLists.txt`, `plugins/profundum/tests/CMakeLists.txt`.

1. `plugins/profundum/CMakeLists.txt`: under a new `# UI (Phase 12)` comment after the controller lines, add
   `src/ui/harmonic_bar_view.cpp`, `src/ui/spread_curve_view.cpp`, `src/ui/panel_sub_controller.cpp`. (Headers wait for T034.)
2. `plugins/profundum/tests/CMakeLists.txt`:
   - in `add_executable(profundum_tests …)` under `# Phase 12` (after T010's line if present): `unit/spectrum_producer_test.cpp`,
     `unit/state_v3_test.cpp`, `unit/controller/editor_layout_test.cpp`, `unit/controller/morph_component_test.cpp`,
     `unit/controller/macro_ui_test.cpp`, `unit/controller/spread_curve_test.cpp`, `unit/controller/editor_sync_test.cpp`,
     `unit/controller/custom_view_laws_test.cpp`, `unit/controller/ui_views_test.cpp`, `integration/spectrum_frame_test.cpp`,
     `integration/main_page_sanity_test.cpp`;
   - in the SECOND-compilation block: `${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/harmonic_bar_view.cpp`, `…/spread_curve_view.cpp`,
     `…/panel_sub_controller.cpp`;
   - in the `-fno-fast-math` block (plan §7.1): `unit/spectrum_producer_test.cpp`, `unit/state_v3_test.cpp`,
     `integration/spectrum_frame_test.cpp`, `integration/main_page_sanity_test.cpp`. `integration/processor_cpu_test.cpp` stays out;
   - `krate_test_pch(profundum_tests)` stays last.

**Verified by:** G8 gate: `Profundum profundum_tests` build with 0 warnings; `Profundum_SpectrumProducerMath`,
`Profundum_EndpointNames` (pure sections), `Profundum_MacroUiMap` and `Profundum_UiViewsUnit` pass; the placeholders compile.

---

# GROUP 9 — State v3 and endpoint names

## T019 — State v3, `kMsgEndpointName`, `setEndpointName`, `v2StreamOf`, P-2 audit (FR-047, FR-096; SC-004, SC-027 (a) API arm, (b), (c), (e); plan §4.5, §6.2 "P-2 audit", R-11, R-13)

**Files edited:** `plugins/profundum/src/plugin_ids.h`, `plugins/profundum/src/processor/processor.h`,
`plugins/profundum/src/processor/processor.cpp`, `plugins/profundum/src/controller/controller.h`,
`plugins/profundum/src/controller/controller.cpp`, `plugins/profundum/tests/profundum_test_fixture.h`,
`plugins/profundum/tests/unit/state_v3_test.cpp`, and the P-2 audit sites in `plugins/profundum/tests/unit/state_v2_test.cpp`,
`unit/state_roundtrip_test.cpp`, `unit/lifecycle_test.cpp`, `unit/phase11_alloc_test.cpp`,
`integration/determinism_test.cpp`, `integration/state_restore_render_test.cpp`.

1. **Test first** — add to `Profundum_EndpointNames` (`unit/state_v3_test.cpp`):
   - `SECTION("CurrentVersionIsThree")`: `::Profundum::kCurrentStateVersion == 3`, `kStateV3Bytes == 574`, `kStateNamesOffset == 510`;
     a default processor's `getState` is 574 bytes and its first int32 is 3; bytes [510, 542) are the block of "A" and [542, 574)
     the block of "B" (length byte 1, then 'A' / 'B', then zeros).
   - `SECTION("ApiCommitSendsOneMessage")` (SC-027 (a), API arm): `ConnectedPair` (`profundum_test_fixture.h:755`) with a counting
     peer: `controller->setEndpointName(Endpoint::A, "DEEP PRESSURE")` sends exactly one `kMsgEndpointName` (int `"endpoint"` 0,
     binary `"name"` 13 bytes) and records `setDirty`; the processor then holds it (`endpointNameForTest(A)`).
   - `SECTION("NamesRoundTrip")` (SC-027 (a)): names A = 31-byte ASCII, B = a multi-byte UTF-8 name of 33 bytes (truncated to 30 at a
     code-point boundary); `getState` → `setState` on a fresh heap processor and `setComponentState` on a fresh controller → both
     names equal the sanitised typed ones on the processor (`endpointNameForTest`) and the controller (`endpointNameForTest`).
   - `SECTION("OlderStreamsLoad")` (SC-027 (b)): `v2StreamOf(priorFieldsP(), a, b)` (new helper, below) and the existing
     `v1Stream(...)` (`profundum_test_fixture.h:1062`) each load into a fresh processor and controller; names are "A" and "B"; every
     restored value equals the existing v2 / v1 expectations reused unchanged from `state_v2_test.cpp` (call the same checker).
   - `SECTION("V3PrefixEqualsV2Body")` (SC-027 (c)): load `v2StreamOf(f, a, b)` into a fresh processor, `getState`: 574 bytes,
     version word 3, bytes [4, 510) `memcmp`-equal to bytes [4, 510) of the `v2StreamOf` reference. Second arm: the same values set
     through parameters instead of `setState`, then each v2 field decoded at its pre-phase offset (`kV2GlobalsAt` … `kV2SeedAt`,
     `kStateEndpointAOffset`, `kStateEndpointBOffset`) equals the value set.
   - `SECTION("DescriptorNotStored")` (SC-027 (e)): two processors with identical names and settings differing in `recipe.body`
     (a field the descriptor reads): their `getState` streams have equal length and differ only inside [kStateEndpointAOffset,
     kStateRuntimeOffset).
   - `SECTION("ShortNameBlockKeepsPriorName")`: a v3 stream truncated at byte 530 loads; name A stays the processor's prior name
     (the Phase 11 truncation rule) and the rest loads.
   - `SECTION("NotifyIsSanitised")` (R-13): a `kMsgEndpointName` carrying 0x01 bytes and 40 bytes stores the sanitised 31-byte name;
     endpoint index 2 is ignored.
   Non-vacuity: before the change `CurrentVersionIsThree` fails (version 2, 510 bytes); `OlderStreamsLoad` guards the two branch
   bugs (`processor.cpp:504`, `controller.cpp:117`), which would reset a v2 stream under v3.
2. **Fixture:** add `v2StreamOf(const V2Fields&, const MorphState& a, const MorphState& b)` exactly per plan §6.2 SC-027 "v2
   reference" (zeroed `kStateV2Bytes` buffer, version word `kStateV2Version`, `withFields` + `withEndpoints`, bytes [490, 510) from
   `BassMorphEngine::serializeRuntime` of a fresh engine). Change the guards of `withEndpoints` / `withFields`
   (`profundum_test_fixture.h:1045, :1092`) to `== kStateV2Bytes || == kStateV3Bytes`; `writeV2Stream` keeps its name and gains the
   comment "returns the current format (v3 since Phase 12)".
3. **Implement** plan §4.5: `plugin_ids.h` constants (`kCurrentStateVersion = 3`, `kStateV2Version = 2`, `kEndpointNameMaxBytes`,
   `kEndpointNameBlockBytes`, `kStateNamesOffset`, `kStateV3Bytes`, `kMsgEndpointName`) and the replacement `static_assert` of
   `plugin_ids.h:48–49`; `static_assert`s tying `endpoint_names.h`'s local constants to these. Processor: `:504` becomes
   `version == kStateV1Version`; v2 / v3 run `loadV2Tail`; v3 then reads the two name blocks (short or invalid keeps the prior
   name); `getState` appends both blocks after the runtime blob; `notify` gains the `kMsgEndpointName` branch; `names_` +
   `namesMutex_` + `endpointNameForTest` (plan §4.4 members; mutex never on the audio thread). Controller: `:117` becomes
   `version >= kStateV2Version`; for v3 skip the 20-byte runtime blob into a local then read both names; `setEndpointName(Endpoint,
   std::string_view)` per plan §4.5 (sanitise, store, send, `setDirty(1u)`); `endpointName(Endpoint) const` and
   `endpointNameForTest`. Copy does not copy names (plan OQ-4).
4. **P-2 audit** (under the T011-confirmed SC-004 ruling): classify each of the 36 `kStateV2Bytes` lines (list in "Verified API
   surface") into exactly one class of plan §6.2 — `getState`-length assertion → `kStateV3Bytes`; builds / truncates a v1 or v2
   stream → unchanged; patch helper → dual guard (step 2). Write the classified list (file:line, class, action) to
   `evidence/run-log/T019.txt`. No v1 / v2 **load** expectation changes.

**Verified by:** G9 gate (`profundum_tests` per-push set; `Profundum_StateVersions`, `Profundum_StateRoundTripV2`,
`Profundum_ParamTableConformance`, `Profundum_ParamDisplayFormats` must pass with only the audit edits, SC-004).

---

# GROUP 10 — Processor spectrum producer

## T020 — DataExchange handler, queue probe, grid sub-slicing, publish, seam arms (FR-071 – FR-075; SC-010, SC-011, SC-012, SC-025, SC-026 (1)(2)(4), SC-030 seam arms; plan §4.4, §5.1, §6.2 "Spectrum")

**Files edited:** `plugins/profundum/src/processor/processor.h`, `plugins/profundum/src/processor/processor.cpp`,
`plugins/profundum/tests/integration/spectrum_frame_test.cpp`.

1. **Test first** (all in `integration/spectrum_frame_test.cpp`; seam arm = `setSpectrumProducerEnabledForTest(true)`, no
   connection; reference per plan §5.2, independent of the frame's f0 / N; observer through `Processor::setEngineObserverForTest`):
   - `TEST_CASE("Profundum_SpectrumFrameMatchesSignal", "[profundum][ui][spectrum]")`, `SECTION("Seam")`: fixtures (MONO, 0.5 s
     settle, Motion 900 Off, Drop Amount 0, no bend): Phase 8 showcase A and B and the default patch at E1 and C3 / 48 kHz; the
     fold fixture (showcase B with Fold 500 = 100 and Character Amount 601 = 1) at E1 and C3 / 48 kHz; the Nyquist fixture (default
     patch, C6 = 1046.5 Hz, 44.1 kHz). For the first frame after settle: `frame.sampleRate == fs`, `frame.windowSamples == N_ref`
     (= clamp(round(8·fs / f0_ref), 64, 2²⁰), f0_ref = `midiNoteToFrequency(note)`), `|frame.f0Hz / f0_ref − 1| ≤ 1e-4`; then the
     reference P_ref,n = 8(|G_L|² + |G_R|²) / N_ref² by `goertzelPhasorWindowed` (`dsp/tests/unit/systems/profundum_excitation_test_helpers.h:107–120`)
     on the observer's `charL / charR` over `[windowEndSample − N_ref, windowEndSample)` with a periodic Hann: every frame partial
     ≥ −60 dB re its strongest agrees within ±0.5 dB; every reference partial ≥ −60 dB with n ≤ 64 is reported; `numPartials ≤
     floor(fs / 2 / f0)` (21 at the Nyquist fixture) and none above Nyquist; `10·log10(anchorPower)` vs (Σ aL² + Σ aR²) / N_ref of the
     observer's anchor taps within ±0.5 dB; after note-off the next published frame after the observer's last voice chunk is silent.
   - `TEST_CASE("Profundum_SpectrumFrameDoesNotChangeAudio", "[profundum][ui][spectrum]")`, `SECTION("Seam")`: 2 s render (note on /
     off, a 105 sweep 0 → 1 → 0 over 1 s, a POLY chord 28 / 33 / 40) twice in the same binary, seam on vs off: max |Δ| == 0 on L and R.
   - `TEST_CASE("Profundum_SpectrumFrameAllocationFree", "[profundum][ui][rt]")`, `SECTION("Seam")`: `TestHelpers::AllocationScope`
     over 2000 `process()` calls (block 256) with notes and parameter changes in flight: 0 allocations.
   - `TEST_CASE("Profundum_SpectrumFrameFocusVoice", "[profundum][ui][spectrum]")`, `SECTION("Seam")`: POLY, Drop 0, no bend; steps
     of SC-025: one note → focus its voice; second note → focus moves; release of the later → back to the earlier; polyphony 2 + a
     third note → the stealing note's voice, identical across two runs; all released → focus kept through the tail, then a silent
     frame. Each step: `spectrumProducerForTest().focusVoice()`, the frame's `focusVoice`, and `f0Hz == midiNoteToFrequency(note)`
     within 1e-4 relative.
   - `TEST_CASE("Profundum_SpectrumFrameSanitise", "[profundum][ui][spectrum]")`: `SECTION("NonFiniteInputNeverReachesFrame")`
     (step 1, through the producer's `feedInterval` seam with a NaN / +Inf bit pattern; every float of every frame passes
     `detail::isFinite`); `SECTION("RePrepareCarriesNewRate")` (step 2: deactivate, `setupProcessing` 96 kHz, activate; the first frame
     has `sampleRate == 96000` and no later frame has 48000); `SECTION("TinyInputsStayNormal")` (step 4, as T013 but through the
     processor's producer instance). (Step 3, the view decay, is T021.)
   - `TEST_CASE("Profundum_SpectrumFrameCadence", "[profundum][ui][spectrum]")`, `SECTION("Seam")`: E1 and C3, 2 s, block 512:
     `spectrumFramesPublishedForTest() == spectrumProducerForTest().windowsCompleted()`, within ±1 of ⌊samples since note-on / N⌋;
     `spectrumMaxFramesPerProcessForTest() ≤ 1`; the fold fixture's `numPartials == min(64, floor(fs / 2 / f0))`.
2. **Implement** plan §4.4 exactly: `connect` / `disconnect` (Vorago `processor.cpp:262–303` shape; block size 288, `numBlocks = 4`,
   alignment 32, `kProfundumFrameUserContextId`); `setActive` (onActivate → `spectrum_.reset()` → `queueState_ = handler ?
   ProbePending : Closed`; on false: Closed → `onDeactivate()` first); `setupProcessing` → `spectrum_.prepare(sr)`; `dispatchEvent`
   → `spectrum_.noteOnArrived(pitch)` before `engine_->noteOn`; the one-time queue probe; the gated grid sub-slicing loop (gate off
   = the Phase 11 loop byte for byte; `lastSliceCount_` still counts event slices); `publishSpectrumFrame()` after the loop; every
   test seam of plan §4.4. Forward-declare `Steinberg::Vst::DataExchangeHandler` in `processor.h`; include
   `public.sdk/source/vst/utility/dataexchange.h` in the `.cpp`. `sizeof(Processor) < 64 KB` stays (`processor.h:207` per plan).

**Verified by:** G10 gate (`profundum_tests` per-push set; existing `midi_event_test.cpp` slice-count asserts stay green).

---

# GROUP 11 — Controller DataExchange consumer

## T021 — `IDataExchangeReceiver`, cached frame, connected arms, SC-023, SC-026 step 3 (FR-071, FR-076, FR-091; SC-010 – SC-012 / SC-025 connected arms, SC-023, SC-026 (3); plan §4.6.1, §6.2)

**Files edited:** `plugins/profundum/src/controller/controller.h`, `plugins/profundum/src/controller/controller.cpp`,
`plugins/profundum/tests/integration/spectrum_frame_test.cpp`.

1. **Test first** (copy `kQueueMayBeUnavailable` / `queueAvailable` from `plugins/vorago/tests/integration/ecosystem_frame_test.cpp:108–131`
   into this TU's anonymous namespace):
   - `TEST_CASE("Profundum_SpectrumFrameHandlerLifecycle", "[profundum][ui][spectrum][lifecycle]")`: `ConnectedPair`;
     `setActive(true)` → exactly one `queueOpened` with uid `0x50524644` and block size 288 (`queueOpenCountForTest()`,
     `lastQueueBlockSizeForTest()`); `setActive(false)` → exactly one `queueClosed`; deactivate, `setupProcessing(96000)`, activate →
     exactly one more close and one more open. Queue assertions go through `queueAvailable(opened, step)` (WARN-skip on Linux only).
     `SECTION("ClosedQueueIsNoOp")` (every OS, FR-071): a processor initialised with a host context that is **not** an
     `IHostApplication`, connected to a `CountingConnectionPoint` (`profundum_test_fixture.h:220`), prepared, activated, 200
     `process()` calls with a held note: `spectrumQueueOpenForTest() == false`, `windowsCompleted() == 0`,
     `spectrumFramesPublishedForTest() == 0`, `spectrumSkippedBlocksForTest() == 0`. On Linux the plain `ConnectedPair` arm, when its
     queue did not open, asserts the same four readings (not WARN-skipped).
   - add `SECTION("Connected")` to `Profundum_SpectrumFrameMatchesSignal` (the controller's `cachedFrameForTest()` after each
     publish equals the processor's `lastSpectrumFrameForTest()` byte for byte, and the T020 checks run on the cached frame),
     `Profundum_SpectrumFrameDoesNotChangeAudio` (queue asserted open vs no connection; max |Δ| == 0; WARN-skip on Linux only),
     `Profundum_SpectrumFrameAllocationFree` (the scope opens before the first `process()` after `setActive(true)`, so the probe is
     measured; 0 allocations) and `Profundum_SpectrumFrameFocusVoice` (one note, then a second; the cached frame's `focusVoice`
     follows).
   - add `SECTION("ViewDecaysAfterSilence")` to `Profundum_SpectrumFrameSanitise` (step 3): a `HarmonicBarView` over the controller's
     cached frame; after a silent frame arrives, `onTickForTest(0.033)` repeatedly: heights never increase and are all 0 by the
     ratified decay time (0.300 s).
   - `SECTION("ClosedEditorCaches")`: with no editor open, delivered blocks update `cachedFrameForTest()`, `framesReceivedForTest()`
     counts them, and no view pointer exists.
2. **Implement** plan §4.6.1: add `public Steinberg::Vst::IDataExchangeReceiver` and `DEF_INTERFACE(IDataExchangeReceiver)`;
   `queueOpened` (uid filter, reset `cachedFrame_` to a silence frame, counts, UI-thread dispatch `dispatchOnBackgroundThread =
   false`), `queueClosed`, `onDataExchangeBlocksReceived` (newest valid block wins, Vorago `controller.cpp:314–329`), `notify` →
   `if (dataExchangeReceiver_.onMessage(message)) return kResultOk;` before the Phase 11 dispatch; `cachedFrame_` POD; the test reads.
   `IMidiLearn` is not added (FR-098).

**Verified by:** G11 gate.

---

# GROUP 12 — Compare read path

## T022 — `compareMode()`, change notification, `pressCompare` (FR-041 – FR-043, FR-046, FR-049; SC-007 (a)(b)(c)(e) controller arms; plan §4.6.3)

**Files edited:** `plugins/profundum/src/controller/controller.h`, `plugins/profundum/src/controller/controller.cpp`,
`plugins/profundum/tests/unit/controller/morph_component_test.cpp`.

1. **Test first** — `TEST_CASE("Profundum_MorphComponentUi", "[profundum][ui][morph]")` on a `ConnectedPair` with
   `RecordingConnectionPoint`s and a `RecordingComponentHandler`:
   - `SECTION("CompareMessagesAndEditTarget")` (a): `pressCompare(A)` → exactly one `kMsgCompare` with mode 1 and exactly one
     begin / perform / end on 1001 with value 0.0; `pressCompare(B)` → mode 2 and value 1.0; `pressCompare(AB)` → mode 0, zero
     `performEdit`, 1001's normalised value unchanged. A begin / perform / end on 105 sends no `kMsgCompare` and leaves
     `compareMode()` and `compareModeChangeCount()` unchanged.
   - `SECTION("ReplyDrivesState")` (b): `toController.hold = true` (`profundum_test_fixture.h:263`), press B: `compareMode()` and
     `compareModeChangeCount()` unchanged; `releaseHeld()` (:308): `compareMode() == B`, the count rose by exactly 1.
   - `SECTION("CopySendsOneMessage")` (c): `copyEndpoint(A)` → one `kMsgCopyEndpoint` with `"from"` 0 and a recorded `setDirty`;
     `copyEndpoint(B)` → `"from"` 1.
   - `SECTION("EditTargetRedisplays")` (e): a begin / perform / end on 1001 to B → the 60 E-parameters equal
     `normalizedFromLeafWord` of `mirror_[B]` and `restartComponent(kParamValuesChanged)` was recorded once.
   (The segment-UI arms of (a), (b) and (d) are T028.)
2. **Implement** plan §4.6.3: public `compareMode() const noexcept`, `compareModeChangeCount() const noexcept`; in `notify`'s
   `kMsgCompareMode` branch after `compare_ = …`: `++compareChanges_;` (the panel's `onCompareModeChanged` call is added by T026,
   when the panel class exists — the T017 header is still a stub here, so `controller.cpp` must not call into it yet);
   `pressCompare(CompareMode)` (setCompare, then for A / B: `beginEdit(1001); performEdit(1001, v); setParamNormalized(1001, v);
   endEdit(1001);`). No production code calls `compareModeForTest()`.

**Verified by:** G12 gate.

---

# GROUP 13 — Macro rings and endpoint captions

## T023 — `macroRingRange`, `refreshMacroRings` (controller arm), `endpointCaption` (FR-005, FR-040, FR-048, FR-061; SC-027 (f), SC-028 controller arm; plan §4.6.5, §4.6.6, §5.5)

**Files edited:** `plugins/profundum/src/controller/controller.h`, `plugins/profundum/src/controller/controller.cpp`,
`plugins/profundum/tests/unit/controller/macro_ui_test.cpp`, `plugins/profundum/tests/unit/state_v3_test.cpp`.

1. **Test first:**
   - `TEST_CASE("Profundum_MacroModulationRing", "[profundum][ui]")`, `SECTION("ControllerValue")`: for each macro value in
     {0, 0.25, 0.5, 0.75, 1} for each of 100 – 103 (others neutral) and each target ID of `kMacroTargetParamIds`:
     `controller->macroRingRange(id)` equals the test's own re-derivation within 1e-6 — rebuilt from `kRows`, `applyModCurve`, the
     field table and `normalizedFromPlain` (amp times: clamp(base·2^c, 0.1, 10000); others: clamp(base + c, lo, hi); result
     norm(mod) − norm(base)); at 0.5 the value is exactly 0.0f; 501 and any ID outside the map give 0; two evaluations are identical;
     setting 900 to Cycle changes nothing; the target's own normalised value is unchanged afterwards. Hand check (plan §5.5): with
     420 at 100 ms and Tight ↔ Loose at 1, the 420 value equals 5·ln 2 / ln(10000 / 0.1) within 1e-6.
   - `Profundum_EndpointNames` `SECTION("CaptionFormat")` (SC-027 (f)): after `setEndpointName(A, "IRON FOLD")`,
     `endpointCaption(A)` is `"IRON FOLD — w1 · w2 · w3"` with w1 – w3 = `describeEndpoint(mirror_[A])`; it is non-empty, has exactly
     three descriptor words; an empty name yields `"A — …"`.
2. **Implement** plan §4.6.5 (copy-then-assign `ProfundumMacroValues`, a local `ProfundumMacroMatrix`, `contributions()`, the
   apply rules; return the signed normalised delta) and `refreshMacroRings()` (calls `setOffset(macroRingRange(id))` on each
   registered `OffsetRingKnob` target, P-5 ruling; a no-op while no editor is open), plan §4.6.6 `endpointCaption`, recomputed on every mirror replacement and
   name change; the `setParamNormalized` refresh hooks of plan §4.6.4 for 100 – 103 and every target ID (rings) — guarded by "editor
   open".

**Verified by:** G13 gate.

---

# GROUP 14 — SPREAD model

## T024 — `refreshSpreadModel` (controller arm of SC-014) (FR-080; plan §4.6.8, D-6)

**Files edited:** `plugins/profundum/src/controller/controller.h`, `plugins/profundum/src/controller/controller.cpp`,
`plugins/profundum/tests/unit/controller/spread_curve_test.cpp`.

1. **Test first** — `TEST_CASE("Profundum_SpreadCurveMatchesField", "[profundum][ui]")`, `SECTION("Model")`: the grid Spread
   {0, 0.5, 1} × Gravity {0, 0.5, 1} × crossover {60, 120, 250} Hz × curve {0, 0.5, 1} × image mode {Solid, Spread, Wide} × field
   Motion depth 805 {0, 1}, written through `setParamNormalized` (normalised by each row's law). For every cell, at the 64 points
   `hz[i] = 20·(10000 / 20)^(i / 63)`: `spreadModelForTest().w[i]` equals `ref.widthCurve(hz[i]) · m` within 1e-6, where `ref` is an
   independent heap `BassStereoField` given the same `Controls` by copy-then-assign then `reset()`, and m = 0 in Solid, else
   spread·(1 − 0.5·depth). Sub-checks: Solid and Spread 0 draw 0 everywhere; at 10 kHz (c = 1) the value equals m and the three
   Spread values are distinct and linearly spaced; depth 1 gives half the depth-0 value; c(f) differs across Gravity and across
   crossover at some point below 4·f_c; Spread and Wide give identical w; `lowerKneeHz` equals f_c·2^(−kLowerKneeMaxOctaves·(1 − G))
   and `upperKneeHz` 4·f_c within 1e-3 relative (the lower moves with Gravity, the upper does not); `wide` / `solid` flags follow 800.
2. **Implement** plan §4.6.8: `field_` (`std::unique_ptr<Krate::DSP::BassStereoField>`, created in `initialize`, never prepared),
   `spreadModel_` (`UI::SpreadCurveModel`), `refreshSpreadModel()` steps 1 – 6, the hook for 800 – 805 and the mirror / Edit-Target
   re-display paths (plan §4.6.4), `spreadModelForTest()`. `bass_stereo_field.h` is not modified.

**Verified by:** G14 gate.

---

# GROUP 15 — `ADSRDisplay` adapter

## T025 — `adsrToRegistered` + `wireAdsrDisplay` + adapter arm of SC-024 (FR-034; plan §4.6.10, D-8)

**Files edited:** `plugins/profundum/src/controller/controller.h`, `plugins/profundum/src/controller/controller.cpp`,
`plugins/profundum/tests/unit/controller/custom_view_laws_test.cpp`.

1. **Test first** — `TEST_CASE("Profundum_UiCustomViewLaws", "[profundum][ui]")`, `SECTION("AdapterOnTestDisplay")`: a heap
   `ADSRDisplay` (shared header, unmodified) wired by `controller->wireAdsrDisplay(display)` with a `RecordingComponentHandler`; drive
   the display's public mouse path (`onMouseDown` at `getControlPointPosition(target)`, `onMouseMoved` by a delta, `onMouseUp`) for
   420, 421, 423 to the clamped minimum (far left), near 100 ms (`setAttackMs(100)` etc., then a 1-pixel drag) and the clamped
   maximum (far right), and 422, 424 – 426 to min / mid / max: every recorded `performEdit` value equals `normalizedFromPlain(law,
   plain)` within 1e-6, where plain is the display's own value after the drag (`getAttackMs()` …); the parameter's `toString` of the
   value round-trips to plain at its display precision; IDs outside 420 – 426 are never emitted. Reverse path:
   `setParamNormalized(420, v)` for v ∈ {0, 0.5, 1} leaves `getAttackMs()` equal to `plainFromNormalized(law, v)` within 1e-4
   relative (same for 421, 423). Negative arm: a raw-wired display (`setParameterCallback` straight to `performEdit`) emits a
   different value at 420 for the 100 ms drag.
2. **Implement** plan §4.6.10: `wireAdsrDisplay` (base IDs 420 / 424, max times `kMaxEnvelopeTimeMs`, `setBezierEnabled(false)`,
   the parameter / begin / end callbacks) and `adsrToRegistered` (table of plan §4.6.10); the reverse setters on
   `setParamNormalized` and re-display.

**Verified by:** G15 gate.

---

# GROUP 16 — Editor delegate and panel

## T026 — `VST3EditorDelegate`, `ProfundumPanelSubController`, editor sync timer, zoom, view-creator includes, `entry.cpp` banner (FR-013, FR-014, FR-030, FR-035, FR-041, FR-044, FR-060, FR-090 – FR-092; SC-015; plan §4.6.2, §4.6.4, §4.6.7, §4.6.9, §4.7, §4.13, D-10, D-11)

**Files edited:** `plugins/profundum/src/controller/controller.h`, `plugins/profundum/src/controller/controller.cpp`,
`plugins/profundum/src/ui/panel_sub_controller.h`, `plugins/profundum/src/ui/panel_sub_controller.cpp`,
`plugins/profundum/src/entry.cpp`, `plugins/profundum/tests/unit/controller/editor_sync_test.cpp`.

1. **Test first** — `TEST_CASE("Profundum_EditorSyncTimer", "[profundum][ui][lifecycle]")` (headless editor via
   `ui_tree_helpers.h` `HeadlessEditor`, on the current uidesc):
   - `SECTION("TimerExistsOnlyWhileOpen")`: `hasEditorSyncTimerForTest()` false before open, true after `didOpen`, false after
     `willClose`; `editorSyncIntervalForTest() == 100` (or the ratified period) and ≤ 100.
   - `SECTION("OneRequestPerTick")`: a `CountingConnectionPoint` peer; each `editorSyncTickForTest()` sends exactly one
     `kMsgRequestSync`; after `willClose` none is sent; with a gesture open (`beginEdit(200)`) a tick sends none (R-6).
   - `SECTION("SynchronousPathUpdatesDisplay")`: a real `ConnectedPair`, no timers pumped on either side: tick; a processor-side
     endpoint change delivered through `process()` (a parameter change on 200 for Edit Target A); tick; the controller's displayed
     200 equals the new value (the `Processor::notify(kMsgRequestSync)` → `flushControllerSync()` → `Controller::notify` path).
   - `SECTION("DelegateNames")`: `createCustomView("nonsense", …) == nullptr`; `createSubController("nonsense", …) == nullptr`;
     `createView(kEditor)` is a `VST3Editor`, `createView("other") == nullptr` (FR-092).
   - `SECTION("ZoomIsSessionState")`: `setZoomStep(i)` for i = 0 … 3 with the editor open sets `getZoomFactor()` to 1.0 / 1.25 /
     1.5 / 2.0; the step survives close / reopen; `getState` bytes identical before and after (SC-032 core; T027 adds the control arm).
2. **Implement**:
   - Controller: add `public VSTGUI::VST3EditorDelegate` (Vorago `controller.h:56–59` per plan); `createCustomView` (plan §4.6.2
     table: `HarmonicBarView` with `bar-variant`, `SpreadCurveView`; rect from `origin` / `size`), `createSubController("ProfundumPanel")`,
     `didOpen` (store editor, `setAllowedZoomFactors({1.0, 1.25, 1.5, 2.0})`, `setZoomFactor(kZoomSteps[zoomIndex_])`, start the
     timer, refresh rings / SPREAD / captions / dimming), `willClose` (stop and release the timer, null `barViews_`, `spreadView_`,
     `panel_`, `activeEditor_`); `beginEdit` / `endEdit` overrides (gesture count, macro edit forwarding); the remaining
     `setParamNormalized` hooks of plan §4.6.4 (414 → portamento readout, 1000 / 1001 → dimming); `registerPanel` and the raw
     `panel_` pointer, plus `if (panel_) panel_->onCompareModeChanged(compare_);` in `notify`'s `kMsgCompareMode` branch after
     T022's `++compareChanges_` (plan §4.6.3); zoom
     (`kZoomSteps`, `zoomIndex_`, `setZoomStep`); the sync timer (`kEditorSyncIntervalMs`, `onEditorSyncTick`, the three test seams);
     include the shared view-creator headers here (`ui/arc_knob.h`, `ui/bipolar_slider.h`, `ui/adsr_display.h`, and any other shared
     view the uidesc of T027 uses), as Vorago does (`plugins/vorago/src/controller/controller.cpp:35–40` per plan P-4).
   - `ProfundumPanelSubController` (namespace `Profundum::UI`) exactly per plan §4.7: session tags (`kSessionTagBase = 9000` and the
     table), `verifyView` collection rules, `valueChanged` routing (page / tab `setVisible`, compare → `pressCompare` then reset every
     compare segment to `compareMode()`, copy, name commit → `setEndpointName`, zoom), macro hover via `IViewEventListener`
     (`MouseEnter` / `MouseExit`, unregistered in the destructor), `onMacroEdit`, `setHighlight` per class (`ArcKnob::setGuideColor`,
     `BipolarSlider::setTrackColor`, `ADSRDisplay::setControlPointColor`), `refreshAnchorDimming` (alpha 0.45 / 1.0, "Held at A"
     label, mouse never disabled), `refreshPortamentoReadout`, `onCompareModeChanged`, test reads `highlightedIdsForTest()`,
     `isAnchorDimmedForTest(id)`, `portaReadoutInvalidationsForTest()`; `verifyView` calls `owner->wireAdsrDisplay` on the
     `ADSRDisplay`.
   - `entry.cpp`: the banner of plan §4.13 only.

**Verified by:** G16 gate (`Profundum_EditorLifecycle` still passes on the Phase 10 uidesc until T027).

---

# GROUP 17 — `editor.uidesc`

## T027 — `editor.uidesc` replaced wholesale + `ui_strings_test_table.h` + layout / binding / label / placement / zoom / lifecycle cases (FR-004, FR-010 – FR-014, FR-020, FR-022, FR-024, FR-030 – FR-036, FR-045, FR-050 – FR-052, FR-070, FR-080, FR-091 – FR-093; SC-002 – SC-005, SC-016, SC-017, SC-019, SC-031, SC-032; plan §4.12, §6.2 "Editor layout and binding", D-11)

**P-5 re-cut (2026-10-23):** every knob bound to a macro target ID (the FR-060 highlight sets except 501) uses the
`OffsetRingKnob` class in the uidesc; other continuous knobs stay `ArcKnob`. FR-032's class rule accepts it as an `ArcKnob`.

**Files edited:** `plugins/profundum/resources/editor.uidesc` (replaced), `plugins/profundum/tests/unit/controller/editor_layout_test.cpp`,
`plugins/profundum/tests/unit/controller/editor_lifecycle_test.cpp`. **Files created:**
`plugins/profundum/tests/ui_strings_test_table.h`.

1. **`ui_strings_test_table.h`** (namespace `ProfundumPluginTest`): the ratified D2 tooltip literals per ID (the 800 – 809 guarantee,
   the 600 – 602 WIDE caveat, 809 folding, 911 Bipolar rest), the text `MORPH BEHAVIOUR` and `ANCHOR MODE`, the forbidden words
   {"oscillator", "filter", "LFO", "partial"} and banned labels {"Oversampling", "Quality", "Low-End Protection", "Parameter Curve"},
   `kD6UiOnlyTags` (the D6 list: 9100, 9110, 9200 – 9205, 9210, 9211, 9220, 9221, 9300), the FR-011 Main set, the FR-004 section →
   ID table, the D7 minima and the designed size — each copied byte for byte from `design/`.
2. **Test first** — in `editor_layout_test.cpp` (helpers from `ui_tree_helpers.h`):
   - `TEST_CASE("Profundum_UidescBindsEverySurfaceId", "[profundum][ui]")` (SC-002): `unreachableParams(xml, all84, {4, 5, 6, 7})`
     (`tests/test_helpers/uidesc_reachability.h:88`) is empty; in the built tree the set of tags < 9000 equals the 80 visible IDs and
     contains none of 4 – 7.
   - `TEST_CASE("Profundum_UidescTagIntegrity", "[profundum][ui]")` (SC-003): every `extractControlTagMap` value is a registered ID;
     every `control-tag=` names a declared tag; the eight Phase 10 names keep their values (`git show 40f6ebca:plugins/profundum/resources/editor.uidesc`
     lines 19–28 give them; copy them as literals).
   - `TEST_CASE("Profundum_UiClassRule", "[profundum][ui]")` (SC-004, FR-032 as amended by P-8): per bound control, by
     `getParameterObject(id)` kind: a `StringListParameter` → `COptionMenu`, `CSegmentButton` or a two-state toggle (two-label lists
     only); otherwise → `ArcKnob`, `BipolarSlider`, `CSlider` or the `ADSRDisplay`; the one exemption: a `CParamDisplay` (exactly
     that class) bound to a continuous ID passes only if the same page binds the ID to a conforming writing control (checked for 105
     and 412 on Main; a readout without a writer fails); 417's knob honours its step count.
   - `TEST_CASE("Profundum_UiLabels", "[profundum][ui]")` (SC-005): the strings shown for 301 and for 1000 equal
     `getParameterObject(301/1000)->toString` of each index and the two sets are disjoint; XML scan: `MORPH BEHAVIOUR` inside
     `anchor-group`, which also holds the 1000-bound control; no 301-bound control in `page-main`; `ANCHOR MODE` labels the 301
     control in `section-sub-anchor`; no text or tooltip anywhere contains a banned label; each tooltip of 800 – 809 contains its
     ruled guarantee, of 600 – 602 the WIDE caveat, of 809 the folding statement, of 911 the Bipolar statement; no Main-page text or
     tooltip contains a forbidden word (case-insensitive) unless D1 rules it in.
   - `TEST_CASE("Profundum_Editor_PageSwitch", "[profundum][ui]")` (SC-017): `page-main` and `page-advanced` exist; selecting each
     page through the panel's page control makes exactly that container visible; the selection survives close / reopen; `getState`
     bytes identical before and after page switches.
   - `TEST_CASE("Profundum_UiLayout", "[profundum][ui]")` (SC-019): at each zoom step after `setZoomFactor`: every bound control's
     rect inside the frame; no two bound controls on the same visible page / tab overlap; every hit target × zoom ≥ the D7 minimum
     (segment cells ≥ 24 × 24); every font ≥ the D7 minimum; every bound control has a non-empty tooltip or an adjacent label;
     text contrast ≥ the D7 ratio (plan §5.6, colours from the uidesc colour table); FR-022 (XML scan): every 105-bound control has
     a `morph-anchor` ancestor and a 1000-bound control descends from it; Main binds 900 and none of 901 – 914.
   - `TEST_CASE("Profundum_UiPlacement", "[profundum][ui]")` (SC-031): (a) Main's bound-ID set equals the FR-011 set (plus 800 only
     if D1 rules the mirror) and contains none of 205, 102, 301; (b) 205, 102, 301 are bound on Advanced and every FR-004 row ID is
     bound inside its `section-*` container, incl. `section-morph-behaviour` holding 1000, 1002, 9203, 9204, 9205 and
     `section-sub-anchor` holding 1000; (c) `tab-modulation` and `tab-settings` are inside `page-advanced`, and the page containers
     are exactly `page-main` and `page-advanced`; (d) the 800 control and the `SpreadCurveView` are in `section-stereo-output` and no
     `SpreadCurveView` is in `page-main`; (e) after one `onTickForTest` on a delivered frame, the two `HarmonicBarView`s' bar heights
     are equal; (f) every tagged `CControl` is bound to a registered ID (< 9000) or carries a tag in `kD6UiOnlyTags`, and every
     `kD6UiOnlyTags` entry is present.
   - `TEST_CASE("Profundum_UiZoom", "[profundum][ui]")` (SC-032): selecting each step of the 9300 control calls `setZoomFactor` with
     1.0 / 1.25 / 1.5 / 2.0 (`getZoomFactor()`); the control is bound to no registered ID; the parameter count stays 84; `getState`
     identical before / after; the size constraints equal the designed size (no other size offered).
   - **`editor_lifecycle_test.cpp`** (SC-016): `HarnessCycles` runs 10 cycles; `EditorBindsSurface` is replaced by
     `SECTION("EditorBindsSurfaceV12")` asserting the SC-002 counts (80 bound IDs, none of 4 – 7); new sections: exactly 2
     `HarmonicBarView` and 1 `SpreadCurveView` in the built tree, every controller view pointer null after `willClose`
     (`viewPointersNullForTest()`); a frame delivered while closed updates only the cache and both bar views show it on reopen; an
     attach / remove arm runs 10 times on each timer-owning view and asserts the timer exists only while attached.
     `PresetConfigIsLive` and `CreateViewNames` are unchanged.
3. **Implement** `editor.uidesc` exactly per plan §4.12 and the ratified D1 / D2 / D7: template `editor` at the designed size with
   `minSize = maxSize`, `sub-controller="ProfundumPanel"` on its root; header; sibling `page-main` / `page-advanced`
   `CViewContainer`s (no `UIViewSwitchContainer`); three tab containers inside Advanced; one `section-*` container per FR-004 row;
   Main's `mass-group` and `morph-anchor` (holding exactly `anchor-group` and `morph-band`); `<control-tags>` for the 80 visible
   IDs with the eight Phase 10 names unchanged and no tag for 4 – 7; the colour tokens of D7; a `tooltip` on every bound control
   with the ratified literals; the class per kind of plan §4.12; custom views `HarmonicBarView` (two, `bar-variant` main / core) and
   `SpreadCurveView` (one); `session-tag` attributes for every UI-only control. Write it whole once; any later change touching more
   than 3 controls uses XSLT (repo rule).

**Verified by:** G17 gate.

---

# GROUP 18 — Morph component UI arms

## T028 — Compare / copy / readout UI, Lock dimming, portamento readout, name commit (FR-035, FR-040 – FR-045, FR-047, FR-049; SC-007 UI arms, SC-008, SC-018, SC-027 (a) UI arm; plan §4.7, §6.2 "Morph component")

**Files edited:** `plugins/profundum/tests/unit/controller/morph_component_test.cpp`, `plugins/profundum/tests/unit/state_v3_test.cpp`
(and production files only if a test exposes a defect in T022 / T026 / T027 code — name it in notes).

1. **Test first** (built tree via `HeadlessEditor` on a `ConnectedPair`):
   - `Profundum_MorphComponentUi` `SECTION("SegmentsShowControllerState")` (SC-007 (a)(b) UI): clicking each segment of 9200 and of
     9203 produces the T022 messages / edits; with `toController.hold = true` both segments' selected index is unchanged after a
     click; after `releaseHeld()` both show `compareMode()`; the count rose by exactly 1 per accepted message.
   - `SECTION("CopyButtons")` (c): 9201 / 9204 send `"from"` 0, 9202 / 9205 send `"from"` 1, one message each.
   - `SECTION("PositionReadout")` (d): the 105 `CParamDisplay`'s text for p ∈ {0, 0.005, 0.42, 0.995, 1} equals
     `getParamStringByValue(105, p)`, i.e. "0%", "0%", "42%", "100%", "100%"; the readout has no formatter of its own.
   - `SECTION("EditTargetControl")` (e): selecting B in the 1001 control gives one begin / perform / end on 1001 and the T022 (e)
     re-display.
   - `TEST_CASE("Profundum_MorphLockDimming", "[profundum][ui][morph]")` (SC-008): every combination of 1000 ∈ {Hold, Blend, Yield}
     × 1001 ∈ {A, B} through `setParamNormalized`: `isAnchorDimmedForTest(300 … 306)` is true exactly for (Hold, B); the "Held at A"
     label is visible exactly then; every 300 – 306 control's `getMouseEnabled()` stays true.
   - `TEST_CASE("Profundum_UiPortamentoUnitFollowsGlideMode", "[profundum][ui]")` (SC-018): stepping 414 through its three values
     changes the unit suffix of the 412 display's value-to-string output each time; `portaReadoutInvalidationsForTest()` rises by
     exactly 1 per 414 step and not on a change of 200; the panel's collected display is the 412-bound `CParamDisplay` of the built
     tree (same pointer); 412's normalised value is unchanged.
   - `Profundum_EndpointNames` `SECTION("TextEditCommit")` (SC-027 (a) UI arm): committing "IRON FOLD" in the 9211 `CTextEdit` sends
     exactly one `kMsgEndpointName` (endpoint 1) and the 9221 caption reads the T023 caption of B.

**Verified by:** G18 gate.

---

# GROUP 19 — Macros on the built tree

## T029 — Highlight, rings on knobs, MIDI learn (FR-060, FR-061, FR-098; SC-009, SC-028 knob arm, SC-029; plan §4.7 "Macro highlight", §6.2 "Macros")

**Files edited:** `plugins/profundum/tests/unit/controller/macro_ui_test.cpp` (and production files only if a test exposes a defect —
name it in notes).

1. **Test first** (built tree via `HeadlessEditor`):
   - `TEST_CASE("Profundum_MacroHighlight", "[profundum][ui]")` (SC-009): synthetic `MouseEnter` / `MouseExit` events through
     `viewOnEvent` on each macro control; for **every** bound control, its highlighted state (class getter `getGuideColor` /
     `getTrackColor` / `getControlPointColor` == the `highlight` token) equals membership of its ID in the set derived in the test
     from `kRows` (amount ≠ 0, Compression excluded); Dark ↔ Bright's set excludes 501; MouseExit clears every highlight;
     `beginEdit` / `endEdit` on 100 – 103 with no hover highlight m's set while open, keep it after a `MouseExit` mid-gesture, and
     clear it on `endEdit`; a keyboard / wheel edit (begin / perform / end without hover) behaves the same; a highlight-set ID bound to
     a class the panel cannot highlight fails the case; `highlightedIdsForTest()` equals the same set.
   - `Profundum_MacroModulationRing` `SECTION("KnobsCarryRing")` (SC-028): for each macro value of T023, each bound
     `OffsetRingKnob` target's `getOffset()` equals `macroRingRange(id)` within 1e-6 and its `ringSpan()` lies on the
     offset's side of the value (P-5); at neutral the offset is exactly 0; the 501 control and controls not bound to an
     `OffsetRingKnob` carry no ring; knob values unchanged.
   - `TEST_CASE("Profundum_MidiLearnHostLevel", "[profundum][ui]")` (SC-029): `getParameterInfo` of 100 – 103 and 105 has
     `kCanAutomate` and not `kIsReadOnly`; `queryInterface(IMidiLearn::iid)` returns `kNoInterface`; no text or tooltip in the built
     tree contains "Learn"; the Phase 11 `IMidiMapping` cases pass unedited.

**Verified by:** G19 gate.

---

# GROUP 20 — Built-tree arms of SC-014 and SC-024

## T030 — `SpreadCurveView` on the built tree; the panel-wired `ADSRDisplay` (SC-014 view arm, SC-024 built-tree arm; plan §6.2)

**Files edited:** `plugins/profundum/tests/unit/controller/spread_curve_test.cpp`,
`plugins/profundum/tests/unit/controller/custom_view_laws_test.cpp`.

1. `Profundum_SpreadCurveMatchesField` `SECTION("BuiltView")`: with the editor open, a subset of the T024 grid (every Spread ×
   mode, Gravity {0, 1}, crossover {60, 250}, curve 0.5, depth {0, 1}): the built `SpreadCurveView`'s `sampledValuesForTest()`
   equals the reference w within 1e-6 and its `kneeMarksForTest()` the T024 knees; it is invalidated after each change.
2. `Profundum_UiCustomViewLaws` `SECTION("BuiltTreeDisplay")`: the `ADSRDisplay` taken from the built tree (the instance the panel's
   `verifyView` wired, not a test-built one) driven through its public mouse path as in T025: the same 1e-6 and round-trip
   assertions at 420, 421, 423 (min / ~100 ms / max) and 422, 424 – 426; `SECTION("BarViewWritesNothing")`: mouse events on both
   built `HarmonicBarView`s record 0 `performEdit`.

**Verified by:** G20 gate.

---

# GROUP 21 — Main-page sanity renders

## T031 — `Profundum_MainPageSanity` (FR-006, FR-024; SC-006 (c); plan §6.2 "Main page render")

**Files edited:** `plugins/profundum/tests/integration/main_page_sanity_test.cpp`.

1. **Test** — `TEST_CASE("Profundum_MainPageSanity", "[profundum][ui][render]")`: renders the D1 item 8 render set (the same
   list T010 reads) through a heap `ProcessorFixture` at E1 and C3, 48 kHz, with an observer on the generator tap. Per render:
   every sample finite (`detail::isFinite`); RMS over [0.2 s, 3 s) > −60 dBFS; sample peak ≤ the ID 3 default ceiling (−1 dBTP as
   a linear value, 10^(−1/20)); on the observer's `coreL / coreR` the sub band ρ ≥ 0.9989; on the processor output
   `StereoFieldTest::bandReading(L, R, n, fs, 0, f_c)` (`dsp/tests/unit/systems/bass_stereo_field_test_helpers.h:736`) gives
   ρ ≥ `kSubBandRhoBar` (0.99) and |Q| ≤ `kSubBandQBarDb` (1.0 dB) with the Phase 9 SC-015 band definition, gated as in
   `plugins/profundum/tests/integration/mono_sum_params_test.cpp:338–360`. The 0.9989 bar is never applied to the output.
   Non-vacuity: a control arm with Spread 801 = 1, image mode Wide and Gravity 0 must still pass the output bar (it is the measured
   guarantee), and a deliberately mono-broken stimulus (R = −L on the generator tap copy) must fail the 0.9989 check — assert that
   the checker returns false for it.
   If this case measures > 15 s wall clock at the gate, tag it `[long]` in a follow-up and tell the main loop (render cases over
   ~15 s are `[long]` by repo rule; it is then part of T036's own-TU `[long]` lane).

**Verified by:** G21 gate.

---

# GROUP 22 — CPU arms (written here; run only by T038)

## T032 — Producer-on arms in `Profundum_ProcessorCpu` and `Profundum_ProcessorCpuFullSurface` (FR-075; SC-013; plan §6.2 "CPU", R-3)

**Files edited:** `plugins/profundum/tests/integration/processor_cpu_test.cpp`.

Add to each existing case (`:214` and the full-surface case) a producer-on arm on the **same** instance via
`setSpectrumProducerEnabledForTest(true)`, measured exactly like its producer-off twin, with `WARN` lines naming the arm and
`REQUIRE(pct ≤ kMonoPctBar)` (8 %) / `≤ kPolyPctBar` (25 %), and the P / D ratio and control-window ceilings unchanged (1.05). No bar
is relaxed and no workload shrunk. The TU stays out of the `-fno-fast-math` block.

**Executor:** write the arms; return done with notes "deferred to the main loop at close: T038 runs the CPU lane alone".

**Verified by:** compile at the G22 gate (the cases are hidden `[.perf]`).

---

# GROUP 23 — Docs and table regeneration

## T033 — Plugin `CLAUDE.md`, `CHANGELOG.md`, D3 / D4 regeneration script (FR-005, FR-007; plan §0.2 "Docs", §3.2)

**Files edited:** `plugins/profundum/CLAUDE.md`, `plugins/profundum/CHANGELOG.md`. **Files created:**
`specs/profundum-phase12-ui/evidence/close/regen-tables.sh`.

1. `CLAUDE.md`: the state v3 table (bytes 510 – 573: two 32-byte name blocks; v1 / v2 still load), `kMsgEndpointName`, the UI notes
   (pages, sub-controller, custom views, DataExchange frame `0x50524644`, the sync timer, zoom as session state, "the spectrum is
   empty in a host with neither DataExchange nor a run loop"), and the Phase 12 test cases (one line each).
2. `CHANGELOG.md`: bullets under the open `## [0.1.0] - Unreleased` entry (no re-bump; version.json stays 0.1.0): the editor (Main
   and Advanced pages), the live harmonic spectrum, Bass Morph compare / copy / named endpoints, macro highlight and rings, zoom.
   User-facing wording.
3. `regen-tables.sh`: builds `profundum_tests` once, runs `profundum_tests.exe "Profundum_UiContractTables"` to a log, splits the
   two CSVs, replaces the text between the `GENERATED-BEGIN` / `GENERATED-END` markers of `design/macro-mapping.md` and
   `design/morph-laws.md` (a Node helper `regen-tables.js` next to it), and prints `git diff --stat` of the two docs; last line
   `TABLES IDENTICAL|TABLES CHANGED`. `bash -n` it.

**Executor:** write all three; return done with notes "deferred to the main loop at close: run `regen-tables.sh`; a CHANGED result
is a review item, not an automatic accept".

**Verified by:** G23 gate (no code change).

---

# GROUP 24 — CMake registration (single task)

## T034 — CMake registration and audit

**Files edited:** `plugins/profundum/CMakeLists.txt` (and only fixes found by the audit).

- Add the headers: under `# UI (Phase 12)`: `src/ui/harmonic_bar_view.h`, `src/ui/spread_curve_view.h`,
  `src/ui/panel_sub_controller.h`, `src/ui/macro_ui_map.h`, `src/ui/endpoint_descriptor.h`; under the processor lines:
  `src/processor/spectrum_frame.h`, `src/processor/spectrum_producer.h`; under `# Parameter Packs`: `src/parameters/endpoint_names.h`.
- **Audit** (counts into `T034.txt`): each of those 8 headers and each of the 3 `src/ui/*.cpp` appears exactly once in
  `plugins/profundum/CMakeLists.txt`; `plugins/profundum/tests/CMakeLists.txt` names each of the 11 Phase 12 test TUs (12 with T010's)
  once in `add_executable`, the 3 `src/ui/*.cpp` once in the SECOND-compilation block, the Phase 12 bit-pattern TUs once in the
  `-fno-fast-math` block, `processor_cpu_test.cpp` in no flag block, `krate_test_pch` last; `dsp/tests/CMakeLists.txt` names
  `profundum_engine_display_test.cpp` once. No `/wd`, `-Wno-` or `pragma warning` added anywhere
  (`git diff 40f6ebca -- '*.txt' '*.h' '*.cpp' | grep -cE "/wd|-Wno-|pragma warning"` = 0).

**Verified by:** G24 gate (`Profundum` builds with 0 warnings).

---

# GROUP 25 — Integration lanes

## T035 [P] — Scope fence, ODR re-sweep, registration greps (FR-095, FR-096; SC-021)

**Files created:** `specs/profundum-phase12-ui/evidence/close/scope-fence.js`, run-log `T035.txt`.
- `scope-fence.js` (Node): the union of `git diff --name-only 40f6ebca` and `git status --porcelain --untracked-files=all` must
  match the allow-list `plugins/profundum/**`, `dsp/include/krate/dsp/systems/profundum_engine.h`,
  `dsp/include/krate/dsp/systems/profundum_voice.h`, `dsp/tests/unit/systems/profundum_engine_display_test.cpp`,
  `dsp/tests/CMakeLists.txt`, `specs/profundum-phase12-ui/**`, `specs/Profundum-roadmap.md` (`.serena/project.yml` excluded by name),
  plus (FR-095 allow-list addition 2026-10-24) the nine test-only `dsp/tests/unit/*/*helpers.h` files named in FR-095,
  `.gitignore`, `specs/profundum-phase9-voice-engine/compliance.md` and `specs/profundum-phase9-voice-engine/evidence/listening-sheet.md`.
  Earlier phases' regenerated evidence is restored to HEAD before the fence runs.
  Any `plugins/shared/` path or other `dsp/` path is RED. A second check: `git diff 40f6ebca -- dsp/include` contains only `+` lines
  (append-only) inside the two public sections. Prints offending paths and `SCOPE GREEN|RED`.
- Greps into `T035.txt`, each with its expected count: every claimed name of T001 step 4 is defined exactly once (in its planned
  file); `grep -rn "IMidiLearn" plugins/profundum/src` = 0; `grep -rn "compareModeForTest" plugins/profundum/src/ui` = 0 (FR-046);
  `grep -rnE "std::isnan|std::isinf|std::isfinite|std::fpclassify" plugins/profundum/src plugins/profundum/tests` = 0 new hits.
- **Executor:** write and run (seconds); quote outputs. A FAIL in a Phase 12 file is fixed here.

## T036 [P] — Suites lane, pluginval, clang-tidy, ASan, tables — **deferred to the main loop** (SC-016 ASan, SC-020, SC-021, SC-022 lane, FR-095)

**Files created:** `specs/profundum-phase12-ui/evidence/close/suites.sh`, `evidence/close/tidy.sh`, `evidence/close/asan.sh`.

**Lane scope (encoded 2026-10-18, Phase 6 ruling G9-4; FR-095).** Executables: `profundum_tests` (the phase's TUs and the amended
Phase 10 / 11 TUs) and `dsp_systems_tests` (the consumer of the two modified headers). Never every `[long]` case of an executable.

`suites.sh` runs detached and writes `evidence/run-log/close/status.txt` (one line per lane: name, exit code, wall clock):
1. **Change-set scope.** `git diff --name-only 40f6ebca -- dsp/include dsp/src plugins/shared tests/test_helpers` (tracked,
   modified) must print exactly the two `systems/profundum_{engine,voice}.h` headers; anything else is RED and stops the script. The
   consumer set is recomputed with `grep -rlE "profundum_engine\.h|profundum_voice\.h" dsp plugins tests` and written to the status
   file (expected: `dsp/tests/unit/systems/profundum_*`, `mass_dynamics_oversampling_test.cpp`, `plugins/profundum/` — Ruinae,
   Seraphis, Vorago and Innexus include neither header).
2. One build of `Profundum profundum_tests dsp_systems_tests` with `-- /nr:false` to `build.log`; warning count
   (`warning C[0-9]|warning:`) must be 0 (SC-021). Wall clock recorded.
3. Concurrently, each to its own log with its wall clock:
   - (a) `profundum_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"` → `All tests passed`;
   - (b) `dsp_systems_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"` (the consumer's per-push set, FR-095) →
     `All tests passed`;
   - (c) the `[long]` cases of the **phase's own TUs only**: `profundum_tests.exe --list-tests "[profundum][long]"` filtered to the
     Phase 12 TEST_CASE names of this file (none is `[long]` at cut time; `Profundum_MainPageSanity` joins if T031 re-tagged it). A
     listed name outside that set is reported, not run;
   - (d) the 46 consumer `[long]` names of `dsp_systems_tests` are **listed** (`--list-tests "[profundum][long]"`) into the status
     file and not run (Deviation 6), unless the user's T009 ruling says run them — then one process per case via
     `xargs -P <min(8, cores − 4)>`;
   - (e) `--list-tests` shows every SC case name of the spec, and `"[.perf]" --list-tests` shows both CPU cases;
   - (f) `tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Profundum.vst3` → exit 0
     (SC-020, editor open / close included);
   - (g) `bash specs/profundum-phase12-ui/evidence/close/regen-tables.sh` (T033).
4. A summary block with every lane's verdict and wall clock.

`tidy.sh`: reconfigure `build/windows-ninja` inside vcvars64 (test CMake changed; the stale-database hazard), confirm the new TUs and
`src/ui/*.cpp` are in `compile_commands.json`, then `./tools/run-clang-tidy.ps1 -Target profundum -BuildDir build/windows-ninja`
and the dsp TU by file list (the Phase 12 dsp TU and the two headers' includers it compiles; never the whole `dsp` target), each to
its own log under a 900 s wall-clock ceiling; last line `TIDY GREEN|RED` (0 warnings).

`asan.sh`: configure `build-asan` with `-DENABLE_ASAN=ON` (Debug), build `profundum_tests`, run `profundum_tests.exe "[lifecycle]"`
to `evidence/run-log/close/asan.log`; last line `ASAN GREEN|RED` (SC-016). Runs concurrently with `suites.sh` (separate build dir).

**Executor:** write the three scripts, `bash -n` each, and return done with notes "deferred to the main loop at close: run
`suites.sh` detached, `tidy.sh` and `asan.sh` concurrently; quote each lane's last lines into `evidence/run-log/T036.txt`; a Phase 12
red is owned (fix, re-run only the affected case or lane)". A timing-budget assertion of an untouched case that fails under the
concurrent lanes is recorded as load, not re-run.

## T037 [P] — Portability (SC-021)

**Files created:** `specs/profundum-phase12-ui/evidence/close/portability.sh`, run-log `T037.txt`.
1. The `.cpp` list = `git diff --name-only 40f6ebca` ∪ untracked, filtered to `\.cpp$` (expected: `processor.cpp`, `controller.cpp`,
   `entry.cpp`, the three `src/ui/*.cpp`, every new and amended Phase 12 test TU, `dsp/tests/unit/systems/profundum_engine_display_test.cpp`).
   `node tools/check-portability.js <files…>` with a 300 s ceiling; required `check-portability: all clear -- N compiled.` with no
   skipped suffix and N equal to the list length.
2. `node tools/lint-apple-globals.js` over the new headers and TUs (re-read its usage line first).
3. `wsl --shutdown`.
Output to `evidence/run-log/close/portability.log`, last line `PORTABILITY GREEN|RED`.
**Executor:** write and run once. If it fails with `wsl: Failed to translate` or exceeds 5 minutes, stop it and return done with
notes "deferred to the main loop at close: run `bash specs/profundum-phase12-ui/evidence/close/portability.sh` unsandboxed".

## T038 [P] — CPU lane, alone (SC-013) — **deferred to the main loop**

**Files created:** `specs/profundum-phase12-ui/evidence/close/cpu-lane.sh` (runs `node tools/run-cpu-tests.js profundum_tests` to
`evidence/run-log/close/cpu.log`; last line `CPU GREEN|RED`).
- Precondition: T036 and T037 lanes finished; the machine idle.
- **Executor:** write the script, `bash -n` it, return done with notes "deferred to the main loop at close: run
  `cpu-lane.sh` last and alone; quote every arm's percentage and ratio (producer off and on: MONO, POLY, MONO-MAX, POLY-MAX,
  automation arms) into `evidence/run-log/T038.txt`".
- A red reading is re-run once after the machine has idled; still red → a defect reported to the user. No bar relaxed, no capacity
  or cadence changed (FR-075), no workload shrunk.

---

## Close procedure (main loop, in this order)

1. G1 – G4 as the workflow runs them. If T010 is `blocked`, take the P-1 ruling into the FR-001 row, then resume with
   `unblock: ["T010"]` (or re-cut T010 if ruled no).
2. Run `evidence/listen/render.sh` (T010); hand the user the WAVs for SC-006 (b) and the Phase 9 FR-097 renders.
3. G5: T011 blocks until the user ratifies D1 – D7 and the plan questions. Record each ruling in its FR / SC row first, then
   Clarifications; resume with `unblock: ["T011"]`.
4. G6 – G24 gates as the workflow runs them.
5. T036 `suites.sh` detached + `tidy.sh` + `asan.sh` concurrently (and `portability.sh` if T037 deferred it).
6. Any red → triage into "Defects found", fix, re-run only the affected case or lane.
7. T038: the CPU lane, alone, after the machine has idled.
8. `node tools/clean-test-output.js --apply` (memory rule: clean up test output at every close).
9. Comply quotes the run-log files. The macOS / Linux CI legs of SC-021 are **PENDING-CI** until the user pushes.

## Open questions carried from the plan (built at the plan's reading; surface, do not decide)

- **Plan OQ-1 (P-1):** may the hidden render TU land before ratification? T010 blocks until ruled.
- **Plan OQ-2 (P-2):** SC-004 wording for the `getState` length assertions. T011 checks the ruling.
- **Plan OQ-3 (P-5):** RULED 2026-10-23: one-sided plugin-local `OffsetRingKnob` (re-cut in T016 / T023 / T027 / T029).
- **Plan OQ-4:** Copy does not copy names.
- **Plan OQ-5:** the §3.1 proposals, ratified via D1 / D2 / D7.
- **Plan OQ-6 (P-8):** FR-032 readout exemption. T011 checks the ruling.
- **Plan OQ-7 (P-9):** no preset field.
- **This task list, Deviation 6:** the 46 consumer `[long]` cases of the two modified `dsp/` headers are listed, not run.

## Defects found

(Empty at task-cut time. Gates, fixers and the main loop record entries here: task, case / section, verbatim reading, and triage.)

- **G18 / T028, Profundum_MorphComponentUi / EditTargetControl** (morph_component_test.cpp:492): `CHECK( countIn(h.performEdits, kEditTarget) == 1 )` read `2 == 1`. Triage: production defect (a VSTGUI echo), not a test error; SC-007 (e) asks for one perform. ParameterChangeListener::performEdit calls setParamNormalized (vst3editor.cpp:226-233). The parameter change is immediate (vstparameters.cpp:73-76). It reaches updateControlValue, which calls `c->valueChanged()` (vst3editor.cpp:349) on the segment, which is still editing. That re-enters VST3Editor::valueChanged and sends a second performEdit of the same value. Fix: Controller::performEdit drops a performEdit of the tag currently inside setParamNormalized (`paramBeingSet_`, controller.h/.cpp). The outer call sends the same value right after. Green at the G18 re-gate.
- **G18 re-gate build, C4388 in profundum_tests** (param_path_audibility_test.cpp, through the Catch decomposer): `REQUIRE(<unsigned> OP <int literal>)` in shared test helpers. Fix: a `u` suffix on 45 literals, at each site MSVC reported (dsp/tests/unit/{processors,systems}/*_helpers.h, plugins/profundum/tests/param_path_audibility_helpers.h:282). The build is now warning-free.

## T039 — Main-page audibility rows for 1, 200, 201, 204, 1000 (SC-006 (a), ruled 2026-10-24) — main loop

**Files created:** `plugins/profundum/tests/integration/main_page_audibility_test.cpp`. **Files edited:**
`plugins/profundum/tests/CMakeLists.txt` (both lists: sources and the `-fno-fast-math` list),
`specs/profundum-phase12-ui/design/macro-mapping.md` (the six D3 rows), run-log `T039.txt`.
- `TEST_CASE("Profundum_MainPageAudibility", "[profundum][audibility]")`, one SECTION per ID, contrasts exactly as in the SC-006
  (a) ruling, processor path only (`ppRenderProcessor`, `param_path_audibility_helpers.h`), E1 and C3, `CHECK(value >= bar)`
  per note. If the case takes more than about 15 s, tag it `[long]`.
- D3: the five rows cite the SECTION and the measured values; the 1001 row cites the exemption and AT-41 / AT-42.

## T040 — Plugin-local Highway Goertzel kernel (FR-075, SC-013; ruled 2026-10-24) — main loop

**Files created:** `plugins/profundum/src/processor/goertzel_bank_simd.h`, `plugins/profundum/src/processor/goertzel_bank_simd.cpp`.
**Files edited:** `plugins/profundum/src/processor/spectrum_producer.h` (calls the kernel per segment),
`plugins/profundum/CMakeLists.txt` and `plugins/profundum/tests/CMakeLists.txt` (the .cpp in BOTH lists; `hwy` linked PRIVATE),
`plugins/profundum/tests/unit/spectrum_producer_test.cpp`, run-log `T040.txt`.
- Failing test first: `TEST_CASE("Profundum_GoertzelBankSimdMatchesScalar", "[profundum][spectrum]")`. For count 1..64 and
  m 1..32, on seeded inputs and coefficients 2 cos(n w1), the kernel's four state arrays equal a scalar reference of the same
  recurrence within 1e-12 relative (absolute 1e-12 floor).
- The kernel uses `hn::ScalableTag<double>`, `LoadU` / `StoreU` only (`tools/lint-simd-aligned-loadstore.js`), a scalar tail
  for count % Lanes, and no allocation. Then the `[spectrum]` cases, `Profundum_SpectrumFrameAllocationFree`, the CPU pair alone
  (pinned), check-portability and lint-apple-globals.
