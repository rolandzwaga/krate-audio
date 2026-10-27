# Tasks: Profundum Phase 14 — Morph Modulation

**Spec:** `specs/profundum-phase14-morph-modulation/spec.md` (Clarifications Q1–Q8, C1, C10, 2026-10-26) · **Plan:**
`specs/profundum-phase14-morph-modulation/plan.md` (§0–§11; plan-stage findings P-1 … P-9; §11 open questions 1–4) ·
**Roadmap:** `specs/Profundum-roadmap.md` Phase 14 (`:1075-1117`), cross-cutting constraints (`:1135-1158`) · **Phase base:**
`3dfc5b7d` on `feat/profundum-preset-morph`
**Status:** Task list (2026-10-26). The build stage has not started. No commit tasks: commits happen outside this workflow.

**Deliverables.** One new KrateDSP class `ProfundumMorphModulator` (L3, `dsp/include/krate/dsp/systems/profundum_morph_modulator.h`);
append-only edits to `profundum_engine.h`, `profundum_engine_observer.h` and the Phase 9 test driver
`profundum_voice_engine_test_helpers.h`; nine host parameters 1003–1011 (`parameters/morph_mod_params.h`, new); state v3 grown
574 → 610 bytes (version stays 3); the C-7 Mod Wheel hand-over and the C-8 Compare bypass in the processor; the controller
mirror; the Main `morph-band` (Source, Amount) and the Advanced → Modulation block (all nine); the display-only
`MorphIndicatorSlider` fed by one float appended to `ProfundumSpectrumFrame` (288 → 320 bytes); the plugin render harness
carrying the modulation; the pre-phase harvest and its close comparison; the probe, the four `MorphMod` coverage columns and
the factory-library conversions; tooltips, `MANUAL.md` and the open `[1.0.0]` CHANGELOG entry. All paths are repo-relative to
`f:/projects/iterum`. `EVID` below means `specs/profundum-phase14-morph-modulation/evidence`.

---

## Read this first (every executor)

### Canonical order inside every task

1. Write the failing test **first**, in the named file and `TEST_CASE`, with the exact assertions given. A case that guards
   behaviour an earlier task built names its **non-vacuity arm** (the control that proves it can fail).
2. Implement the minimum that makes it pass, exactly as the cited plan section pins it.
3. Zero warnings (`warning C[0-9]|warning:` count 0). "Pre-existing" is not a category.
4. Tests pass at the group gate.

**Executors do not build or run tests**; the group gate does, after every group (`.claude/workflows/profundum-phase.js:572-582`):
it builds the dispatched targets and runs `'[profundum]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]'`.
**Every TEST_CASE of this phase carries `[profundum]` and `[morphmod]`** plus its layer / component tags (DSP: `[systems]`;
plugin: `[state]`, `[midi]`, `[ui]`, `[preset]`, …). Hidden cases (`[.harvest]`, `[.probe]`, `[.perf]`) and `[long]` cases are
written by executors and run by the main loop. Test targets of this phase: `dsp_systems_tests`, `dsp_ieee_tests` (it compiles
`profundum_engine_test.cpp`, `profundum_voice_test.cpp`, `profundum_engine_nonfinite_test.cpp`, consumers of the modified engine
header: `dsp/tests/CMakeLists.txt` `dsp_ieee_tests` list), `profundum_tests`, `Profundum`, `profundum_preset_generator`.

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests dsp_ieee_tests profundum_tests Profundum profundum_preset_generator -- /nr:false 2>&1 | tee F:/tmp/p14-build.log | tail -5
grep -cE "warning C[0-9]|warning:" F:/tmp/p14-build.log      # must print 0
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[morphmod]~[.]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/profundum_tests.exe "[profundum]~[long]~[.]" 2>&1 | tail -5
```

Catch2 filters are positional; tags in brackets; never `ctest -R`. The post-build copy to `C:/Program Files/Common Files/VST3/`
may fail with a permission error; compilation still succeeded. Build with `-- /nr:false` (MSBuild node reuse blocks probes).

**Gate scope note (shared-header rule).** This phase MODIFIES two tracked `dsp/include` headers (`profundum_engine.h`,
`profundum_engine_observer.h`), so the workflow's "modified tracked shared file" clause applies. Their consumers are
exclusively Profundum TUs, and every TEST_CASE in them carries `[profundum]` (checked 2026-10-26: 14 `dsp/tests/unit/systems/`
TUs include the engine header, the observer or the test driver, and every TEST_CASE in them carries the tag — the two whose tags
sit on the next line, `MassDynamics_OversamplingOffUnchanged` `mass_dynamics_oversampling_test.cpp:259-260` and
`VoiceEngine_MonoReplaceClickless_Retrigger` `profundum_engine_click_test.cpp:1158-1159`, included; every TEST_CASE under
`plugins/profundum/tests/` carries it too, and those TUs reach the engine directly or through `profundum_test_fixture.h`). The `[profundum]` gate
filter is therefore the consumers' per-push set; no other instrument's suite consumes these headers.

### Parallelism

`[P]` tasks inside one group create **only brand-new files**, disjoint from every other task in the group. Every task that edits
a file that already exists — any CMake list, `profundum_engine.h`, `profundum_engine_observer.h`, the test driver,
`processor.h/.cpp`, `controller.h/.cpp`, `plugin_ids.h`, `sound_param_table.h`, `spectrum_frame.h`, `spectrum_producer.h`,
`panel_sub_controller.cpp`, `editor.uidesc`, `MANUAL.md`, `CHANGELOG.md`, `tools/profundum_preset_defs.h`, any harness header,
`spec.md`, any test TU created by an earlier group (including the G2 placeholders) — is in a sequential group. All Profundum
plugin test TUs link into the one `profundum_tests` binary.

### Main-loop tasks (STOP AND SURFACE)

Any task whose work is a run longer than ~5 minutes (the pre-phase harvest, tree generation, the probe, `[long]` library lanes,
suites, clang-tidy, the macOS fast-math repro, CPU, pluginval) writes its lane script and returns without running it. Where a
**later group depends on the run's result** (T005 harvest, T015 / T025 trees, T022 probe, T026 library lanes) the task returns
status **`blocked`** with notes "main loop: run `<script>`; unblock `<Tnnn>` when `<condition>`", because a `done` whose notes say
"stop" is not honoured. Close-lane tasks (G21) return `done` with notes "deferred to the main loop at close: run `<script>`". The
main loop records every reading in `EVID/run-log/<Tnnn>.txt` (wall clock, core use, verdict line).

### Rules for every file of this phase

- **`dsp/` fence (FR-019 as amended by P-8 in T001).** The only `dsp/` files this phase touches: NEW
  `dsp/include/krate/dsp/systems/profundum_morph_modulator.h`, `dsp/tests/unit/systems/profundum_morph_modulator_test.cpp`,
  `dsp/tests/unit/systems/profundum_engine_morphmod_test.cpp`; append-only EDITS of
  `dsp/include/krate/dsp/systems/profundum_engine.h`, `.../profundum_engine_observer.h`,
  `dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h`, `dsp/tests/CMakeLists.txt` (two registration lines),
  `dsp/tests/unit/systems/profundum_engine_perf_test.cpp`, `profundum_engine_display_test.cpp`,
  `profundum_voice_engine_helpers_test.cpp` (appended cases / one appended SECTION; no existing case changes). The build stage is
  authorised to edit these from its first task and MUST NOT stop at a `dsp/` scope fence for them (FR-019, user 2026-10-26). Any
  other `dsp/` edit: STOP AND SURFACE. `BassMorphEngine`, `ProfundumMotion`, `LFO`, `ADSREnvelope`, `MorphState`, `MotionConfig`
  are never edited (spec Non-goals).
- **`plugins/shared/` and `tests/test_helpers/` are not touched.**
- **Append-only** on the shared engine header, the observer and the driver: no existing line is reordered or reworded except the
  body of `setMorphPosition` (P-1), the documented insertions in `prepare` / `reset` / `resetDependents` / `restoreMorphRuntime` /
  `serializeMorphRuntime` / `applySettings` / `applyNoteOn` / `gridStep` and the grid-record initializer (plan §4.2). Steps 1–8 of
  the grid step keep their order (`profundum_engine.h:22-25`, FR-011). No `ProfundumCall` enumerator is added (FR-015).
- **Fast-math (macOS leg).** Finiteness only through `Krate::DSP::detail::isFinite` (`core/db_utils.h`); never `std::isnan` /
  `std::isinf` / `std::isfinite` / `std::fpclassify`, never a hand-written `0x7F800000` exponent-mask clone. No
  `numeric_limits<T>::infinity()` / `quiet_NaN()` anywhere in this phase's code or tests; non-finite test inputs are built from a
  `volatile std::uint32_t` bit pattern through `std::bit_cast`. A value that must be exact (the transparent target `b`, Velocity
  127 → 1.0, Amount ±0) comes from a branch, never from cancelling arithmetic. No bit-equality claim between separately compiled
  computations: a test's recomputation of `clamp(b + A · s, 0, 1)` is compared within 1e-6, never bitwise. Same-binary
  same-code renders may be compared bitwise (`std::memcmp`).
- **No bit-exact float goldens across binaries.** The pre-phase harvest is compared through `render_fingerprint.h`
  (`kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4`, `tests/test_helpers/render_fingerprint.h:46-50`). Evidence CSVs are
  read by tests, never pasted into C++ literals (`tools/lint-float-bit-goldens.js`).
- No brace-init narrowing; designated initializers with matching types, or copy-then-assign.
- **Plugin tests that build non-finite values or compare bits** are listed in the `-fno-fast-math -fno-finite-math-only
  -ffp-contract=off` block of `plugins/profundum/tests/CMakeLists.txt` (`:118-153`); `integration/processor_cpu_test.cpp` stays
  out of it.
- Plugin types are `::Profundum::…`; plugin test code lives in `ProfundumPluginTest`; the moved Phase 9 readers are reached only
  through `namespace auRd = Krate::DSP::ProfundumAudibility;`. Every `Processor`, `Controller` and `ProfundumEngine` in a test lives
  on the heap.
- **ODR.** Names claimed by this phase (swept 2026-10-26, `grep -rnw <Name> dsp/ plugins/ tools/ tests/`: 0 hits each):
  `ProfundumMorphModulator`, `MorphModulator`, `MorphModParams`, `MorphModConfig`, `MorphModSource`, `MorphModSettings`,
  `MorphIndicatorSlider`, `MorphIndicatorSliderCreator`, `MorphModOverride`, `morphModTrajectoryCell`, `morphModSourceCell`,
  `withMorphMod`, `handleMorphModParamChange`, `registerMorphModParams`, `morphModConfigOf`, `kMorphModSourceLabels`,
  `kStateMorphModOffset`. Any further new name: sweep first, record 0 hits in the run log. Never a roadmap hazard name
  (`CharacterProcessor`, `SubOscillator`, `StereoField`, `MorphEngine`, `MorphNode`, `TransientDetector`, `HarmonicCloud`,
  `SpectralState`, `MacroMapper`).
- **Real-time safety.** Nothing reachable from `process()` / `processBlock()` allocates, locks, throws or does I/O.
  `ProfundumMorphModulator::prepare` is control-thread only.
- **FR-023-style stop.** A probe miss for a column, a column no preset or new preset can showcase from the shipped surface, or a
  bar that cannot be met: STOP AND SURFACE with the measured value. Never edit a bar (3 dB D_nl, 0.01 window stability, 0.05
  non-vacuity, factor 2 click rule, 1e-6 trace bounds, CPU budgets) or a shared tolerance.
- **uidesc** edits of more than two controls go through one XSLT stylesheet run with Saxon-JS (`npx xslt3 -xsl:<xsl>
  -s:plugins/profundum/resources/editor.uidesc -o:plugins/profundum/resources/editor.uidesc`), never string replacement (memory
  rule "XSLT for uidesc changes").
- Phase 9 and Phase 13 evidence (`specs/profundum-phase9-voice-engine/evidence/`,
  `specs/profundum-phase13-presets-release/evidence/`) is never left modified except the Phase 13 coverage-matrix shards the
  showcase cases write by design (FR-041); any other run that writes them is followed by `git checkout -- <dir>`.

### Ruling table (plan §2 P-1 … P-9, §11 Q1–Q4, and this list's deviations; T001 blocks until each is recorded)

| Ruling | What the tasks build (the plan's reading) | If ruled otherwise | Needed before | Tasks |
|---|---|---|---|---|
| **R1 = P-1 / §11 Q4** `setMorphPosition` stores `b` AND forwards; `prepare` / `reset` re-hand `b` before the morph snap | plan §4.2 lifecycle edits | Store-only: the Off path diverges at every snap (plan P-1); STOP, re-cut T007 | G6 | T001, T007 |
| **R2 = P-3 / §11 Q3** every successful `restoreMorphRuntime` resets the modulator, also mid-stream | plan §4.2 `restoreMorphRuntime` edit; SC-013 (h) | Reset only when nothing rendered since reset: re-cut T007, T008 SC-013 (h) | G6 | T001, T007, T008 |
| **R3 = P-5 / §11 Q1** the indicator updates at the frame cadence (≈ window rate while a note sounds, 4 Hz in silence) | plan §4.8, §4.9; manual sentence | A morph-only republish changes the Phase 12 frame-count contract (`Profundum_SpectrumFrameCadence`): re-cut T017, T018 | G12 | T001, T014, T017, T018 |
| **R4 = P-9 / §11 Q2** conversion rule (i) one per column (best eligible margin) or (ii) every eligible carrier | plan §5.7 step 4; SC-018 P-9 assertion | — (both are built; the ruled one is the constant `kMorphModConversionRule` in T023) | G17 | T001, T021, T023 |
| **R5 = P-2, P-4, P-6, P-7, P-8** row amendments | plan §2 | Re-cut the named task | G2 | T001 |
| **R6 = Deviation 1** FR-031 / §5.6 read window is the D_nl reader's frame `[1.0 s, 1.0 s + W)`, W = 262 144 samples | T020 | The `[1.0 s, 2.0 s)` window cannot be read by `dNlDb` (`bass_morph_test_helpers.h:703-707`, `:771`): no alternative within the AT-41 descriptor | G15 | T001, T020 |

### Deviations from the plan made while cutting these tasks (surface them; do not silently revert)

1. **FR-031 / plan §5.6 window (R6).** FR-031 and plan §5.6 read D_nl "over the file's `[1.0 s, 2.0 s)`". The AT-41 / AT-42
   reader `auBm::dNlDb` reads one `kLowFrequencyFftSize = 262 144`-sample frame from `readWindow`'s fixed
   `kSettleSeconds = 1.0 s` start (`dsp/tests/unit/systems/bass_morph_test_helpers.h:703-707`, `:771-779`), the same correction
   plan §5.5 already made for the showcase window. The two Main-page rows therefore render `1.0 s + W + 0.25 s` and check the
   resolved-position precondition over `[1.0 s, 1.0 s + W)`. T001 writes it into the FR-031 row.
2. **Harvest and close comparison are sharded** as `Profundum_MorphModPrePhaseHarvest_<k>of4` and
   `Profundum_MorphModPrePhaseFingerprints_<k>of4` (plan §5.8 names one case each). Each shard writes `EVID/prephase/*-<k>.csv`;
   `EVID/prephase/merge.js` writes the FR-062 paths `EVID/prephase-fingerprints.csv` and `EVID/prephase-decoded.csv` (root
   `CLAUDE.md`: no single sequential process on the 32-core box).
3. **Early CMake registration with a final audit.** CMake refuses a list that names a missing file and the harvest TU must be
   registered before the harvest runs at HEAD, so the four new test TUs are created in G2 (DSP placeholders, the harvest TU) and
   registered in G3 (T004); the `MorphIndicatorSlider` `.cpp` is registered by its own task (T018). The integration group's
   single CMake task (T029) audits every list (the Phase 13 tasks Deviation 1 precedent).
4. **The `MorphMod` capability columns land with the conversions (G17), not with the harness.** Adding the four `Capability`
   enumerators empties four columns, and `Profundum_CoverageMatrixComplete` (`unit/preset/preset_library_test.cpp:1312`) is a
   per-push case. The probe (G16) therefore calls a source-keyed reader `morphModSourceCell(source, …)` (written in T019); the
   column-keyed `morphModTrajectoryCell(Capability, …)` of plan §4.12 is a thin wrapper added in T023.
5. **`MANUAL.md` and the uidesc controls land in the registration group (G10).** Registering 93 parameters turns
   `Profundum_ManualCoversSurface` (`unit/manual_coverage_test.cpp:73-82`) and `Profundum_UidescBindsEverySurfaceId`
   (`unit/controller/editor_layout_test.cpp:494-523`) red until the manual names and the editor binds every new title, so
   registration, editor, manual and the 610-byte tree regeneration are one sequential group. The Main-page Morph slider's class
   swap to `MorphIndicatorSlider` waits for the view class (T018).
6. **SC-007 transparency shards are authored in G17** (after the converted set exists), so only the last gates pay their wall clock.
7. **Hard-coded sites the plan's P-4 list missed** (re-grep 2026-10-26; the re-grep, not any list, is the authority):
   `editor_lifecycle_test.cpp:224-226` (84 / 80u / 80u), `phase11_alloc_test.cpp:175` (`84u`), `state_v3_test.cpp:396`, `:401`
   (`574u`), `spectrum_frame_test.cpp:95`, `:107` (`288u`), `spectrum_producer_test.cpp:49` (`288`),
   `src/parameters/sound_param_table.h:191` (`routedCount() == 84`), comments `controller.cpp:9`, `:96`, `controller.h:13`,
   `sound_param_table.h:12`, `processor.cpp:663`, `:747`, `preset_test_support.h:7`, `profundum_test_fixture.h:1029`.

### Verified API surface these tasks rely on (read 2026-10-26 at `3dfc5b7d`)

| Fact | Source |
|---|---|
| `prepare`: step 2 `morph_.prepare(fs);   // it resets` then `resolveInitialState()`; step 5 `motion_.prepare(fs);`; step 9 `reset()` | `dsp/include/krate/dsp/systems/profundum_engine.h:147-198` |
| `reset()`: `morph_.reset(); resetDependents(); queueCount_ = 0; …` | `profundum_engine.h:202-209` |
| `noteOn`: drops `note >= kNumMidiNotes`; velocity 0 → `noteOff`; `queueCount_ >= kMaxQueuedEvents` → `++dropped_` | `profundum_engine.h:223-235`; `kMaxQueuedEvents = 256` `:102` |
| `setTempoBpm`: non-finite or `<= 0` keeps; stores `pending_.tempoBpm` | `profundum_engine.h:258-262` |
| `void setMorphPosition(float p) noexcept { morph_.setPosition(p); }`; `serializeMorphRuntime` forwards; `restoreMorphRuntime` re-snaps dependents only if `prepared_ && !renderedSinceReset_` | `profundum_engine.h:270-286` |
| `morphPosition()` returns `morph_.targetPosition()` | `profundum_engine.h:350` |
| `droppedEvents()` | `profundum_engine.h:413` |
| `nonZero(float)` = `(bit_cast<u32>(x) & 0x7FFFFFFFu) != 0u` | `profundum_engine.h:561-563` |
| `resetDependents()`: `motion_.reset();` at `:630`, `applySettings(true)`, `renderedSinceReset_ = false` | `profundum_engine.h:618-662` |
| `applySettings` tempo branch: `field_.setTempoBpm(p.tempoBpm); motion_.setTempoBpm(p.tempoBpm);` | `profundum_engine.h:768-772` |
| `applyNoteOn(note, velocity)`: MONO `mono_.noteOn` then `if (!mev.isNoteOn) return;`; POLY via `allocator_.noteOn` | `profundum_engine.h:793-836` |
| `gridStep`: step 1 `applySettings(false)`, `applyQueuedNotes()`, `logCall(ProfundumCall::Events, …)` `:902`; step 2 `morph_.advanceInterval()` `:905`; step 4 Motion with `consumeEnvelopeRestart()` `:918-925` | `profundum_engine.h:889-936` |
| Grid record emitted `observer_->onGrid(ProfundumGridRecord{.sample = pos_, …` | `profundum_engine.h:1097` |
| `ProfundumGridRecord { sample; resolved; afterMacros; afterMotion; motionSource … compressionHanded; compressorGrDb; targets; }` (no member initializers); `ProfundumCall` "order is normative: SC-032" | `profundum_engine_observer.h:43-60` |
| `VoiceEngineEventKind {… Legato, Endpoint}`; `VoiceEngineEvent {sample, kind, note, velocity = 100, value, index}`; `VoiceEngineScript {… float position; … bool countAllocations; }`; `VoiceEngineGridRecord {… MassLaw::Targets targets{}; }` | `dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h:123-184` |
| `kStateV3Bytes = kStateNamesOffset + 2 * kEndpointNameBlockBytes; // 574` and `static_assert(kStateV3Bytes == 574)`; `kCurrentStateVersion = 3`; `kRtTargetOffset = 1`; `kMsgCompare` | `plugins/profundum/src/plugin_ids.h:23`, `:58-63`, `:71`, `:85` |
| `kMorphRampTimeId = 1002, kMorphParamRangeEnd = 1100, kNumRoutedIds = 1003` + `static_assert(kMorphRampTimeId + 1 == kNumRoutedIds)` | `plugin_ids.h:218-226` |
| `setState`: future version refused `:609-611`; v1 branch `:615-626`; v2 `resetNamesToDefaults()` `:633-634`; v3 name loop `:639-646`; `resetPerformanceParams` `:653` | `plugins/profundum/src/processor/processor.cpp:600-660` |
| `getState`: … runtime blob precedence; two `writeEndpointName` `:694-696` | `processor.cpp:665-700` |
| `routeParamChange` switch over `RouteKind` | `processor.cpp:893-919` |
| `applyEndpoints`: `c = clamp(modWheel + channelPressure, 0, 1)` … `if (c != 0.0f) s.mass = …` | `processor.cpp:927-950` |
| `pushParamsToEngine`: … Compare A → 0, B → 1; `engine_->setMorphPosition(position);` `:979` | `processor.cpp:957-980` |
| DataExchange `kBlockSize` + `static_assert(kBlockSize == 288u, …)` | `processor.cpp:352-356` |
| `ProfundumSpectrumFrame` 288 bytes, `partialPower` at 32, static asserts `:46-51` | `plugins/profundum/src/processor/spectrum_frame.h:32-51` |
| `SpectrumProducer::onInterval(const ProfundumEngine&)` `:98`; `feedSilence` `:210`; `kSilenceRepublishSeconds = 0.25` `:47`; `reset()` `:60` | `plugins/profundum/src/processor/spectrum_producer.h` |
| `registerMorphControlParams(parameters);` `:102`; `getParamStringByValue` `:306-332`; `setComponentState` `:142-217`; `redisplayEditTarget` `:466`; `registerRingKnob` `:633`; `queueOpened` `:819`; `onDataExchangeBlocksReceived` `:839`; `willClose` `:1006`; `viewPointersNullForTest` `:1089` | `plugins/profundum/src/controller/controller.cpp` |
| `ProcessorFixture::ScriptedEvent { enum class Kind { NoteOn, NoteOff, Param }; sample; kind; pitch; velocity; id; value; }`; `processNoOutputs` `:626`; `renderScript` `:653`; `engine()` `:727`; `MultiParamChanges` `:162`; `writeV2Stream` `:1030`; `patchU32/I32/F32` `:1048-1055`; `withEndpoints` `:1066` | `plugins/profundum/tests/profundum_test_fixture.h` |
| `engineForTest` `:108`; `setEngineObserverForTest` `:117`; `mutableEngineForTest` `:119`; `compareModeForTest` `:142` | `plugins/profundum/src/processor/processor.h` |
| `PresetArmContext` `:118-140` (`std::string stop` `:135`); `effectiveScriptFor` `:144`; `processorCarrierParam` `:206`; `processorSampleZeroParams` `:262`; `processorRenderRefusal` `:295`; `renderScriptThroughProcessor` `:368` (`withEndpoints` `:407`); `showcaseArmPlan` `:815` … `case C::Count:` `:1258`; `renderPresetSweep(comp, notes, fs, maxMass, seed)` `:1635`; `presetCompOf(def)` `:1864` | `plugins/profundum/tests/preset_render_support.h` |
| `DecodedPresetState` `:40-67`; `decodePresetState` `:115`; `out.ok` requires `bytes == kStateV3Bytes` `:182` | `plugins/profundum/tests/preset_test_support.h` |
| `ppRenderProcessor` `:577`; twin `t.position = fx->engine()->morphPosition();` `:632` | `plugins/profundum/tests/param_path_audibility_helpers.h` |
| `isAllowedControlId` (1, 100-103, 105, 410-416, 1000, 1002) `:235-240`; `kStateV3Bytes` size check `:432` | `plugins/profundum/tests/profundum_preset_host.h` |
| `Capability {AT01 … AT50, RSolid, RLock, RSine, Count}`; `kNumCapabilities == 53`; `ProfundumPresetDef::controls` comment `:153`; `allPresets()` `:169` | `tools/profundum_preset_defs.h:64-70`, `:145-158` |
| `fingerprintRender`, `compareFingerprints`, `FingerprintComparison::withinTolerance()` | `tests/test_helpers/render_fingerprint.h:52-120` |
| `AllocationScope` | `tests/test_helpers/allocation_detector.h:111` |
| `enableFTZDAZ()`, `KRATE_TEST_FTZ_DAZ_X86` | `tests/test_helpers/enable_ftz_daz.h:20-31` |
| `readWindow` (start `kSettleSeconds` = 1.0 s, length `kLowFrequencyFftSize`), `dNlDb(taps, f0, fs)` | `dsp/tests/unit/systems/bass_morph_test_helpers.h:703-779` |
| `auStateB()` `:210`, `auStateAs()` `:211`, `AuSetting` `:218`, `auOne` `:225`, `auScript` `:262`, `auSteady` `:339` | `dsp/tests/unit/systems/profundum_audibility_readers.h` |
| `Profundum_MidiEventTranslation` / SECTION `PartitionInvariance` (`kTotal = 96000`, partitions {1, 7, 32, 65, 4096} vs 512) | `plugins/profundum/tests/unit/midi_event_test.cpp:210-245` |
| `Profundum_EngineDisplayAccessorNeutral` | `dsp/tests/unit/systems/profundum_engine_display_test.cpp:368` |
| `VoiceEngine_WorstInterval` (`[.perf]`) | `dsp/tests/unit/systems/profundum_engine_perf_test.cpp:596` |
| `Profundum_ProcessorCpuFullSurface` `:641`, `Profundum_PresetCpu` `:885` | `plugins/profundum/tests/integration/processor_cpu_test.cpp` |
| Main `morph-band` (636 × 204, Copy B → A ends at x 448), `CSlider control-tag="MorphPosition"` (28, 70, 476 × 24); `MorphRampTime` tag 1002 `:120`, its ArcKnob tooltip `:343`; `tab-modulation` `:348` (960 × 516) | `plugins/profundum/resources/editor.uidesc:173-188` |
| CHANGELOG `## [1.0.0] - 2026-10-25`; "84 parameters" `:24`; "574 bytes (format version 3)" `:35`; version.json `1.0.0` | `plugins/profundum/CHANGELOG.md`, `plugins/profundum/version.json` |

---

## Group and task map

| Group | Tasks | Kind | Gate / unblock condition |
|---|---|---|---|
| G1 | T001 | spec rows + census | blocked until `EVID/plan-rulings.md` records R1–R6 |
| G2 | T002 [P], T003 [P] | placeholder DSP TUs; harvest TU + processor-test placeholder + merge script | (not built until G3) |
| G3 | T004 | early test registration (two CMake lists) | build, per push |
| G4 | T005 | pre-phase harvest run (main loop) | blocked until the three pre-phase CSVs exist |
| G5 | T006 | `ProfundumMorphModulator` + unit tests | `[morphmod]` |
| G6 | T007 | engine / observer / driver edits; composition, transparency, driver-fields tests | `[profundum]` both DSP exes |
| G7 | T008 | engine behaviour tests | `[profundum]` |
| G8 | T009 | click-free shards | `[profundum]` |
| G9 | T010 | display-accessor SECTION | `[profundum]` |
| G10 | T011 → T012 → T013 → T014 → T015 | params / processor / state → controller / counts → uidesc + tooltips → manual → tree (main loop) | blocked at T015 until 610-byte tree; then per push |
| G11 | T016 | processor behaviour tests | per push |
| G12 | T017 | frame + producer | per push |
| G13 | T018 | `MorphIndicatorSlider` + wiring + class swap | per push |
| G14 | T019 | render harness carriage, override, reader, close comparison | per push |
| G15 | T020 | Main-page audibility rows | per push |
| G16 | T021 → T022 | probe case + scripts → probe run (main loop) | blocked until `probe-morphmod.csv` |
| G17 | T023 → T024 → T025 → T026 | columns + conversions + SC-018 → transparency + fidelity → tree (main loop) → library lanes (main loop) | blocked until `LIBRARY LANES GREEN` |
| G18 | T027 | CPU cases (hidden) | build |
| G19 | T028 | CHANGELOG | per push |
| G20 | T029 | CMake registration audit (single) | build |
| G21 | T030 [P] … T035 [P] | scope fence, suites lane, portability + lints, macOS fast-math repro, clang-tidy, CPU lane | main loop at close |

---

# GROUP 1 — Rulings

## T001 — Ruling gate, row amendments, branch / base / ODR census (no code) — STOP AND SURFACE until ruled

**Files edited:** `specs/profundum-phase14-morph-modulation/spec.md`. **Files read:** `EVID/plan-rulings.md` (written by the
**main loop**, never by this task). **Files created:** `EVID/run-log/T001.txt`.

1. **Census.** `git rev-parse --abbrev-ref HEAD` must print `feat/profundum-preset-morph`; `git merge-base --is-ancestor
   3dfc5b7d HEAD` must exit 0; `git status --porcelain -- dsp/ plugins/profundum/src plugins/profundum/resources tools/` must
   print nothing (the harvest needs the pre-phase tree). Otherwise STOP AND SURFACE (branch safety). Record the outputs.
2. **Ruling gate.** If `EVID/plan-rulings.md` is missing, or lacks a decision for each of R1–R6 (ruling table above), return
   **`blocked`** with notes: "main loop: grill the user on plan §11 Q1–Q4 (R1–R4), the P-2/P-4/P-6/P-7/P-8 amendments (R5) and
   tasks Deviation 1 (R6); write each decision verbatim to EVID/plan-rulings.md; unblock T001". If any item is ruled **other**
   than the plan's reading, return `blocked` naming the tasks the ruling table lists for re-cut. R4 must name rule (i) or (ii).
3. **Row first, then Clarifications** (memory rule). For each ruling write the amended text into its spec row(s) before anything
   else:
   - R1 → FR-010: replace "MUST store the base `b`, sanitised by `BassMorphEngine::setPosition`'s rule …, instead of forwarding it
     directly" with "MUST store the sanitised base `b` and forward it; the per-interval target replaces it before every
     `advanceInterval`, and `prepare` / `reset` re-hand `b` immediately before the morph snap (plan P-1)"; C-2 gains the same
     sentence.
   - R2 → C-11 and FR-016: "`restoreMorphRuntime` sets `b` to the restored target and resets the modulator on every successful
     restore, also mid-stream (plan P-2, P-3); `serializeMorphRuntime` patches bytes 1–4 with `b`".
   - R3 → C-18 and FR-037: "the indicator updates at the frame cadence (each completed window while a note sounds, every
     0.25 s in silence, `spectrum_producer.h:47`); the manual says so" (or the ruled alternative).
   - R4 → FR-046 (P-9): the ruled rule verbatim (i or ii, plan §5.7 step 4), and SC-018 gains "the converted set equals the
     ruled rule applied to `evidence/probe-morphmod.csv`".
   - R5 → C-12 hard-coded list gains the P-4 sites **and** tasks Deviation 7's sites; C-17 gains the P-7 event kinds
     (`MorphModulation`, `MorphModWheel`, `MorphModBypass`); FR-031 notes gain the P-6 confound (both arms' `smoothedMass`
     logged); FR-019 and C-1 gain the P-8 list verbatim (plan §2 P-8 "Ruling proposed").
   - R6 → FR-031: replace "the read window is the file's [1.0 s, 2.0 s) (`main_page_audibility_test.cpp:33-34`)" with "the read
     window is the D_nl reader's frame `[1.0 s, 1.0 s + W)`, W = 262 144 samples (`bass_morph_test_helpers.h:703-707`)", and the
     precondition window likewise.
   - Fix the dangling reference in C-14 "(FR-064)" → "(FR-044)" (plan Review notes).
   Then append `### Session 2026-10-2x (plan rulings)` to Clarifications, one line per ruling, each ending with its enforcing ids in
   brackets.
4. **ODR re-sweep.** For every name in "Rules → ODR" above: `grep -rnw <Name> dsp/ plugins/ tools/ tests/`; record each count
   (all must be 0). Any hit: STOP AND SURFACE.

**Verified by:** G1 gate (build only; no code changed).

---

# GROUP 2 — New test TUs (parallel)

## T002 [P] — DSP placeholder TUs

**Files created:** `dsp/tests/unit/systems/profundum_morph_modulator_test.cpp`,
`dsp/tests/unit/systems/profundum_engine_morphmod_test.cpp`.

Each holds only a header comment (phase, spec slug, the TEST_CASE names a later task writes: T006 for the first; T007 – T009 for
the second) and `#include <catch2/catch_test_macros.hpp>`. No TEST_CASE, and no include of the not-yet-existing
`profundum_morph_modulator.h`. They are registered by T004 and must compile empty.

**Verified by:** G3 gate (build).

## T003 [P] — Plugin TUs: the pre-phase harvest case, the processor-test placeholder, the merge script (FR-062, FR-063)

**Files created:** `plugins/profundum/tests/integration/morph_mod_preset_test.cpp`,
`plugins/profundum/tests/integration/morph_mod_processor_test.cpp` (placeholder: header comment naming T016's cases and
`#include <catch2/catch_test_macros.hpp>` only), `EVID/prephase/merge.js`, `EVID/prephase/run-harvest.sh`.

`morph_mod_preset_test.cpp` (this task writes only the harvest; T019 / T024 append later cases):
- Includes `preset_render_support.h`, `preset_test_support.h`, `profundum_test_fixture.h`, `render_fingerprint.h`,
  `plugin_ids.h`, `<profundum_preset_defs.h>` (via `tools/`), Catch2, `<filesystem>`, `<fstream>`.
- `[[nodiscard]] std::filesystem::path mmEvidenceDir()` = `std::string(KRATE_DSP_TESTS_DIR) +
  "/../../specs/profundum-phase14-morph-modulation/evidence"` (the `scCoverageDir` pattern,
  `integration/preset_showcase_test.cpp:79-82`).
- `void mmRunHarvestShard(std::size_t k)`: for every definition index `i` of `Profundum::PresetDefs::allPresets()` with
  `i % 4 == k`:
  1. `comp = presetCompOf(def)` (`preset_render_support.h:1864`); `REQUIRE(comp.size() == ::Profundum::kStateV3Bytes)` (574 at
     this commit).
  2. For note ∈ {`auRd::kAuE1`, `auRd::kAuC3`}: `r = renderPresetSweep(comp, {note}, 48000.0, false)`; `fl =
     fingerprintRender(r.L)`, `fr = fingerprintRender(r.R)`; one row per channel to `EVID/prephase/fingerprints-<k>.csv`:
     `preset,note,channel,rms,peak,meanAbs,totalVariation,cp0..cp31` — metrics printed with `%a` (hex float, exact), checkpoints
     as `0x%08X` of `std::bit_cast<std::uint32_t>`.
  3. Decoded pre-existing fields to `EVID/prephase/decoded-<k>.csv`: one row per field region `preset,field,offset,length,hex`
     where the regions are named by the `plugin_ids.h` offset constants (version, globals, macros, note block, morph-control
     block, seed, endpoint A, endpoint B, runtime blob, name A, name B; read `plugin_ids.h:25-63` for the constants). A
     `REQUIRE` asserts the regions tile `[0, 574)` exactly (sorted, contiguous, last end == `kStateV3Bytes` at this commit,
     written as the named constant `kStateNamesOffset + 2 * kEndpointNameBlockBytes` so it stays 574 after T011).
  4. Shard 0 only: `EVID/prephase-claims.csv` with one row per definition: `name,primaryLabel,secondaryLabels` (labels from
     `capabilityTable()`, secondaries sorted and joined by `;`; plan §4.13).
- `TEST_CASE("Profundum_MorphModPrePhaseHarvest_<k>of4", "[profundum][morphmod][preset][.harvest]")` for k = 0..3, each calling
  `mmRunHarvestShard(k)` and printing its wall clock.

`EVID/prephase/merge.js` (Node): concatenates `fingerprints-0..3.csv` → `EVID/prephase-fingerprints.csv` and `decoded-0..3.csv` →
`EVID/prephase-decoded.csv` (one header, rows sorted by preset, note, channel / offset); fails if any shard file is missing or a
preset appears in two shards; prints `PREPHASE MERGED presets=<n> rows=<m>`.
`EVID/prephase/run-harvest.sh`: builds `profundum_tests` once (`-- /nr:false`), launches the four shards concurrently, each to
`EVID/run-log/T005/<k>.log` with its wall clock, then runs `merge.js`; last line `HARVEST GREEN|RED`.

**Verified by:** G3 gate (compiles; the hidden cases do not run).

---

# GROUP 3 — Early test registration

## T004 — Register the four new TUs (Deviation 3)

**Files edited:** `dsp/tests/CMakeLists.txt`, `plugins/profundum/tests/CMakeLists.txt`.

- `dsp/tests/CMakeLists.txt`, `dsp_systems_tests` list: after `unit/systems/profundum_engine_display_test.cpp` (`:790`), a comment
  block `# Profundum Phase 14 (specs/profundum-phase14-morph-modulation) - ENUMERATED, not globbed:` naming each TU's SCs, then
  `unit/systems/profundum_morph_modulator_test.cpp` and `unit/systems/profundum_engine_morphmod_test.cpp`. Not in
  `dsp_ieee_tests` (plan §6: no case of this phase goes there).
- `plugins/profundum/tests/CMakeLists.txt`: a `# Phase 14` block after the Phase 13 TUs (`:45-53`) with
  `integration/morph_mod_processor_test.cpp` and `integration/morph_mod_preset_test.cpp`; both also in the `-fno-fast-math` block
  (`:118-153`) under a `# Phase 14 (bit comparisons, bit-pattern finiteness)` comment.

**Verified by:** G3 gate: all targets build with 0 warnings; the per-push sets are unchanged in count.

---

# GROUP 4 — Pre-phase harvest

## T005 — Run the harvest at the pre-phase tree (FR-062, FR-063; SC-008 reference) — **main loop**, blocks until recorded

**Files created:** `EVID/run-log/T005.txt`.
Executor: confirm `git status --porcelain -- dsp/include dsp/src tools/ plugins/profundum/src plugins/profundum/resources`
prints nothing (no engine, generator, plugin or preset change yet; only the G2/G3 test files may differ). If it prints anything,
STOP AND SURFACE. Then check whether `EVID/prephase-fingerprints.csv`, `EVID/prephase-decoded.csv` and `EVID/prephase-claims.csv`
exist with a row for every `allPresets()` definition (58 expected). If not, return **`blocked`** with notes "main loop: run
`bash specs/profundum-phase14-morph-modulation/evidence/prephase/run-harvest.sh` (one build + four concurrent shards); quote its
last line and the four wall clocks into evidence/run-log/T005.txt; unblock T005 when it prints HARVEST GREEN". The harvest must
complete before any T006+ edit lands (FR-062 "before the first engine edit").

---

# GROUP 5 — The modulator

## T006 — `ProfundumMorphModulator` (FR-001 – FR-008; SC-001 (a), SC-002, SC-003 (a)(b)(c)(e) and the modulator half of (d); plan §4.1, §5.2, §5.3)

**Files created:** `dsp/include/krate/dsp/systems/profundum_morph_modulator.h`.
**Files edited:** `dsp/tests/unit/systems/profundum_morph_modulator_test.cpp` (the T002 placeholder).

**Tests first** (all `[systems][profundum][morphmod]`; include `<enable_ftz_daz.h>`, `<allocation_detector.h>`):

- `ProfundumMorphModulator_ApiAndRt` (SC-001 (a)):
  `auto [a, b, c, d, e, f, g, h, i, j] = ProfundumMorphModulator::Config{};` compiles (exactly 10 members);
  `STATIC_REQUIRE(std::is_base_of_v<ModulationSource, ProfundumMorphModulator::Output>)`; `output().getSourceRange()` equals
  `{-1.0f, 1.0f}`; `STATIC_REQUIRE(noexcept(m.reset()))` and the same for `setConfig`, `setTempoBpm`, `noteOn`, `setModWheel`,
  `advanceInterval`, `sourceValue`, `offsetValue`, `output`. FR-007 value contract: per source (Envelope at an interval inside
  Attack with A = 400 ms, Cycle Sine 1 Hz, Velocity after `noteOn(64)`, Mod Wheel 0.3) × Amount ∈ {−1, 0.5, 1}, after each
  `advanceInterval()`: `std::bit_cast<u32>(output().getCurrentValue()) == std::bit_cast<u32>(offsetValue())` and
  `|getCurrentValue() − std::clamp(amount · sourceValue(), −1, 1)| ≤ 1e-7`. FR-006: Mod Wheel source, `setModWheel(1.5f)` → next
  `advanceInterval()` returns `1.0f`; `setModWheel(-0.2f)` → `0.0f`; after `setModWheel(0.4f)`, a quiet-NaN made from
  `volatile std::uint32_t q = 0x7FC00000u` through `std::bit_cast` leaves the next return at `0.4f`. `setConfig` sanitising: a NaN
  amount keeps the previous applied amount (`config().amount`), amount 3 clamps to 1, attack 0 clamps to `kMinEnvelopeTimeMs`,
  rate 50 clamps to `LFO::kMaxFrequency`, `static_cast<Source>(7)` keeps the previous source. `setTempoBpm` NaN / 0 / −1 keep the
  tempo (non-vacuity: a synced Cycle's period after `setTempoBpm(90)` differs from after `setTempoBpm(0)`). RT: one
  `AllocationScope` around 10 000 calls each of `setConfig`, `noteOn`, `setModWheel`, `advanceInterval` per source →
  `getAllocationCount() == 0`.
- `ProfundumMorphModulator_Envelope` (SC-002), for fs ∈ {48 000, 44 100, 96 000} (control rate `fc = fs / 32` = 1500, 1378.125,
  3000):
  (a) Envelope source, `noteOn(100)` then N intervals: each `advanceInterval()` within 1e-6 of a reference `ADSREnvelope`
  prepared at `fc`, `setRetriggerMode(RetriggerMode::Hard)`, same A/D/S, `reset(); gate(true)` at the same interval, then
  `process()` once per interval.
  (b) Attack: the first interval whose value reads `1.0` (peak) lies within `round(A · fc / 1000) ± 1` intervals of the note for
  A ∈ {0.1, 10, 400, 10 000} ms (A = 0.1: the first interval reads 1.0).
  (b′) Decay with Sustain 0: intervals from the peak to the first `s == 0` within `round(D · fc / 1000) ± 1` for
  D ∈ {10, 400, 10 000} ms.
  (c) Sustain S ∈ {0.5, 1}: the interval where Decay ends reads `std::bit_cast<u32>(s) == std::bit_cast<u32>(S)` and every later
  interval `|s − S| ≤ 1e-6`; S = 0: `|s| ≤ 1e-3` from interval `A + D + 1` onward.
  (d) Sustain step 0 → 1 during the Sustain stage: settles to within 1e-3 of 1 in ≤ {11, 10, 21} intervals (48 / 44.1 / 96 kHz)
  and the trace matches `out = S + c_s (out − S)`, `c_s = exp(−5000 / (5 · fc))`, within 1e-6. Print the counts (`WARN`).
  (e) Restart: A = 400 ms, Sustain 1; at an interval with `s > 0.5` call `noteOn(90)`: that interval's `s` equals the first
  `process()` of a fresh reference (`reset(); gate(true)`) within 1e-6 and is lower than the previous interval's `s`. Two
  `noteOn` calls before one `advanceInterval()` give the same `s` as one call.
  (f) Sustain-stage flush (plan §4.1, §10): on x86 (`#ifdef KRATE_TEST_FTZ_DAZ_X86`) a local RAII guard saves `_mm_getcsr()`,
  clears FTZ (bit 15) and DAZ (bit 6), and restores on exit. Sustain 1, held into Sustain, then `setConfig` with Sustain 0: `s`
  reads bitwise `+0` (`std::bit_cast<std::uint32_t>(s) == 0u`) no later than `⌈ln(1e-15) / ln(c_s)⌉ + 1` intervals after the
  step (53 / 49 / 105 at 48 / 44.1 / 96 kHz), stays `+0` for the next 1 000 intervals, and `offsetValue()` reads `+0` too
  (Amount 1). Print the count.
- `ProfundumMorphModulator_Cycle` (SC-003 modulator part):
  (a) for the six `Waveform`s, the trace matches a reference `LFO` prepared at `fc` with the same setter calls in the same order
  (`setWaveform`, `setFrequency`, `setTempoSync`, `setNoteValue`) within 1e-6 over 3 000 intervals.
  (b) zero-crossing period (linear interpolation between grid points, ≥ 10 cycles; ≥ 2 at 0.01 Hz) within 0.5 % of `1 / f` for
  f ∈ {0.01, 0.1, 1, 10, 20} Hz; synced at 90 and 120 BPM for Sync Rate indices 13 (1/4), 9 (1/8T), 19 (1/1)
  (`kNoteValueDropdownMapping`, `core/note_value.h:136`) within 0.5 % of `beats · 60 / bpm` (beats from `getBeatsForNote`).
  (c) every value in [−1, 1].
  (d) S&H and Smooth Random: two modulators prepared and run identically produce bit-identical traces (same binary, same code).
  (e) ≥ 8 `noteOn` calls at irregular intervals (e.g. intervals 37, 41, 160, 161, 509, 777, 1200, 2047) with velocities
  {1, 30, 64, 90, 100, 110, 120, 127}: the Cycle trace equals the no-note reference `LFO` within 1e-6 for all six shapes, free
  (1 Hz) and synced (120 BPM, index 13). Non-vacuity: a reference `LFO` with `retrigger()` at those intervals differs by > 0.1 at
  ≥ 1 interval for Sine.

**Implement** plan §4.1 exactly: the class sketch, `prepare` (`fc = fs / 32`, env `prepare(float(fc))`, Hard retrigger,
`lfo_.prepare(fc)`, `reset()`), `reset` (env / LFO reset, velocity, `s_`, output 0, `configured_ = false`; wheel and tempo kept),
`setConfig` (sanitise against `applied_`, then change-only setter calls in the listed order), `setTempoBpm`, `noteOn`
(`env_.reset(); env_.gate(true);` velocity `v >= 127 ? 1.0f : v / 127.0f`, `v == 0` no change), `setModWheel`, `advanceInterval`
(`detail::flushDenormal(env_.process())`, LFO value sanitised with `detail::isFinite` and clamped, selected source, offset
`std::clamp(amount * s, -1, 1)`). Includes downward only (L0 `db_utils.h`, `modulation_source.h`, `note_value.h`; L1
`adsr_envelope.h`, `envelope_utils.h`, `lfo.h`); no `morph_state.h`.

**Verified by:** G5 gate, `dsp_systems_tests "[morphmod]"`.

---

# GROUP 6 — Engine integration

## T007 — Observer fields, engine edits, driver fields; composition and transparency (FR-010 – FR-017; SC-005, SC-006, SC-023 (d); plan §4.2 – §4.4, §5.1)

**Files edited:** `dsp/include/krate/dsp/systems/profundum_engine_observer.h`, `dsp/include/krate/dsp/systems/profundum_engine.h`,
`dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h`, `dsp/tests/unit/systems/profundum_engine_morphmod_test.cpp`
(T002 placeholder), `dsp/tests/unit/systems/profundum_voice_engine_helpers_test.cpp` (one appended TEST_CASE).
**Precondition:** R1 and R2 ruled as the plan reads them (T001); otherwise STOP.

**Tests first** (`[systems][profundum][morphmod][voice-engine]`), driving the engine through `voiceEngineRender` with the new
script fields / events:
- `ProfundumEngine_MorphModComposition` (SC-005): showcase pair `auStateAs()` / `auStateB()`, MONO, E1, 48 kHz, host block 64.
  (a) every grid record: `|morphTarget − std::clamp(b + A · morphModSource, 0, 1)| ≤ 1e-6` for each non-Off source at A ∈
  {−1, −0.5, 0.5, 1}, b ∈ {0, 0.5, 1}. (b) `std::bit_cast<u32>(morphTarget) == std::bit_cast<u32>(b)` at every grid point when
  Source Off, A = +0, A = −0 (made as `-0.0f`), or bypass on with A = 1. (c) `resolved.position ∈ [0, 1]`. (d) for every source ×
  A ∈ {−1, −0.5, 0.5, 1} × b ∈ {0, 0.5, 1} × ramp ∈ {5, 40, 2000} ms: per interval `|Δp| ≤ 32000 / (rampMs · fs) + 0.5 ·
  ulp(p)` (the Phase 8 B3-1 bar, `bass_morph_engine.h:369-373`); Cycle uses Square 2 Hz, Envelope A 0.1 / D 300 / S 0, so steps
  occur (non-vacuity: ≥ 1 interval per arm with `|Δp| > 0.5 · Δp_max`). (e) FR-006 engine level: Mod Wheel source, A 1, b 0;
  `setMorphModWheel(1.5f)` → next record `morphModSource == 1.0f`; `setMorphModWheel(-0.2f)` → `0.0f`; after `0.4f`, a quiet-NaN
  (volatile bit pattern) keeps `morphModSource == 0.4f` and `morphModWheel() == 0.4f`.
- `ProfundumEngine_MorphModTransparentAtZero` (SC-006): MONO and POLY4 × {default `MorphState{}` pair, `auStateAs()` /
  `auStateB()`, a Fold + Wide pair (A = `auStateAs()` with `foldAmount = 60`; B = A with `foldAmount = 100` and
  `field.mode = BassStereoField::ImageMode::Wide`)} × {E1, C3}, 2 s: Source Off vs each of Envelope, Cycle, Velocity, Mod Wheel at
  Amount 0 (wheel 0); and Source Off vs bypass on with Envelope at Amount 1; `outL` and `outR` `std::memcmp`-identical. Position
  0.5 for every arm (so a non-transparent target would differ). Non-vacuity: Envelope at Amount 1 without bypass differs.
- `VoiceEngineHelpers_MorphModFields` (SC-023 (d)), appended after `VoiceEngineHelpers_DriverSplitsAtEvents` (`:191`): a Phase 9
  showcase script (`auScript(auOne(auStateAs()), kAuE1, …)` pattern, 1 s) with the appended fields at their defaults renders
  `std::memcmp`-identical to the same script with `morphMod.source = Off`, `morphMod.amount = 1.0f`, `morphModWheel = 1.0f`;
  `VoiceEngineScript{}.morphMod.source == Source::Off`, `.morphModWheel == 0.0f`, `.morphModBypass == false`; a `MorphModulation`
  event whose index is out of `morphModStates` range is rejected like an out-of-range `Endpoint` (same mechanism as `:388-394`).

**Implement:**
- Observer (plan §4.3): append `float morphModSource = 0.0f; float morphModOffset = 0.0f; float morphTarget = 0.0f;` after
  `targets` in `ProfundumGridRecord`.
- Engine (plan §4.2, verbatim): include `profundum_morph_modulator.h` and `<cstring>`; `static_assert(kControlInterval ==
  ProfundumMorphModulator::kControlInterval);` beside `:125-131`; members `MorphModSettings`, `morphMod_`, `morphModPending_`,
  `morphBase_`, `morphTarget_`, `kMorphRuntimeTargetOffset = 1`; the `setMorphPosition` body (P-1); setters
  `setMorphModulation`, `setMorphModWheel` (`detail::isFinite`, clamp), `setMorphModBypass`; read-backs `morphBasePosition()`,
  `morphResolvedPosition()` (`morph_.position()`), `morphModulation()`, `morphModWheel()`, `morphModBypass()`; lifecycle edits in
  `prepare` (`morph_.setPosition(morphBase_)` before `morph_.prepare(fs)`; `morphMod_.prepare(fs)` after `motion_.prepare(fs)`),
  `reset` (`morph_.setPosition(morphBase_)` before `morph_.reset()`), `resetDependents` (`morphMod_.reset()` after
  `motion_.reset()`), `restoreMorphRuntime` (on `ok`: `morphBase_ = morph_.targetPosition(); morphMod_.reset();`),
  `serializeMorphRuntime` (memcpy `morphBase_` at offset 1 when `n == BassMorphEngine::kMorphRuntimeBytes`), `applySettings`
  tempo branch (`morphMod_.setTempoBpm(p.tempoBpm)`), `applyNoteOn` (first statement `morphMod_.noteOn(velocity);`), sub-step 1b
  between `:902` and `:905` (plan §4.2 code block; `nonZero` for ±0; no `logCall`), grid record `.morphModSource`,
  `.morphModOffset`, `.morphTarget` appended to the designated initializer.
- Driver (plan §4.4): `VoiceEngineEventKind` appends `MorphModulation, MorphModWheel, MorphModBypass`; `VoiceEngineScript`
  appends `ProfundumMorphModulator::Config morphMod{}; float morphModWheel = 0.0f; bool morphModBypass = false;
  std::vector<ProfundumMorphModulator::Config> morphModStates;`; `voiceEngineIssue` cases; `voiceEngineRender` hands the three
  fields after `setMorphPosition(s.position)` and validates `MorphModulation` indices; `VoiceEngineGridRecord` appends the three
  floats and `VoiceEngineObserver::onGrid` copies them.

**Verified by:** G6 gate: `dsp_systems_tests` and `dsp_ieee_tests` `[profundum]` (every consumer of the modified headers,
including Phase 9 SC-032's call sequence and the determinism cases, keeps its verdict) and `profundum_tests` per push (the
plugin's consumers; the processor still calls only `setMorphPosition`).

---

# GROUP 7 — Engine behaviour

## T008 — Velocity, retrigger, lifecycle, allocation, Anchor mono (SC-001 (b), SC-003 (d) engine half, SC-004, SC-010 (a)–(e)(g)(h), SC-012 (b)(c)(c′), SC-013 (e) engine half, (h), SC-019 engine)

**Files edited:** `dsp/tests/unit/systems/profundum_engine_morphmod_test.cpp`. No production code (a red here is a T007 defect:
fix it in the engine, never in the test).

All `[systems][profundum][morphmod][voice-engine]`; restart checks compare against a fresh reference `ADSREnvelope` at `fs / 32`
(`reset(); gate(true); process()` once) within 1e-6; restart non-vacuity: A ≥ 10 ms, Sustain 1, `s > 0.5` the interval before.
- `ProfundumEngine_MorphModVelocity` (SC-004, FR-013), MONO and POLY4: grid `morphModSource == v / 127 ± 1e-6` for v ∈ {1, 64}
  and `== 1.0f` exactly for 127; `0.0f` before the first note; unchanged through a note-off and 2 s of silence; changes at the
  next note-on's grid point. Dropped note-on: with the queue empty at an interval start, issue 257 note-ons at one sample inside
  that interval — notes `36 + (i % 48)` at velocity 64 for i = 0..255, then one at velocity 120; REQUIRE `droppedEvents()` rose
  by exactly 1 and `morphModSource == 64 / 127 ± 1e-6` at that grid point.
- `ProfundumEngine_MorphModNoAlloc` (SC-001 (b)): `countAllocations = true`; each non-Off source × Amount ±1; a `MorphModulation`,
  `MorphModWheel` and `MorphModBypass` event in every host block → `taps.allocations == 0`.
- `ProfundumEngine_MorphModRetrigger` (SC-010): (a) MONO Legato on, three overlapping notes: each note's grid point reads the
  restart observable; (b) MONO High priority, a lower note pressed while a higher one is held: its grid point reads the restart
  observable; (c) POLY4, a 3-note chord at one sample offset: the `morphModSource` trace equals a single note-on's trace within
  1e-6, and with Velocity source the value is the last note's velocity / 127; (d) POLY4, three staggered notes: the restart
  observable at each of the 3 grid points; (e) a note-off changes no `morphModSource` value (trace equal to a no-note-off run
  within 1e-6); (g) MONO ↔ POLY switch (a `VoiceMode` event) with notes held: trace equals a no-switch run within 1e-6;
  (h) Sustain 1, held into Sustain, engine `noteOff`, then 5 s with no note: `|morphModSource − S| ≤ 1e-6` at every grid point,
  and in POLY4 with three held voices every voice's chunk records see the same resolved position (one global value).
- `ProfundumEngine_MorphModLifecycle` (SC-012 (b)(c)(c′), SC-003 (d) engine half, SC-013 (e) engine half, (h)):
  (b) two renders with the same seed and Cycle S&H at A 1 are `memcmp`-identical; (SC-003 d) S&H and Smooth Random traces
  identical across `seed` 1 and `seed` 0xB455 (`morphModSource`, bitwise, same binary). (c) after `setMorphModulation(cfg)`,
  `prepare(96000)` keeps `morphModulation()` field-equal to `cfg`; `reset()` zeroes `morphModSource` for Envelope and Velocity
  at the next grid point and keeps `morphModWheel()` and the config. (c′) Cycle Sine synced to index 13, A 1:
  `setTempoBpm(90.0f)` before the first block → zero-crossing period of `morphModSource` over ≥ 10 cycles within 0.5 % of
  0.6667 s; a second arm at 120 BPM reads 0.5 s ± 0.5 % (non-vacuity of the forwarding line). (e engine) with Envelope running
  (A 400 ms, Amount 1, b 0.25), `serializeMorphRuntime` bytes 1–4 equal `std::bit_cast` of 0.25f, and a fresh `BassMorphEngine`
  restored from the blob reads `targetPosition() == 0.25f` (pins `kMorphRuntimeTargetOffset` against the real engine). (h) a blob
  from a second engine at position 0.75, Source Off; the first engine with Velocity source, A 1, a note at v 100 rendered until
  `morphModSource == 100/127 ± 1e-6`; `restoreMorphRuntime(blob)` returns true mid-stream; next grid record `morphModSource ==
  0.0f`; `morphBasePosition()` bit-equal to the float at blob bytes 1–4; with Envelope source, `0.0f` at the next interval with no
  note-on; with Cycle 1 Hz Sine free, the next interval equals a freshly prepared `ProfundumMorphModulator`'s first
  `advanceInterval()` within 1e-6.
- `ProfundumEngine_MorphModAnchorMono` (SC-019 engine): a SOLID ↔ WIDE pair (A `field.mode = Solid`, B `= Wide`), Cycle Sine
  1 Hz at A 1, b 0.5, MONO and POLY4 (3 notes), 3 s: for every tapped voice and every sample, `anchorL == anchorR` bitwise;
  non-vacuity: the resolved position spans ≥ 0.9 of [0, 1] over the render.

**Verified by:** G7 gate (`[profundum]` on both DSP exes).

---

# GROUP 8 — Click bound

## T009 — `ProfundumEngine_MorphModClickFree_<k>of4` (SC-011; plan §6)

**Files edited:** `dsp/tests/unit/systems/profundum_engine_morphmod_test.cpp`.
Showcase pair `auStateAs()` / `auStateB()`, E1, MONO, 48 kHz, b 0.5, 4 s per arm. The arm list (fixed order, shard = index % 4):
Envelope with (A, D) ∈ {0.1, 10 000}² at S 0; Cycle at {0.01, 20} Hz × the six shapes; Velocity (notes at v 1 and 127 alternating
every 0.5 s); Mod Wheel (`MorphModWheel` events 0 ↔ 1 every 0.5 s); each × A ∈ {−1, +1} × ramp ∈ {5, 2000} ms. Per arm:
(a) every interval `|Δp| ≤ Δp_max + 0.5 · ulp(p)`; (b) `max_n |y[n] − y[n−1]|` over `outL` and `outR` ≤ 2 × the maximum of the
same metric on two static renders (Source Off) at the trajectory's recorded min and max `resolved.position` (Phase 13 FR-048
factor-2 rule; cache static renders by position bits); (c) 0 non-finite samples (`detail::isFinite`). Four TEST_CASEs
`[systems][profundum][morphmod][voice-engine]` each print their wall clock (`WARN`). Each must read ≤ 9 s (60 % of the 15 s
per-push bar) on the dev box; the G8 gate records the four readings; a shard above 9 s is a Defect entry and the arms are
re-sharded into 8, never shortened.

**Verified by:** G8 gate.

---

# GROUP 9 — Display accessor neutrality

## T010 — `Profundum_EngineDisplayAccessorNeutral` SECTION `MorphResolvedPosition` (SC-024 (d), P-8)

**Files edited:** `dsp/tests/unit/systems/profundum_engine_display_test.cpp` (one appended SECTION; no existing SECTION edited).
Two engines, same mono script as `edaMonoScript()` but with Cycle Sine 2 Hz at Amount 1, b 0.5: engine B calls
`morphResolvedPosition()` after every 32-sample sub-call, engine A never; outputs bit-identical (`edaBitIdentical`); at every grid
point B's read equals that grid record's `resolved->position` bitwise (same object read in the same binary); non-vacuity: the reads
span ≥ 0.5.

**Verified by:** G9 gate.

---

# GROUP 10 — Plugin surface (sequential; one gate after T015)

## T011 — Parameter pack, IDs, routes, processor routing, C-7 / C-8, state v3 610 bytes (FR-020 – FR-023, FR-025; SC-009, SC-013 processor arms; plan §4.5 – §4.7)

**Files created:** `plugins/profundum/src/parameters/morph_mod_params.h`.
**Files edited:** `plugins/profundum/src/plugin_ids.h`, `plugins/profundum/src/parameters/sound_param_table.h`,
`plugins/profundum/src/processor/processor.h`, `plugins/profundum/src/processor/processor.cpp`,
`plugins/profundum/CMakeLists.txt` (the header in the Parameter Packs list), `plugins/profundum/tests/unit/state_v3_test.cpp`,
`plugins/profundum/tests/unit/state_v2_test.cpp`, `plugins/profundum/tests/unit/state_roundtrip_test.cpp`,
`plugins/profundum/tests/unit/midi_mapping_test.cpp`.

**Tests first:**
- `Profundum_StateV3MorphMod` (`[profundum][morphmod][state]`, in `state_v3_test.cpp` beside `Profundum_EndpointNames` `:198`),
  processor arms (T012 adds the controller arms):
  (a) the nine fields at non-defaults (Source 2, Amount −0.6, Attack 50, Decay 900, Sustain 0.4, Shape 3, Rate 3, Sync 1, Sync
  Rate 7) set through normalised parameter changes, `getState` → 610 bytes, `setState` into a fresh processor → floats bit-equal,
  ints equal (read back through a second `getState`); (b) a v1 stream and a v2 stream load all nine as defaults (Off, 0, 10, 300,
  0, 0, 1, 0, 13) even after the instance held non-defaults; (c) a 610-byte v3 stream truncated at each of the 36 lengths 574 …
  609 keeps the prior value of every field from the cut on (prime the instance with values different from the stream's); (d)
  hostile values patched into a 610-byte stream are not stored: NaN and ±inf (bit patterns 0x7FC00000, 0x7F800000, 0xFF800000)
  in each float field, Source −1 and 5, Shape 6, Sync Rate 30, Sync 2; finite out-of-range floats clamp (Amount 3 → 1, Attack
  0 → 0.1, Rate 50 → 20); (f) load-then-save is byte-stable; (g) `STATIC_REQUIRE(::Profundum::kCurrentStateVersion == 3)`;
  `getState`'s first int32 equals the literal `3`; a valid 610-byte body with its first int32 patched to 4 → `setState` returns
  `kResultFalse` and a following `getState` is byte-identical to the one before; (e processor) with Envelope running at Amount 1
  and the registered position 0.3, the `getState` runtime blob's target (bytes `kStateV2Bytes − 20 + kRtTargetOffset` per the
  layout constants) is bit-equal to 0.3f.
- `Profundum_StateVersions` (`state_v2_test.cpp:421`) and `Profundum_StateRoundTrip` (`state_roundtrip_test.cpp:230`): their
  size expectations become `::Profundum::kStateV3Bytes` (610).
- `Profundum_MorphModWheelRouting` (`[profundum][morphmod][midi]`, in `midi_mapping_test.cpp`; `Profundum_MassPerformanceOffset`
  `:78` is not edited): (a) Source Mod Wheel (normalised 1.0), Amount ∈ {1, −0.5}: Mod Wheel 0 → 1 leaves both
  `engine()->getEndpoint(A/B).mass` bit-unchanged, and the next grid record (observer attached) reads `morphModSource == 1.0f`;
  (a′) Amount +0 and −0 (normalised 0.5, and a stream-patched −0.0f): wheel 0 → 1 pushes Mass bit-equal to the Source-Off arm;
  (a″) wheel 0.25, Amount 0 → 0.5: both endpoints re-sent at that block (mass bits change) and not on the next block; the Mass
  push stops; 0.5 → 0 re-sends once and the push resumes; (b) every other source at Amount 1: wheel 0 → 1 Mass bit-equal to the
  Source-Off arm; (c) Channel Pressure 0.5 → both endpoints' Mass + 50 (clamped by the Mass law) under every source; (d) wheel
  0.25, Amount 1: Source Envelope → Mod Wheel and back re-sends both endpoints exactly once each time.

**Implement** plan §4.5 (pack: labels, laws with the `static_assert`s against `lawOf(50)` / `lawOf(40)`, `MorphModParams`
atomics, `handleMorphModParamChange`, `registerMorphModParams` with the `defaultNormalizedFor` nudge, `formatMorphModParam`,
`morphModConfigOf` through `kNoteValueDropdownMapping`, `saveMorphModParams`, `loadMorphModParams` (EOF-safe, keep prior from the
cut on), `loadMorphModParamsToController`, `resetMorphModParams`, `morphModHandsWheelToMorph` = `source == 4 && amount != 0.0f`),
§4.6 (IDs 1003–1011, `kNumRoutedIds = 1012`, its `static_assert`, `kStateNamesEnd`, `kStateMorphModOffset`,
`kStateMorphModBytes = 36`, `kStateV3Bytes` = 610 with `static_assert(kStateV3Bytes == 610)`, `RouteKind::MorphMod`,
`setRange(kMorphModSourceId, kMorphModCycleSyncRateId, …)`, `routedCount() == 93`), §4.7 (member, route case, the one-term
`applyEndpoints` change keeping today's expression and operand order in the `else` arm, the three appended
`pushParamsToEngine` calls with `setMorphModBypass(cmp != CompareMode::AB)`, `getState` append, `setState` v1 / v2 reset and v3
`namesRead` gate, the `:747` comment "the 610-byte v3 stream"). The DataExchange `static_assert` is T017's.

**Gate note:** after T011 the factory tree is still 574 bytes and the controller still registers 84; the group's gate runs after
T015, so the intermediate state is never gated.

## T012 — Controller registration, display, state mirror, count moves (FR-020, FR-024; SC-013 (c) controller arm, SC-015, SC-022, SC-001 (c); plan §4.11, P-4, Deviation 7)

**Files edited:** `plugins/profundum/src/controller/controller.cpp`, `controller.h` (comment), and the tests:
`unit/param_table_test.cpp`, `unit/state_v3_test.cpp`, `integration/copy_compare_test.cpp`, `unit/phase11_alloc_test.cpp`,
plus every count site found by the re-grep below.

**Re-grep first** (record in `EVID/run-log/T012.txt`): `grep -rnE "\b(84|80|288|574)u?\b" plugins/profundum/src
plugins/profundum/tests`; classify each hit as "moves" (registered / routed / visible / state size) or "unrelated" (seed bytes
84-87, note C6 = 84, …). Moves: registered 84 → 93, visible 80 → 89, `574` → `kStateV3Bytes` / 610. Known sites:
`editor_layout_test.cpp:498`, `:517`, `:518`, `:535`, `:656`, `:1449`, `:1501`; `editor_lifecycle_test.cpp:224-226`;
`editor_sync_test.cpp:169`; `manual_coverage_test.cpp:82`; `param_denorm_test.cpp:257`; `param_table_test.cpp:76`, `:383`,
`:384`, `:422`; `param_wiring_test.cpp:416`; `phase11_alloc_test.cpp:175`, `:182` (and its routed-ID list `:64`);
`preset/preset_library_test.cpp:77`, `:243`, `:1262-1263`; `integration/preset_browser_test.cpp:467`; `state_v3_test.cpp:396`,
`:401`; comments in `controller.cpp:9`, `:96`, `controller.h:13`, `sound_param_table.h:12`, `processor.cpp:663`,
`preset_test_support.h:7`, `profundum_test_fixture.h:1029`. The 288 sites belong to T017; leave them.

**Tests first:**
- `Profundum_ParamTableConformance` (`param_table_test.cpp:380`) and `Profundum_SoundParamTableRouting` (`:46`): 93 registered,
  93 routed, routes cover 0..1011 with no gap, `kSoundRows.size() == 60`; the explicit SC-005 row table (`:218`) gains the nine
  C-12 rows (titles, units, defaults, flags `kCanAutomate` / `kCanAutomate | kIsList`, list strings); normalised ↔ plain round
  trips within the existing law tolerances; 1005 vs 913, 1009 vs 904, 1004 vs 902 print identical strings at normalised
  {0, 0.25, 0.5, 0.75, 1}; 1004 at 0.5 prints the +0 string.
- `Profundum_StateV3MorphMod` controller arm (SC-013 (b)(c)(d)): `setComponentState` with the same v1 / v2 / truncated / hostile
  streams: the controller's normalised values follow the processor's stored plain values; a value not stored makes no
  `setParamNormalized` call (a recording component handler / counting wrapper around `setParamNormalized`).
- `Profundum_MorphModOutsideEndpoints` (`[profundum][morphmod][morph]`, in `copy_compare_test.cpp` beside
  `Profundum_CopyEndpoint` `:226`; SC-022): `STATIC_REQUIRE(kSoundRows.size() == 60)`; no route in 1003..1011 is
  `RouteKind::Sound`; the nine at non-defaults (Source Cycle, Amount −0.6, Attack 50 ms, Decay 900 ms, Sustain 0.4, Shape
  Square, Rate 3 Hz, Sync On, Sync Rate index 7); Copy A→B, Copy B→A, Edit Target A → B → A: after each step the controller's nine
  normalised values and the processor's `MorphModParams` atomics are bit-unchanged, and `redisplayEditTarget` issues no
  `setParamNormalized` for 1003–1011.
- `Profundum_Phase11PathsDoNotAllocate` (`phase11_alloc_test.cpp:154`, SC-001 (c)): routed-ID list gains 1003–1011; Source
  rotated 0–4 per block, Amount ±1, Mod Wheel moved every block, Compare toggled inside the existing scopes; 0 allocations;
  count 93.

**Implement** plan §4.11: `registerMorphModParams(parameters)` after `registerMorphControlParams` (`:102`, comment 84 → 93);
`formatMorphModParam` in `getParamStringByValue` before the base call; `setComponentState` v1 / v2 → registered defaults for
1003–1011, v3 `namesRead` → `loadMorphModParamsToController`.

## T013 — Editor: control tags, Main `morph-band`, Advanced MORPH MODULATION block, tooltips (FR-030, FR-032; SC-016; plan §4.10)

**Files created:** `EVID/uidesc/phase14.xsl`, `EVID/uidesc/run-xslt.sh`. **Files edited:**
`plugins/profundum/resources/editor.uidesc` (through the XSLT only), `plugins/profundum/tests/unit/controller/editor_layout_test.cpp`.

**Tests first** (`editor_layout_test.cpp`, extended; existing case names kept): `Profundum_UidescBindsEverySurfaceId` (93 / 89
after T012's moves), `_UidescTagIntegrity`, `_UiLayout` (0 overlaps at 100 / 125 / 150 / 200 %), `_UiPlacement`, `_UiZoom`,
`_UiLabels`, `_UiClassRule` pass; a new SECTION or case `Profundum_UiMorphModPlacement` (`[profundum][morphmod][ui]`): the
container labelled `morph-band` binds exactly {1003, 1004} of 1003–1011, the container labelled `tab-modulation` binds all nine;
every control bound to 1003–1011 has a non-empty `tooltip`; the 1003 tooltip contains `stops pushing Mass`, the 1009 tooltip
contains `Morph Ramp Time`, the 1002 ArcKnob tooltip contains `Morph Mod`; exactly one `UpdateBanner`, `CheckForUpdatesButton`,
`VersionLabel` (unchanged).

**Implement** one identity-transform stylesheet (plan §4.10) run by `run-xslt.sh` (`npx xslt3 …`):
nine `<control-tag>` rows after `MorphRampTime` (`MorphModSource` 1003 … `MorphModCycleSyncRate` 1011); `morph-band`:
`CTextLabel "MORPH MOD"` (456, 104, 120 × 16), `COptionMenu MorphModSource` (456, 122, 124 × 20), `ArcKnob MorphModAmount`
(588, 104, 40 × 40), `CParamDisplay MorphModAmount` (456, 150, 124 × 16); `tab-modulation`: `CTextLabel "MORPH MODULATION"`
(12, 196) and the three columns of plan §4.10 (Source / Amount `BipolarSlider`; Env Attack / Decay / Sustain `ArcKnob`s; Cycle
Shape `COptionMenu`, Rate `ArcKnob`, Sync `CSegmentButton`, Sync Rate `COptionMenu`) with their labels; the tooltip texts of plan
§4.10 (1003, 1009 and the 1002 addition), a one-sentence tooltip naming the range on every other new control. The Main-page
`CSlider MorphPosition` keeps its class here (T018 swaps it). Pixels move only if `_UiLayout` reports an overlap. Run the script,
then `git diff --stat plugins/profundum/resources/editor.uidesc` into the run log; `node tools/lint-plugin-editor-baseline.js`
must pass.

## T014 — `MANUAL.md` Morph Modulation section (FR-033; SC-020 manual half; Deviation 5)

**Files edited:** `plugins/profundum/MANUAL.md`, `plugins/profundum/tests/unit/manual_coverage_test.cpp` (count 93, already moved
by T012 if the re-grep caught it; otherwise here).
A "Morph Modulation" section naming all nine registered titles verbatim ("Morph Mod Source", "Morph Mod Amount", "Morph Env
Attack", "Morph Env Decay", "Morph Env Sustain", "Morph Cycle Shape", "Morph Cycle Rate", "Morph Cycle Sync", "Morph Cycle Sync
Rate"), the formula `clamp(Morph Position + Amount × source, 0, 1)`, the five sources, the envelope restart from 0 on every
accepted note-on and rest at Sustain (no release), the free-running Cycle and its sync, the Velocity hold, the Mod Wheel / Mass
hand-over (only at a non-zero Amount), the Compare A / B bypass, the Morph Ramp Time slew consequence (C-9), "the modulation is
silent where the clamp holds the result at an endpoint", and the indicator cadence as ruled in R3. `Profundum_ManualCoversSurface`
passes with 93 titles.

## T015 — Regenerate the factory tree at 610 bytes (FR-061; Phase 13 `Profundum_CommittedPresetTreeMatches`) — **main loop**, blocks until regenerated

**Files created:** `EVID/tree/gen-tree.sh`, `EVID/run-log/T015.txt`.
`gen-tree.sh`: one build of `profundum_preset_generator profundum_tests` (`-- /nr:false`); the `generate_profundum_presets` target
(root `CMakeLists.txt`); then `profundum_tests.exe "[profundum][preset]~[long]~[.]"`; then `git status --short --
plugins/profundum/resources/presets` and the file count; last line `TREE GREEN files=<n>` when every Comp is 610 bytes
(`Profundum_FactoryPresetStateFormat`) and the preset cases pass. No definition changes in this task: every preset stores Source
Off (the defaults), so pre-existing bytes are unchanged (checked against `EVID/prephase-decoded.csv` by T024's SC-007 case and by
the T031 close comparison).
**Executor:** write, `bash -n`; return **`blocked`** with notes "main loop: run `bash
specs/profundum-phase14-morph-modulation/evidence/tree/gen-tree.sh`; quote its last line into evidence/run-log/T015.txt; unblock
T015 when TREE GREEN".

**Verified by:** G10 gate (after T015): all targets, `[profundum]` per push; 0 warnings.

---

# GROUP 11 — Processor behaviour

## T016 — Partition invariance per source, Compare bypass, pedal re-strike (SC-012 (a)(d), SC-010 (f); FR-021, C-8)

**Files edited:** `plugins/profundum/tests/unit/midi_event_test.cpp`, `plugins/profundum/tests/integration/morph_mod_processor_test.cpp`
(T003 placeholder). No production code (a red is a T007 / T011 defect).

- `Profundum_MidiEventTranslation` (`:210`) gains four SECTIONs `PartitionInvarianceMorphMod_<Envelope|Cycle|Velocity|ModWheel>`
  reusing the existing script, `kTotal = 96000` and partitions {1, 7, 32, 65, 4096} vs 512 unchanged; `ScriptedEvent` `Param`
  points at sample 0: 1003 = {0.25, 0.5, 0.75, 1.0}, 1004 = 1.0 (Amount 1), 105 = 0.5, and for Mod Wheel 6 = 1.0 (constant from
  sample 0). Envelope uses A 400 ms (1005 at `normalizedFromPlain(kMorphModTimeLaw, 400)`), Cycle Sine 2 Hz. Precondition
  (non-vacuity): in the 512 reference, with an observer attached (`setEngineObserverForTest`), ≥ 1 grid record has `morphTarget`
  bits ≠ 0.5f bits. Then `requireNoMismatches` for every partition.
- `Profundum_MorphModCompareBypass` (`[profundum][morphmod][morph]`, SC-012 (d)): processor fixture, held E1, Envelope (A 400 ms,
  S 1) and, separately, Cycle 1 Hz Sine, Amount 1, Morph 0.5; Compare A·B → A at 1.0 s (the `kMsgCompare` message built as
  `phase11_alloc_test.cpp:81-89`) and back at 1.5 s. During the bypass every grid record's `morphTarget` is bit-equal to 0.0f
  (Compare A pins b = 0) — i.e. equals b; at every grid point `morphModSource` equals a no-compare run's within 1e-6 (sources kept
  advancing, no restart).
- `Profundum_MorphModPedalRestrike` (`[profundum][morphmod][midi]`, SC-010 (f)): Sustain Pedal (5) down, note-on E1, note-off
  (latched), note-on E1 again with Envelope (A 400 ms, S 1, Amount 1): at the second note-on's grid point `morphModSource` equals a
  fresh reference envelope's first step within 1e-6 and the previous interval read > 0.5.

**Verified by:** G11 gate.

---

# GROUP 12 — Frame and producer

## T017 — Resolved position in `ProfundumSpectrumFrame` (FR-036, FR-037; SC-024 (a)(b)(c); plan §4.8, P-5)

**Files edited:** `plugins/profundum/src/processor/spectrum_frame.h`, `plugins/profundum/src/processor/spectrum_producer.h`,
`plugins/profundum/src/processor/processor.cpp` (`:355` only), `plugins/profundum/tests/integration/spectrum_frame_test.cpp`,
`plugins/profundum/tests/unit/spectrum_producer_test.cpp` (`:49`).
**Precondition:** R3 ruled as the plan reads it; otherwise STOP.

**Tests first:**
- `Profundum_MorphIndicatorFrame` (`[profundum][morphmod][ui][spectrum]`, in `spectrum_frame_test.cpp`): (a)
  `static_assert(sizeof(ProfundumSpectrumFrame) == 320)`, `% 32 == 0`, trivially copyable, standard layout,
  `offsetof(…, morphPosition) == 288`, `windowEndSample` 24, `partialPower` 32; the handler block (`kHandlerBlockBytes`, `:107`,
  320) is a multiple of 32 and ≥ sizeof; (b) for Source Off (b 0.3) and each non-Off source at Amount 1, every published frame's
  `morphPosition` equals `engine()->morphResolvedPosition()` sampled at that frame's production within 1e-6 and lies in [0, 1];
  Source Off reads 0.3f bit-equal.
- Extend `Profundum_SpectrumFrameDoesNotChangeAudio` (`:772`) with a Cycle-at-Amount-1 arm (bit-identical audio with the frame path
  on and off) and `Profundum_SpectrumFrameAllocationFree` (`:874`) with the same arm (0 allocations).
- Static asserts at `spectrum_frame_test.cpp:95`, `:107` and `spectrum_producer_test.cpp:49` move 288 → 320.

**Implement** plan §4.8: `float morphPosition = 0.0f;` at 288 and `std::uint8_t reserved2[28] = {};`; static asserts
`sizeof == 320 && <= 320`, offsets; `SpectrumProducer::onInterval` first statement reads `engine.morphResolvedPosition()`,
sanitised with `detail::isFinite` and clamped to [0, 1] into `morphPos_`; both staging sites write it; `reset()` zeroes it;
`processor.cpp:355` `static_assert(kBlockSize == 320u, …)` (rounding expression unchanged).

**Verified by:** G12 gate (includes `Profundum_SpectrumFrameCadence` unchanged).

---

# GROUP 13 — Live indicator view

## T018 — `MorphIndicatorSlider`, controller wiring, uidesc class swap (FR-035; SC-024 (e)(f), SC-016 class rule; plan §4.9)

**Files created:** `plugins/profundum/src/ui/morph_indicator_slider.h`, `plugins/profundum/src/ui/morph_indicator_slider.cpp`,
`EVID/uidesc/phase14-indicator.xsl`.
**Files edited:** `plugins/profundum/src/controller/controller.h`, `controller.cpp`, `plugins/profundum/src/ui/panel_sub_controller.cpp`,
`plugins/profundum/CMakeLists.txt` (the `.h/.cpp` in the UI list), `plugins/profundum/tests/CMakeLists.txt` (second compilation of
`../src/ui/morph_indicator_slider.cpp`), `plugins/profundum/resources/editor.uidesc` (XSLT: the Main-page `CSlider
control-tag="MorphPosition"` becomes `class="MorphIndicatorSlider"`, every other attribute kept),
`plugins/profundum/tests/unit/controller/ui_views_test.cpp`, `plugins/profundum/tests/unit/controller/editor_lifecycle_test.cpp`.

**Tests first:**
- `Profundum_UiMorphIndicator` (`[profundum][morphmod][ui]`, in `ui_views_test.cpp`): a `MorphIndicatorSlider` with value 0.2:
  before any `setIndicator`, `indicatorPosition() == getValueNormalized()` and `hasIndicator() == false`; `setIndicator(0.7f)` →
  `indicatorPosition() == 0.7f`, `getValueNormalized()` bit-unchanged (0.2f), a listener counting `valueChanged` /
  `beginEdit` sees 0; `setIndicator(1.5f)` → 1.0f; `clearIndicator()` falls back to the value. Controller path: a frame with
  `morphPosition = 0.65f` delivered through `onDataExchangeBlocksReceived` → the registered slider's `indicatorPosition() ==
  0.65f`; the Morph Position parameter's normalised value bit-unchanged and no `performEdit`; after `queueOpened` and before a
  frame, the indicator falls back to the slider value; a slider registered after a frame takes the cached value.
- `Profundum_UiClassRule` accepts the slider (it `dynamic_cast`s to `CSlider`, `editor_layout_test.cpp:617-619`); the Main-page
  `MorphPosition` control is a `MorphIndicatorSlider` in the headless editor.
- `Profundum_EditorLifecycle` (`editor_lifecycle_test.cpp`): open / close cycles leave the shared-view count unchanged, and
  `viewPointersNullForTest()` is true after `willClose` (includes `morphIndicator_`).

**Implement** plan §4.9: the class (`CSlider` subclass, `setIndicator` clamps and calls `invalid()` only on a bitwise change,
`draw` = `CSlider::draw` then a 2 px full-height line at the centre of `calculateHandleRect(indicatorPosition())` in
`CColor(255, 255, 255, 220)`; never `setValue` / `valueChanged` / `beginEdit` / `performEdit`), its `ViewCreatorAdapter` named
`"MorphIndicatorSlider"` with base `"CSlider"`; `ProfundumPanelSubController::verifyView` registers it via
`owner_->registerMorphIndicator(slider)` (the `registerRingKnob` route); the controller members, `queueOpened`,
`onDataExchangeBlocksReceived`, `registerMorphIndicator`, `willClose`, `viewPointersNullForTest` edits. UI thread only.

**Verified by:** G13 gate (CMake touched: all targets build; 0 warnings).

---

# GROUP 14 — Render harness carriage

## T019 — Decode, overlay, stream write, wheel carrier, refusals, `MorphModOverride`, twin, source-keyed reader, close comparison (FR-045; SC-023 (e), SC-008 case; plan §4.12, §5.5, §5.8; Deviations 2, 4)

**Files edited:** `plugins/profundum/tests/preset_test_support.h`, `plugins/profundum/tests/preset_render_support.h`,
`plugins/profundum/tests/param_path_audibility_helpers.h`, `plugins/profundum/tests/integration/morph_mod_preset_test.cpp`.
All edits append-only (C-17 item 4): existing callers compile and behave unchanged.

**Tests first** (in `morph_mod_preset_test.cpp`):
- `Profundum_MorphModHarnessCarriage` (`[profundum][morphmod][preset]`, SC-023 (e)): (1) a probe-context script (Envelope,
  A 150 ms, D 600 ms, S 0.6, Amount 0.8) and (2) a preset-context M-WHEEL hi arm on the first factory preset (`ctx.morphModWheel =
  1`, source forced to Mod Wheel, Amount 1): after the sample-0 block and `prepare`, `fx->engine()->morphModulation()` equals the
  effective script's `e.morphMod` (enums and the note pair equal, every float bit-equal) and `fx->engine()->morphModWheel()` is
  bit-equal to `e.morphModWheel`. A script with `morphModBypass = true`, or with a `MorphModulation` or `MorphModBypass` event,
  returns the named refusal and renders nothing (`ctx.stop` non-empty, contains `"Morph Mod"`). `decodePresetState` of a
  610-byte Comp whose block holds the T011 (a) non-defaults reads them back (floats bit-equal) with `morphMod.present == true`.
  The inverse lookup is a helper `mmSyncIndexOf(NoteValue, NoteModifier)` (sweep the name first) that returns the
  `kNoteValueDropdownMapping` index, or −1 for a pair not in the table; `CHECK(mmSyncIndexOf(Quarter, None) == 13)`, a pair
  absent from the table reads −1, and `withMorphMod` REQUIREs the index ≥ 0.
- `Profundum_MorphModOverrideSmoke` (`[profundum][morphmod][preset]`): on one factory preset at E1, `renderPresetSweep(comp,
  {E1}, 48000, false)` vs `renderPresetSweep(…, std::nullopt, {.source = 2, .amount = 0.0f})` `memcmp`-identical, and vs
  `{.source = 2, .amount = 1.0f}` different (non-vacuity); `{.compare = CompareMode::A}` leaves `compareModeForTest() == A`
  after prepare; `REQUIRE(!(maxMass && mm.wheel))` is honoured (the predicate, not a crash).
- `Profundum_MorphModPrePhaseFingerprints_<k>of4` (`[profundum][morphmod][preset][.harvest]`, SC-008, FR-061; Deviation 2): for
  presets `i % 4 == k` present in `EVID/prephase-fingerprints.csv` (a harvested preset missing from `allPresets()` fails; a new
  preset is skipped and listed): converted ⇔ `decodePresetState(comp).morphMod.source != 0`; unconverted → `renderPresetSweep(comp,
  {note}, 48000, false)`, converted → `…, std::nullopt, {.source = 0}`; per note and channel `compareFingerprints(actual, ref)`
  with the shared tolerances; `withinTolerance()` for every row (0 failures); worst metric and sample errors printed. Every
  preset's pre-existing regions bit-equal to `EVID/prephase-decoded.csv`. The CSV is parsed at run time (hex floats via
  `std::strtod`, checkpoints via `std::stoul(…, 16)` + `std::bit_cast`).

**Implement** plan §4.12 items: `DecodedPresetState::MorphMod` + decode at `kStateMorphModOffset` when `bytes >= kStateV3Bytes`;
`PresetArmContext` appends `morphModCandidate`, `forceMorphModSource`, `morphModWheel`; `effectiveScriptFor` overlay steps
(1)–(4); `withMorphMod` (C-10 order, Sync Rate as the inverse table index) called right after `withEndpoints` (`:407`);
`processorSampleZeroParams` appends `(kModWheelId, e.morphModWheel)` only when `!(e.morphModWheel == 0.0f)`;
`processorCarrierParam` `case K::MorphModWheel`; `processorRenderRefusal` named refusals for bypass and the two event kinds;
`MorphModOverride` + the trailing defaulted parameter of `renderPresetSweep` (source / amount patched with `patchI32` / `patchF32`
next to the seed patch; wheel block after the `maxMass` blocks; compare sent after `setState` via `proc->notify`, verified after
`prepare`); `ppRenderProcessor` twin from `morphBasePosition()`, `morphModulation()`, `morphModWheel()`, `morphModBypass()`
keeping the script's note events. **The source-keyed reader (Deviation 4):** `[[nodiscard]] inline auRd::AuCell
morphModSourceCell(std::int32_t source, const char* row, const DecodedPresetState& q, std::uint8_t note)` implementing plan §4.12
reader steps 1–6 and §5.5 (render `kMorphModArmTotal = grid-round(3.0 s · fs + W) + 32` samples, host block 64, MONO, the note
held; lo / hi arms per the C-15 table keyed by `source` (1 Envelope, 2 Cycle: lo forced Off, hi as stored; 3 Velocity: v 1 vs
127; 4 Mod Wheel: wheel 0 vs 1); window = earliest `t0 ∈ [1.0 s, 3.0 s]` on the 32-sample grid with `max − min ≤ 0.01` of
`resolved.position` in both arms over `[t0, t0 + W)` and `|p̄_hi − p̄_lo| ≥ 0.05`; taps shifted so `dNlDb`'s 1.0 s start lands on
`t0`; `value = |D_nl(hi) − D_nl(lo)|`, bar `kAuBarDb` "≥ 3 dB", remark carrying `t0`, `p_lo`, `p_hi` or "no window: …").

**Verified by:** G14 gate (per push; the `[.harvest]` shards do not run here).

---

# GROUP 15 — Main-page audibility rows

## T020 — Rows 1003 and 1004 in `Profundum_MainPageAudibility` (FR-031 as amended by R6; SC-017 (c), SC-023 (c); plan §5.6, P-6)

**Files edited:** `plugins/profundum/tests/integration/main_page_audibility_test.cpp`.
**Precondition:** R6 ruled (FR-031 row amended); otherwise STOP.
Two SECTIONs through `ppRenderProcessor` (twin carries the modulation, T019), endpoints `auStateAs()` / `auStateB()` loaded via
`setState`, every lever a normalised parameter change, 48 kHz, host block 64, notes E1 and C3, render `1.0 s + W + 0.25 s`
(W = `TestUtils::kLowFrequencyFftSize` samples):
- 1003 (R7, 2026-10-27): every script note at velocity 127, lo `{1003: 0, 1004: 1.0, 105: 0}`, hi `{1003: 0.75, 1004: 1.0, 105: 0}`
  (Off → Velocity; the Off → Mod Wheel pin with wheel 1 was confounded by the wheel's Mass push, see Defects, G15 gate a1).
- 1004: Source Velocity `{1003: 0.75, 105: 0}`, every script note at velocity 127, lo `{1004: 0.5}`, hi `{1004: 1.0}`.
Descriptor: `auBm::dNlDb` on the Char / twin tap (characterL/R vs linearCoreL/R); value `|hi − lo|`; bar ≥ 3 dB; `mpReport`.
Preconditions (REQUIRE, before the bar): over `[1.0 s, 1.0 s + W)` the grid-record `resolved.position` varies ≤ 0.01 in both
arms, is ≥ 0.9 in the hi arm and ≤ 0.01 in the lo arm. P-6: print both arms' mean `smoothedMass` for the 1003 row. SC-023 (c): on
the 1003 hi arm, the twin's grid-record `resolved.position` equals the processor render's within 1e-6 at every grid point of the
window. Record the added wall clock in `EVID/run-log/T020.txt`; if the two rows add > 15 s, move them to a new
`Profundum_MainPageAudibilityMorphMod` tagged `[profundum][morphmod][audibility][long]` and record the move as a Defects entry.

**Verified by:** G15 gate.

---

# GROUP 16 — Probe

## T021 — `Profundum_MorphModProbe_<k>of32` and its scripts (FR-044; SC-017 (a) input; plan §5.7 steps 1–4)

**Files edited:** `plugins/profundum/tests/integration/preset_probe_test.cpp` (appended cases). **Files created:**
`EVID/probe/run-probe.sh`, `EVID/probe/merge.js`, `EVID/probe/select.js`.
- 32 TEST_CASEs `Profundum_MorphModProbe_<k>of32` (`[profundum][morphmod][probe][.probe]`), shard = definition index `% 32`. For
  every existing preset P (as stored, Source Off) and source column X, render X's arms through `morphModSourceCell` under a
  `ScriptRenderScope(&processorRenderHook, &ctx)` in preset context (the existing probe pattern, `:780-800`), with
  `ctx.morphModCandidate` = each candidate of plan §5.7 step 1: M-ENV Amount `σ·{1, 0.6}` (σ = +1 if P's stored position ≤ 0.5
  else −1) × Attack {0.1, 150} ms × Sustain {1, 0.6}, Decay 600 ms (8); M-CYCLE Amount {+1, −1} × Shape {Square, S&H}, Rate
  0.05 Hz free (4); M-VEL Amount `σ·{1, 0.6}` (2); M-WHEEL Amount `σ·{1, 0.6}` (2); at E1 and C3. The forced-Off lo arm is
  rendered once per (P, note) and reused. Rows to `EVID/probe/shard-<k>.csv`: `preset,defIndex,column,candidate (the nine
  fields),note,t0,p_lo,p_hi,dnl_lo,dnl_hi,contrast,pass,primary,singleState` where `singleState` is true iff P claims (primary or
  secondary) a column whose `showcaseArmPlan` on P with `morph.position = 0.5` records the override `"kMorphPositionId=0"`
  (`preset_render_support.h:679-684`).
- `merge.js`: shards → `EVID/probe-morphmod.csv`, stamping `runId = <git short hash>-<UTC yyyymmddThhmm>`; fails on a missing shard.
- `select.js`: plan §5.7 steps 2–4 on the merged CSV for the ruled rule (R4): carriable = some candidate passes at both notes;
  margin = `min(contrast_E1, contrast_C3) − 3`; eligible = `!singleState` and P's primary keeps ≥ 1 other primary after every
  conversion chosen so far; rule (i): columns in order M-ENV, M-CYCLE, M-VEL, M-WHEEL, each picks the eligible carrier with the
  largest margin (ties: definition order); rule (ii): presets in descending best-margin order, each converts for its best-margin
  column if still eligible; a preset converts at most once; a column with no eligible carrier reports "NEW PRESET NEEDED" with its
  best probe row. Writes `EVID/conversions.md` (column, preset, winning candidate, margin; or the new-preset row) and prints
  `SELECTION <n> conversions, <m> new`.
- `run-probe.sh`: one build, 32 concurrent shards each to `EVID/run-log/T022/<k>.log` with wall clock, then `merge.js`,
  `select.js`; last line `PROBE GREEN|RED`.
**Verified by:** G16 gate (compiles; hidden cases do not run).

## T022 — Run the probe (FR-044) — **main loop**, blocks until recorded

**Files created:** `EVID/run-log/T022.txt`.
Executor: if `EVID/probe-morphmod.csv` and `EVID/conversions.md` are absent, return **`blocked`** with notes "main loop: run `bash
specs/profundum-phase14-morph-modulation/evidence/probe/run-probe.sh`; quote the per-shard wall clocks, the SELECTION line and
any NEW PRESET NEEDED row into evidence/run-log/T022.txt; if a column has no passing candidate in ANY preset, STOP AND SURFACE
with the best measured contrast (FR-023 rule, spec C-15); unblock T022 when PROBE GREEN".

---

# GROUP 17 — Library

## T023 — Columns, showcase cases, conversions, library rules (FR-040 – FR-042, FR-046, FR-063; SC-017 (a)(b) wiring, SC-018; plan §4.12 showcase cases, §4.13, §5.7 step 5; Deviation 4)

**Files edited:** `tools/profundum_preset_defs.h`, `plugins/profundum/tests/profundum_preset_host.h`,
`plugins/profundum/tests/preset_render_support.h`, `plugins/profundum/tests/unit/preset/preset_library_test.cpp`.
**Precondition:** R4 ruled and `EVID/conversions.md` exists (T022).

**Tests first** (`preset_library_test.cpp`):
- `Profundum_CoverageMatrixComplete` (`:1312`): 57 columns, 0 empty, exactly 1 primary per preset; every one of the 53
  pre-existing columns still has a primary.
- `Profundum_PresetDefsRules` (`:695`), extended: (FR-042) for every factory definition's generated Comp,
  `decodePresetState(...).morphMod`: `source ∈ [0, 4]`, `cycleShape ∈ [0, 5]`, `cycleSync ∈ {0, 1}`, `cycleSyncRate ∈ [0, 29]`,
  every float `detail::isFinite` and inside its law box (amount [−1, 1]; attack / decay [0.1, 10 000]; sustain [0, 1]; rate
  [0.01, 20]); 0 claimants of a single-state column storing Source ≠ 0. (FR-063) every def whose Comp decodes `source == 0` has a
  row in `EVID/prephase-claims.csv` with the same primary label and the same sorted secondary labels; a pre-phase row with no def
  fails; a def with no row must be listed as an added preset in `EVID/conversions.md`. (P-9) the set of defs whose primary is in
  the `MorphMod` group, minus the added presets, equals the selection `select.js`'s rule produces, re-derived in C++ from
  `EVID/probe-morphmod.csv` with the constant `kMorphModConversionRule` (= the R4 ruling). Library size printed.

**Implement:** `CapabilityGroup::MorphMod`; `Capability` gains `MEnv, MCycle, MVel, MWheel` before `Count`;
`kNumCapabilities == 57` static_assert; `capabilityTable()` rows "M-ENV Morph Mod Envelope", "M-CYCLE Morph Mod Cycle", "M-VEL
Morph Mod Velocity", "M-WHEEL Morph Mod Wheel", `atId = 0`; the `controls` comment and `isAllowedControlId` allow 1003–1011;
`showcaseArmPlan` gains the four cases of plan §4.12 before `case C::Count:` (`:1258`) with `pl.cell` calling
`morphModTrajectoryCell(c, q, note)`, a thin wrapper mapping the column to its source and row label and calling
`morphModSourceCell`, plus a REQUIRE that `EVID/probe-morphmod.csv` holds a passing row for the same preset, source and stored
settings (SC-017 (a)); the conversions of `EVID/conversions.md` (plan §5.7 step 5): the winning candidate's nine `controls` as
normalised values through the T011 laws, primary moved to the new column, old primary not re-claimed, secondaries unchanged,
description rewritten to name the new primary (Phase 13 FR-065); any NEW PRESET row authored as a new definition on the base the
probe names. Never hand-edit a Comp.

## T024 — Transparency shards and harness fidelity (FR-060 (a), FR-061; SC-007, SC-023 (a)(b); Deviation 6)

**Files edited:** `plugins/profundum/tests/integration/morph_mod_preset_test.cpp`, `plugins/profundum/tests/unit/preset/preset_library_test.cpp`
(`Profundum_FactoryPresetStateFormat` `:1239`: `d.bytes == ::Profundum::kStateV3Bytes` and `pf.comp.size()` likewise, plus the
first int32 read as the **literal** 3).
- `Profundum_MorphModPresetTransparency_<k>of64` (`[profundum][morphmod][preset][long]`, nightly per R10; re-sharded from 32 at G17, see Defects), shard = (2 · definition index + note index) % 64,
  notes E1 and C3, all through `renderPresetSweep(comp, {note}, 48000, false, std::nullopt, mm)`: a stored-Off preset: for
  w ∈ {0, 1}, reference `{.source = 0, .wheel = w}` (w = 0: `mm = {}`) vs `{.source = X, .amount = 0.0f, .wheel = w}` for
  X ∈ {1, 2, 3, 4}; for C ∈ {A, B}, reference `{.source = 0, .compare = C}` vs `{.source = X, .amount = 1.0f, .compare = C}` for
  each X. A converted preset: the same with the reference forced `{.source = 0}` and the compare arms for its stored source S
  only. Every pair `std::memcmp`-identical on L and R. Every Comp's first int32 reads literal `3` and its size is 610; its
  pre-existing regions are bit-equal to `EVID/prephase-decoded.csv`. Each shard prints its wall clock; target ≤ 9 s each; the
  G17 gate records them, and a shard above 9 s is a Defects entry re-sharded to 64 (never fewer renders).
- `Profundum_MorphModHarnessFidelity` (`[profundum][morphmod][preset][long]`, SC-023 (a)(b)): for each of the four new columns'
  primaries, E1 and C3: the C-5 fidelity arm passes (processor vs engine-direct within `kMetricTolerance` / `kSampleTolerance` on
  sumL / sumR / characterL / characterR, `ctx.fidelity` all `pass`), and over the placed window the twin's grid-record
  `resolved.position` equals the processor render's within 1e-6 at every grid point.

## T025 — Regenerate the tree with the conversions — **main loop**, blocks until regenerated

**Files created:** `EVID/run-log/T025.txt`. Executor: return **`blocked`** with notes "main loop: run `bash
specs/profundum-phase14-morph-modulation/evidence/tree/gen-tree.sh`; quote TREE GREEN and `git status --short --
plugins/profundum/resources/presets` (the converted and added presets) into evidence/run-log/T025.txt; unblock T025".

## T026 — Library lanes (FR-041, FR-043, FR-051; SC-017 (b), SC-018 Phase 13 gates, SC-019 presets, SC-023 (a)(b)) — **main loop**, blocks until green

**Files created:** `EVID/library/run-library-lanes.sh`, `EVID/run-log/T026.txt`.
`run-library-lanes.sh` (detached; status to `EVID/library/status.txt`): one build; then concurrently, one process per case name,
each to its own log with its wall clock: `Profundum_PresetShowcase_<k>of8` (k 0–7; the four new columns' cells land in
`specs/profundum-phase13-presets-release/evidence/coverage-matrix/shard-<k>.csv`, merged by that phase's `evidence/merge-csv.js`),
`Profundum_PresetMorphEndpoints_<k>of8`, `Profundum_PresetMorphSweep`, `Profundum_PresetSweep`, `Profundum_PresetSweepRates`,
`Profundum_PresetTruePeakMaxMass`, `Profundum_PresetMonoSum`, `Profundum_PresetDeterminism`, `Profundum_PresetLoudnessMatch`,
`Profundum_PresetDistinctness`, `Profundum_MorphModHarnessFidelity`; then the per-push `[profundum][preset]` set (includes
`Profundum_PresetLoadUnderHeldNote`, the render guard and the SC-007 shards). After all exit: the merged coverage matrix holds
an `M-ENV`, `M-CYCLE`, `M-VEL`, `M-WHEEL` primary row at E1 and C3 with value ≥ 3 dB, and each new primary's minimum margin is
printed. Last line `LIBRARY LANES GREEN|RED`. Restore any Phase 9 evidence the cases wrote (`git checkout --
specs/profundum-phase9-voice-engine/evidence/`).
**Executor:** write, `bash -n`; return **`blocked`** with notes "main loop: run run-library-lanes.sh detached; a red Phase 13 gate
on a converted preset is fixed by replacing that conversion with the next carrier from probe-morphmod.csv (plan §10) and
re-running T025 + this lane, never by editing a bar; quote every lane's verdict, wall clock and worst margin vs the pre-phase run
into evidence/run-log/T026.txt; unblock T026 when LIBRARY LANES GREEN".

**Verified by:** G17 gate (per push, after T026).

---

# GROUP 18 — CPU cases (hidden)

## T027 — `Profundum_ProcessorCpuMorphMod`, `VoiceEngine_WorstIntervalMorphMod` (FR-053; SC-014 (a)(b)(d))

**Files edited:** `plugins/profundum/tests/integration/processor_cpu_test.cpp`, `dsp/tests/unit/systems/profundum_engine_perf_test.cpp`.
- `Profundum_ProcessorCpuMorphMod` (`[profundum][morphmod][.perf][performance]`): the Phase 11 SC-020 method of
  `Profundum_ProcessorCpuFullSurface` (`:641`: 640 × 10-block trials, P / D alternated, control arm within 1.05 or the run is
  INVALID) on the same switching pair, with Cycle Sine 20 Hz at Amount 1 and Morph Ramp Time 5 ms: MONO ≤ 8 % and 4 voices ≤ 25 %
  of one core at 48 kHz.
- `VoiceEngine_WorstIntervalMorphMod` (`[systems][profundum][morphmod][voice-engine][performance][.perf]`, appended after
  `VoiceEngine_WorstInterval` `:596`): its method with the same modulation; worst 64-sample host block at 4 voices ≤ 100 % of one
  core; also times `ProfundumMorphModulator::advanceInterval` over 10⁶ calls per source and prints ns per call.
- `Profundum_PresetCpu` (`:885`) is unchanged: it covers opted-in presets as stored (SC-014 (c)).
Never relax a budget; these run only in T035.

**Verified by:** G18 gate (build; hidden).

---

# GROUP 19 — Changelog

## T028 — CHANGELOG `[1.0.0]` (FR-034; SC-020 changelog half)

**Files edited:** `plugins/profundum/CHANGELOG.md`.
In the open `## [1.0.0] - 2026-10-25` entry: one `### Added` bullet "**Morph Modulation**" (the five sources, the formula, the
envelope restart, the Mod Wheel / Mass hand-over at a non-zero Amount, the Compare bypass, the live indicator, the presets that
show it off); the parameter-count bullet (`:24`) reads 93 parameters and names Morph control 1000–1011; the state-size bullet
(`:35`) reads 610 bytes (format version 3) and mentions the modulation block; the library size if T023 added presets. Then
`git diff --quiet plugins/profundum/version.json` must exit 0 (record it).

**Verified by:** G19 gate.

---

# GROUP 20 — CMake registration audit (single)

## T029 — CMake registration and audit (Deviation 3)

**Files edited:** `dsp/tests/CMakeLists.txt`, `plugins/profundum/tests/CMakeLists.txt`, `plugins/profundum/CMakeLists.txt` only for
fixes the audit finds. **Files created:** `EVID/run-log/T029.txt`.
Audit: `profundum_morph_modulator_test.cpp` and `profundum_engine_morphmod_test.cpp` appear exactly once, in `dsp_systems_tests`,
and in no IEEE list; `morph_mod_processor_test.cpp` and `morph_mod_preset_test.cpp` exactly once in `add_executable(profundum_tests
…)` and once in the `-fno-fast-math` block; `processor_cpu_test.cpp` still in no flag block; `morph_indicator_slider.cpp` once in
the plugin sources and once in the tests' second-compilation list; `morph_mod_params.h` listed; `krate_test_pch` still last;
`node tools/lint-ieee-flag-sets.js` passes; no `/wd`, `-Wno-` or `pragma warning` added (`git diff 3dfc5b7d -- '*.txt' '*.h'
'*.cpp' | grep -cE "^\+.*(/wd|-Wno-|pragma warning)"` = 0).

**Verified by:** G20 gate: every target builds with 0 warnings; full per-push `[profundum]` green.

---

# GROUP 21 — Integration lanes (main loop at close)

## T030 [P] — Scope fence, ODR re-sweep, greps (FR-018, FR-019, non-goals)

**Files created:** `EVID/close/scope-fence.js`, `EVID/run-log/T030.txt`.
`scope-fence.js`: the union of `git diff --name-only 3dfc5b7d` and `git status --porcelain --untracked-files=all` must match the
allow-list: the `dsp/` files of "Rules → dsp/ fence"; `plugins/profundum/**`; `tools/profundum_preset_defs.h`;
`specs/profundum-phase14-morph-modulation/**`; `specs/profundum-phase13-presets-release/evidence/coverage-matrix*`;
`specs/Profundum-roadmap.md`; (`.serena/project.yml`, `.claude/workflows/profundum-phase.js` excluded by name as pre-phase
working-tree changes). Any other path is RED (in particular `plugins/shared`, `tests/test_helpers`, any other plugin,
`bass_morph_engine.h`, `profundum_motion.h`, `lfo.h`, `adsr_envelope.h`, `morph_state.h`). Greps: each ODR name defined exactly once;
`grep -rnE "std::isnan|std::isinf|std::isfinite|std::fpclassify|infinity\(\)|quiet_NaN\(\)"` over the phase's new and changed
lines = 0; `static_assert(kMorphStateBytes == 201)` untouched; `kSoundRows` 60 rows; `ProfundumCall` unchanged against
`3dfc5b7d`; `kCurrentStateVersion = 3`; `version.json` unchanged; `node tools/lint-odr.js`, `node tools/lint-layers.js`. Prints
`SCOPE GREEN|RED`.
**Executor:** write and run (seconds); quote the output.

## T031 [P] — Suites lane, close comparison and pluginval — **deferred to the main loop** (SC-008, SC-021, the C-16 lanes)

**Files created:** `EVID/close/suites.sh`.
**Lane scope (encoded 2026-10-18, Phase 6 ruling G9-4; spec C-16).** `suites.sh` runs detached, writes
`EVID/run-log/close/status.txt` (lane, exit, wall clock):
1. **Change set.** `git diff --name-only 3dfc5b7d -- dsp/include dsp/src plugins/shared tests/test_helpers dsp/tests` lists the
   modified shared files (expected: `profundum_engine.h`, `profundum_engine_observer.h`, `profundum_voice_engine_test_helpers.h`,
   the three appended DSP TUs, `dsp/tests/CMakeLists.txt`); anything outside the T030 allow-list is RED and stops the script.
2. One build of `dsp_systems_tests dsp_ieee_tests profundum_tests Profundum profundum_preset_generator` (`-- /nr:false`); 0 warnings.
3. Concurrently, each to its own log with wall clock:
   (a) `profundum_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"` (the phase executable's per-push lane);
   (b) `dsp_systems_tests.exe "[profundum]~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"` and
   `dsp_ieee_tests.exe "[profundum]"` — the per-push set of every consumer of the modified headers (all of them carry
   `[profundum]`, see "Gate scope note");
   (c) the **consumers' full suites**: the `[long]` cases of the TUs that include (transitively) a modified header, listed by
   `-# --list-tests "[#<tu>][long]"` per TU (TU list = `grep -rlE '#include .*(profundum_engine\.h|profundum_engine_observer\.h|
   profundum_voice_engine_test_helpers\.h|profundum_audibility_readers\.h|preset_render_support\.h|param_path_audibility_helpers\.h)'`
   over `dsp/tests/unit/systems` and `plugins/profundum/tests`), one process per case name — the Phase 9 click / audibility /
   chain / aliasing / soak `[long]` cases and the Phase 11 / 13 `[long]` cases, which include this phase's own `[long]` cases
   (`Profundum_MorphModHarnessFidelity`, any moved main-page rows). Skip the Phase 13 library `[long]` names when T026's
   `EVID/library/status.txt` ends GREEN and is newer than every modified file under `plugins/profundum` and `tools/`; never every
   `[long]` case of an executable outside that TU list;
   (d) `profundum_tests.exe "Profundum_MorphModPrePhaseFingerprints_<k>of4"` × 4 (SC-008 close gate, 0 failures; print the worst
   metric / sample errors);
   (e) `tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Profundum.vst3` → exit 0;
   (f) `node tools/check-preset-generator-determinism.js --plugin profundum` → exit 0;
   (g) `--list-tests` shows every SC case name of this file (hidden ones under their hidden tags).
4. Summary block with every lane's verdict and wall clock; then `node tools/clean-test-output.js --apply` is NOT run here (close
   procedure step 9).
**Executor:** write, `bash -n`, return done with notes "deferred to the main loop at close: run `suites.sh` detached concurrently
with tidy.sh; quote each lane's last lines into evidence/run-log/T031.txt; a Phase 14 red is owned (fix, re-run only the affected
case); a timing-budget assertion of an untouched case that fails under the concurrent lanes is recorded as load, not re-run".

## T032 [P] — Portability and lints (FR-054; SC-021)

**Files created:** `EVID/close/portability.sh`, `EVID/run-log/T032.txt`.
1. `.cpp` list = `git diff --name-only 3dfc5b7d` ∪ untracked, filtered to `\.cpp$` (the four new TUs, `morph_indicator_slider.cpp`,
   `processor.cpp`, `controller.cpp`, `panel_sub_controller.cpp`, the edited test TUs). `node tools/check-portability.js <files…>`
   with a 300 s ceiling; required `check-portability: all clear -- N compiled.` with N = list length.
2. `node tools/lint-finite-bitmask.js`, `node tools/lint-nonfinite-symbols.js`, `node tools/lint-ieee-flag-sets.js`,
   `node tools/lint-plugin-editor-baseline.js`, `node tools/lint-apple-globals.js` (read each usage line first), `node
   tools/lint-float-bit-goldens.js`, `node tools/lint-simd-aligned-loadstore.js`; each exit 0.
3. `wsl --shutdown`. Output to `EVID/run-log/close/portability.log`, last line `PORTABILITY GREEN|RED`.
**Executor:** write and run once. If it fails with `wsl: Failed to translate` or exceeds 5 minutes, stop it and return done with
notes "deferred to the main loop at close: run `bash …/evidence/close/portability.sh` unsandboxed".

## T033 [P] — macOS fast-math repro of the phase's DSP TUs — **deferred to the main loop**

**Files created:** `EVID/close/fastmath-repro.sh`.
In WSL: `bash tools/macos-fastmath-repro.sh systems/profundum_morph_modulator_test.cpp systems/profundum_engine_morphmod_test.cpp
systems/profundum_voice_engine_helpers_test.cpp systems/profundum_engine_display_test.cpp systems/profundum_engine_test.cpp --
"[profundum]~[long]~[.]"`, then the same with `--ieee`. Each to `EVID/run-log/close/fastmath[-ieee].log`. A case red without
`--ieee` and green with it is a macOS red to fix before push (spec FR-054; root `CLAUDE.md` fast-math rules); `wsl --shutdown`
after. Last line `FASTMATH GREEN|RED`.
**Executor:** write, `bash -n`; return done with notes "deferred to the main loop at close: run fastmath-repro.sh (minutes; the
object cache is per mode); quote both summary lines into evidence/run-log/T033.txt". (Plugin tests are out of the script's scope;
read its "LIMITS" header.)

## T034 [P] — clang-tidy — **deferred to the main loop** (FR-054; SC-021)

**Files created:** `EVID/close/tidy.sh`.
Reconfigure `build/windows-ninja` inside vcvars64 (test and plugin CMake changed: stale compile-database hazard); confirm the new
TUs and `morph_indicator_slider.cpp` are in `compile_commands.json`; `./tools/run-clang-tidy.ps1 -Target profundum -BuildDir
build/windows-ninja`; then the DSP TUs of this phase **by file list** (the four `dsp/tests/unit/systems` TUs it created or
appended to, plus one TU that includes each modified header), each under a 900 s wall-clock ceiling, never the whole `dsp`
sweep (a 15 h hang is on record). Last line `TIDY GREEN|RED` (0 warnings).
**Executor:** write, `bash -n`; return done with notes "deferred to the main loop at close: run tidy.sh concurrently with
suites.sh; quote into evidence/run-log/T034.txt".

## T035 [P] — CPU lane, alone — **deferred to the main loop** (FR-053; SC-014)

**Files created:** `EVID/close/cpu-lane.sh` (`node tools/run-cpu-tests.js profundum_tests` then `node tools/run-cpu-tests.js
dsp_systems_tests` to `EVID/run-log/close/cpu.log`; last line `CPU GREEN|RED`).
Precondition: T031–T034 finished; the machine idle; never straight after the clang-tidy lane (Phase 12 handoff 13.5).
**Executor:** write, `bash -n`; return done with notes "deferred to the main loop at close: run cpu-lane.sh last and alone; quote
`Profundum_ProcessorCpuMorphMod` MONO / 4-voice readings, the control ratio, `VoiceEngine_WorstIntervalMorphMod`, the modulator ns
per interval and every preset's `Profundum_PresetCpu` reading into evidence/run-log/T035.txt". A red reading is re-run once after
the machine has idled; still red → a defect reported to the user. No budget relaxed, no workload shrunk.

---

## Close procedure (main loop, in this order)

1. T001: grill the user on R1–R6, write `EVID/plan-rulings.md`, unblock T001 (rows amended first, then Clarifications).
2. G2 – G3 gates. T005: `run-harvest.sh` → HARVEST GREEN → unblock (before any engine or generator edit).
3. G5 – G9 gates (both DSP executables `[profundum]`).
4. G10: T015 `gen-tree.sh` → TREE GREEN → unblock; the gate runs per push.
5. G11 – G15 gates.
6. T022: `run-probe.sh` → PROBE GREEN → unblock.
7. G17: T025 `gen-tree.sh` → unblock; T026 `run-library-lanes.sh` detached → LIBRARY LANES GREEN → unblock; the gate.
8. G18 – G20 gates.
9. T031 `suites.sh` detached + T034 `tidy.sh` concurrently (+ T032 `portability.sh` if deferred, T033 `fastmath-repro.sh`); then
   T035 CPU lane alone after the machine has idled.
10. `node tools/clean-test-output.js --apply` (memory rule: clean up test output at every close).
11. Comply quotes the run-log files. The macOS / Linux CI legs are **PENDING-CI** until the user pushes.

## Open questions carried from the plan (built at the plan's reading; surface, do not decide)

- Plan §11 Q1–Q4 (R1–R4) and the R5 / R6 amendments: T001 blocks until ruled.
- Deviation 2 (sharded harvest / comparison names), Deviation 3 (early registration), Deviation 4 (columns with the conversions,
  source-keyed probe reader), Deviation 5 (manual and uidesc with registration), Deviation 6 (SC-007 authored in G17),
  Deviation 7 (count sites the plan missed).
- SC-007 per-push cost: 58 presets × 2 notes × up to 20 renders; the shard count (32) is an estimate to be confirmed by the G17
  gate's wall clocks (target ≤ 9 s per shard).

## Defects found

(Empty at task-cut time. Gates, fixers and the main loop record entries here: task, case / section, verbatim reading, and triage.)

- **T006, G5 gate a1, `ProfundumMorphModulator_Envelope` (b) and (b′), all three rates.** Reading:
  `profundum_morph_modulator_test.cpp:537 REQUIRE( std::abs(peakAt - nominal) <= 1 ) with expansion: 2 <= 1` (attackMs 0.1,
  nominal 0) and `:551 REQUIRE( m.advanceInterval() == 1.0f ) with expansion: 1.0f == 1.0f` (the first interval printed as 1.0f
  but read 0.99999988). Root cause: the plan §5.2 probe premise "A = 0.1 ms: first Attack step 1.0" is false for the real
  float32 substrate. At the rate clamp (`envelope_utils.h:96-97`) the single step is `base = 1.3f * (1 - coef)` with
  `coef = 0.23076926f`, which rounds to 0.99999988, one ulp below the peak, so the clamp to 1.0 and the entry to Decay
  (`adsr_envelope.h:302-306`) land in the second interval (the probe had used double `exp`). Fix (triage: T006, fixed in G5):
  `ProfundumMorphModulator::advanceInterval` runs the envelope's clamp step in the same interval when the applied attack is
  within the envelope's own one-step rate clamp (`A · 0.001 · fc <= 1`, the same float expression) and the stage is still
  Attack after the first step, so the restart interval reads 1.0 exactly by branch on every toolchain. `adsr_envelope.h` is
  untouched. Consequence to note against SC-002 (a): for A <= one interval and Sustain < 1, the modulator's trace leads a
  reference `ADSREnvelope` by one interval after the peak. No tested (a) configuration hits this (its 0.1 ms row has Sustain 1),
  and (b) "peak in the first interval" and a one-interval-late reference cannot both hold there. Gate a2: GREEN,
  dsp_systems_tests 419 cases / profundum_tests 107 cases, wall 135.7 s.
- **G7 gate a1, `Profundum_PresetRenderGuard_<k>of8` (all 8), `preset_rt_test.cpp:408 CHECK( seconds <= kRtGuardSecondsBar )`.**
  Reading: 17.4-20.8 s (filtered) and 18.6-23.1 s (full) against the 15 s bar. G7 changed no production code (T008 is
  tests only; `profundum_engine.h` last written 21:03, before the green G6 gate, which read 11.1-14.3 s). The same build
  with the 10 `[preset][rt]` cases alone on 10 workers read 10.1-11.5 s. Root cause: the case's wall-clock bar already
  stood at 67-77 % of the bar on an idle box, above the root CLAUDE.md 60 % rule, so the 30-worker gate pool tips it red.
  Fix (triage: Phase 13 R-11 split, fixed in G7): split further, `_0of16` .. `_15of16` (3-4 presets each); the bar is
  unchanged. Gate a2: every shard green, 8.4-12.6 s (filtered) and 9.1-13.6 s (full). The full run's one red,
  `SeraphisEngine_VoiceStealIsClickless` `seraphis_engine_test.cpp:4265 REQUIRE( bestWorstBlockUs <= kBlockBudgetUs )`
  (13412.8 <= 10666.7 us), is a wall-clock block budget in untouched Seraphis code (green in a1-full): recorded as load,
  not re-run. Gate-command defect, not a test: `profundum_preset_generator` is not a Catch2 executable. Passed to
  run-gate it reads the filter as its output directory, writes 58 presets into `<repo>/[profundum]~[performance]~...`
  once per shard (120 concurrent writers), and reports "no Catch2 summary". Build it as a target; never list it as a
  run-gate executable. The stray directory from a1 was deleted.
- **G9 gate a1, `Profundum_PresetRenderGuard_11of16` / `_14of16`, `preset_rt_test.cpp:410 CHECK( seconds <= kRtGuardSecondsBar )`.**
  Reading: 15.83 s and 17.06 s against the 15 s bar, both in pass-1 shards (`profundum_tests-96.log`, `-99.log`, filter
  `... ~[wallclock]`). Root cause: a stale binary, not a code defect. The gate ran `profundum_tests.exe` built at 21:58,
  before the `[wallclock]` tag landed on the render guards in `preset_rt_test.cpp` (22:49). The tag only takes effect
  after a rebuild, so the untagged cases ran in the 30-worker busy pass, and its quiet pass matched nothing ("No test cases
  matched"). Fix (triage: G9, no code change): rebuild (0 warnings), then `lint-wallclock-tags: clean`, and `--list-tests`
  shows 17 `[profundum][wallclock]` cases. Gate a2: GREEN, dsp_systems_tests 431 cases / profundum_tests 115 cases, wall
  199.3 s. The quiet pass ran 17 cases in 23.9 s, and the render guards read 1.86-3.14 s. `header-consumers.js --tag
  '[profundum]'`: "CONSUMERS OUTSIDE [profundum]: none", so no other suite was run.
- **G15 gate a1, `Profundum_MainPageAudibility` SECTION 1003, `main_page_audibility_test.cpp:87 CHECK( value >= bar )`, E1 and C3.**
  Reading (`f:/tmp/gate/profundum-phase14-morph-modulation-G15_Main_page_rows-a1/profundum_tests-65.log`): `1003 E1: lo 13.0709,
  hi 14.3522, |diff| 1.28132, bar 3` and `1003 C3: lo 12.6217, hi 13.8655, |diff| 1.24381, bar 3`; P-6 line `mean smoothedMass lo
  100 hi 50; mean position lo 0 hi 1` at both notes. The frame preconditions and SC-023 (c) pass (8192 grid points, worst
  |p_twin - p_proc| 0), and row 1004 clears the bar (`lo 0 hi 14.3522` E1, `lo 0 hi 13.8655` C3). Root cause: not a code defect.
  The test arms match FR-031's pin (`main_page_audibility_test.cpp:328-353`), and the processor hands the wheel over as C-7
  rules (`processor.cpp:944-947`, `:957-959`). The P-6 confound the spec records is large enough to cancel the row: in the lo arm
  the wheel pushes A_s from Mass 50 to Mass 100, which on its own lifts D_nl from 0 dB (row 1004's lo arm, same A_s at Mass 50)
  to 13.07 dB, within 1.3 dB of B at Mass 50 (14.35 dB). D_nl cannot tell "A_s, Mass 100" from "B, Mass 50", so the pinned
  Off -> Mod Wheel contrast cannot clear 3 dB with any correct implementation. Triage: **needs a user ruling on FR-031's 1003
  pin** (the test is not weakened; it matches the spec). Options to put to the user: (i) keep Off -> Mod Wheel but hold the
  endpoints' Mass where the C-7 push is a no-op or read a descriptor that separates Mass from Morph; (ii) re-pin the 1003 lever
  to a source with no Mass hand-over, e.g. lo = Off, hi = Velocity, Amount 1, every note at velocity 127, wheel 0 (no confound,
  and row 1004 already shows that contrast reads 13.9-14.4 dB); (iii) keep the pin and record the 1003 row as reported, not
  gated. No code or test was changed; G15 stays red until the ruling.
  **Ruling R7 (user, 2026-10-27): option (ii).** FR-031, plan §5.6 / P-6 and T020 re-pinned to Off → Velocity (Amount 1, velocity
  127, wheel 0); the main loop changed the 1003 SECTION of `main_page_audibility_test.cpp` to match. Bar unchanged.
- **T025 a1, `Profundum_GeneratorSmoke` `preset_library_test.cpp:372 CHECK( written == 58u )`, 61 == 58.** Root cause: a tasks gap.
  Rule (ii) added three presets (definitions 59-61: Tidal Fold, Struck Fold, Wheel Fold), but no task moved the library-size
  sites or the manual. Fix (main loop, no bar changed): `gen-tree.sh` EXPECTED 58 -> 61; `preset_library_test.cpp:372`,
  `preset_browser_test.cpp:737`, `:770` and `manual_coverage_test.cpp:122` 58 -> 61; `MANUAL.md` Factory Library synced from
  `profundum_preset_defs.h` (intro 58 -> 61, the five converted presets' comments rewritten to their new descriptions, the three
  new rows added under Animated and Driven). Still stale, outside the phase fence: the root `CMakeLists.txt:814` banner "53
  presets across 6 categories" (stale since Phase 13b).
- **T026 a1, LIBRARY LANES RED (8 lanes).** Coverage matrix GREEN (M-ENV min margin 2.59 dB, M-CYCLE 42.83, M-VEL 41.76, M-WHEEL
  42.83). Reds: `Profundum_PresetMorphEndpoints_*` `preset_sweep_test.cpp:819` distance with the modulation moving A toward B
  (Long Tail 0.76, Poly Spread 1.81, Poly Swell 1.36, Poly Ignition 1.22, Tidal Fold 1.18, Struck Fold 7.72, bar 10) and `:823`
  Rectified Floor C3 level delta 4.50 dB (bar 3); `Profundum_PresetLoudnessMatch` `:717` Long Tail 4.93 dB from the median (bar 4);
  `Profundum_PresetDistinctness` `:763` Fed Fold vs Wheel Fold d 0 (bar 2). Root causes: (1) the endpoint check measured a
  modulated A; (2) the three new presets copied Fed Fold's endpoints. Replacing carriers (plan §10) cannot fix (1): every conversion
  moves A by design. **Rulings R8 and R9 (user, 2026-10-27)**, spec FR-043 / FR-046 first: R8 renders a modulated preset's A as a
  both-endpoints-A preset in `swRunMorphEndpointShard` (`preset_sweep_test.cpp`, main loop); R9 gives Tidal Fold, Struck Fold and
  Wheel Fold their own endpoints and names (`profundum_preset_defs.h`, main loop; MANUAL rows re-synced). Long Tail's loudness
  red is re-read after R8; if it stays red its conversion is replaced per plan §10. No bar changed.
- **T026 a3 (after R8, R9).** Green: every converted preset's endpoints, distinctness, coverage (M-CYCLE 4.23, M-VEL 5.28,
  M-WHEEL 1.92 dB margin). Red: the added presets' B 4.7-9.1 dB below A (`:828`, bar 3); Struck Fold 7.22 dB below the loudness
  median; Long Tail 5.37 dB above it; Drifting Weight 4.0013 (the median moved to -11.57). Fixes (main loop): (1) plan §10
  replacement made checkable: `evidence/gate-exclusions.csv` read by `select.js` and by `mmSelect`
  (`preset_library_test.cpp`); Long Tail measured on each column with the loudness lane alone (three exe variants,
  `run-log/T026-longtail-loudness-{cycle,vel,wheel}.log`: 5.37, 4.95, 2.55 dB), so it carries M-WHEEL; select.js now reads
  "5 conversions, 2 new" and Wheel Fold (definition 61) is removed; the library is 60 (count sites, `gen-tree.sh` EXPECTED and
  MANUAL moved to 60). (2) Level: Tidal Fold A sustain 0.8 -> 0.55, B 0.8 -> 1; Struck Fold A sustain 0.6 -> 0.8, B sustain
  0.6 -> 1, Mass 55 -> 70, Anchor level 0.8 -> 1.
- **T026 a4.** Red: Tidal Fold C3 level delta 3.35, Struck Fold C3 3.23 (bar 3); mono sum Tidal Fold rho 0.98943 (bar 0.99);
  Drifting Weight 4.0117 from the median -11.5771 (bar 4; its fields are unchanged, the added presets sat below the median).
  Tuned with a build plus the four affected lanes only (`f:/tmp/p14-quick/v1`, `v2`): Tidal Fold A sustain 0.45, B Mass 80,
  Anchor level 1, image SPREAD; Struck Fold A sustain 0.65 (B Mass 100 read worse at C3 and went back to 70). v2: Tidal Fold
  deltas 2.73 / 0.97 dB, mono rho 0.99980, loudness +0.27 dB; Struck Fold 0.89 / 1.43 dB, -1.35 dB; Drifting Weight 3.92 (the
  median moved to -11.4868).
- **T026 a5: LIBRARY LANES GREEN** (25 lanes, 517.6 s; coverage M-ENV 2.59, M-CYCLE 6.66, M-VEL 6.30, M-WHEEL 7.70 dB margin;
  per-push 69 cases GREEN, 125.5 s). DEFECT line: the 32 transparency shards read 26.9-93.4 s under the 30-worker gate
  (target 9 s); shard 2 alone 29.7 s (two presets). Per the T024 rule, re-sharded to 64 over (definition, note) units (64
  per-definition shards would leave 4 of 60 empty and `CHECK(presets > 0u)` red); about 15 s each alone, still above the 9 s
  estimate, with the same renders. `run-library-lanes.sh` non-vacuity names moved to `_0of64` / `_63of64`.
- **Compliance remediation (2026-10-27), SC-011 shard wall clock.** Reading, the four `ProfundumEngine_MorphModClickFree_<k>of4`
  run alone one after another on an idle box (`f:/tmp/p14-sc011-alone.log`): 11.06 / 13.57 / 11.08 / 13.89 s against the 9 s
  T009 target (60 % of 15 s); every functional bar green (worst step / static 1.1231, bar 2). Fix (T009 rule, nothing
  shortened): re-sharded into 8, `_1of8` .. `_8of8` (index % 8, 9 arms each), `suites.sh` case list moved. Alone readings
  (`f:/tmp/p14-sc011-alone-of8.log`): 6.56 / 7.93 / 6.88 / 8.40 / 6.05 / 7.38 / 6.02 / 7.84 s, all ≤ 9 s; worst step / static
  1.1231 (bar 2), all 8 cases "All tests passed".
- **Compliance remediation (2026-10-27), CC-fastmath deviation (2): the one-interval Attack rule recomputed the envelope's rate
  clamp.** `isOneIntervalAttack` evaluated `attackMs * 0.001f * controlRate_ <= 1.0f` beside the envelope's own copy
  (`envelope_utils.h:96-97`); two compiled copies can disagree by an ulp under -ffast-math. A sweep over ±64 ulps of attackMs
  around one interval (new `ProfundumMorphModulator_Envelope` section "(b) Attack at the one-interval boundary", 3 rates) was
  RED on the old rule on MSVC too: at attack = exactly one interval (0.66667 ms at 48 kHz, 0.72562 at 44.1, 0.33333 at 96) the
  rate computes a hair above 1, the first step lands a few ulps below the peak, and the peak arrived one interval late
  (`profundum_morph_modulator_test.cpp:566 REQUIRE( s1 == 1.0f )`, 3 failures). Fix: the second step in the restart interval
  is decided by the envelope's own first step from 0 (`eRaw >= kOneStepPeakFloor`, 8 ulps below 1, restart interval only), no
  recomputation. Green: 87-89 peaked / 40-42 stepped per rate.
- **Compliance remediation (2026-10-27), CC-fastmath deviation (1): NaN-injection asserts in fast-math TUs.** The NaN arms of
  `ProfundumMorphModulator_ApiAndRt` (wheel, setConfig amount / decay, setTempoBpm) and `ProfundumEngine_MorphModComposition`
  (e) (NaN wheel event, NaN `setMorphModWheel`) moved to `profundum_morph_mod_nonfinite_test.cpp` in `dsp_ieee_tests` (and the
  -fno-fast-math per-source block), cases `ProfundumMorphModulator_NonFinite` and `ProfundumEngine_MorphModNonFinite`; the
  finite arms stay. Gate (`f:/tmp/p14-remediate-gate.log`): GATE GREEN dsp_systems_tests 431 / dsp_ieee_tests 122 /
  profundum_tests 125 cases, 0 failed; after the SC-011 re-shard `f:/tmp/p14-remediate-gate2.log`: GATE GREEN
  dsp_systems_tests 435 cases, 0 failed.
