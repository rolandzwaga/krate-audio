# Tasks: Profundum Phase 11 — Full Parameter Surface & State

**Spec:** `specs/profundum-phase11-parameters/spec.md` (Clarifications Q1–Q8, 2026-10-22) · **Plan:**
`specs/profundum-phase11-parameters/plan.md` (§0–§10, Open Questions 1–2, §10 amendments) · **Roadmap:**
`specs/Profundum-roadmap.md` Phase 11 (lines 794–809) · **Phase base:** `565f195d` on `synth/profundum`
**Status:** Task list (2026-10-22). The build stage has not started. No commit tasks: commits happen outside this workflow.

**Deliverables.** 76 new parameters (84 registered), state v2 (510 bytes), the Edit A / Edit B morph editing model, Copy and
Compare, `IMidiMapping` with the Mass performance offset and the sustain latch, `getTailSamples()`, the processor-authoritative
controller mirror, 12 new test TUs in `profundum_tests`, one new TU in `shared_tests`, the promoted
`Krate::Plugins::SustainLatch`, and the doc edits (FR-072, FR-023). **No file under `dsp/` is modified (FR-070). No new DSP.**
All paths are repo-relative to `f:/projects/iterum`.

---

## Read this first (every executor)

### Canonical order inside every task

1. Write the failing test **first**, in the named file and `TEST_CASE`, with the exact assertions given. A section that guards
   behaviour an earlier task already built says so and names its **non-vacuity arm** (the control that proves it can fail).
2. Implement the minimum that makes it pass, exactly as the cited plan section pins it.
3. Zero warnings (`warning C[0-9]|warning:` count 0). "Pre-existing" is not a category.
4. Tests pass at the group gate.

**Executors do not build or run tests**; the group gate does, after every group. The gate builds the group's targets and runs
`<exe> '[profundum]~[performance]~[perf]~[benchmark]~[!benchmark]~[long]~[.]'` (`.claude/workflows/profundum-phase.js:551–552`).
**Every TEST_CASE of this phase carries `[profundum]`**, including the one in `shared_tests` (T002), so the gate's tag filter
reaches it. `[long]` cases and the hidden `[.perf]` case are written by executors and **run by the main loop** (T031, T033).

Commands for anyone who does run them (Windows, full CMake path; capture to a file once, never re-run a slow suite for output):

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target Profundum profundum_tests shared_tests -- /nr:false 2>&1 | tee F:/tmp/p11-build.log | tail -5
grep -cE "warning C[0-9]|warning:" F:/tmp/p11-build.log          # must print 0 (SC-001)
build/windows-x64-release/bin/Release/profundum_tests.exe "[profundum]~[long]~[.]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/profundum_tests.exe "Profundum_StateVersions" 2>&1 | tail -5
```

Catch2 filters are positional; tags go in brackets. Never `ctest -R profundum_tests`. The post-build copy to
`C:/Program Files/Common Files/VST3/` may fail with a permission error; compilation still succeeded.

### Parallelism

`[P]` tasks inside one group create **only brand-new files**, disjoint from every other task in that group. Every task that edits
a file that already exists — `processor.h/.cpp`, `controller.h/.cpp`, `plugin_ids.h`, `profundum_test_fixture.h`, a test TU created
by an earlier group (including the G2 placeholders), any CMake list, any file outside `plugins/profundum/` — is **alone in its
group**. All Profundum test TUs link into the one `profundum_tests` binary.

### Rules for every file of this phase (plan §4, §6 conventions, Phase 10 rules)

- **No `dsp/` edit (FR-070).** If a task appears to need one, STOP AND SURFACE (return `blocked` with the reading and the need).
  Including `dsp/tests/unit/systems/*_test_helpers.h` read-only is allowed (plan §6.4), and only from the two TUs named there
  (`param_path_audibility_test.cpp`, `mono_sum_params_test.cpp`; Risk R7).
- Never `std::isnan` / `std::isinf` / `std::isfinite`. Use `Krate::DSP::detail::isFinite` (`dsp/include/krate/dsp/core/db_utils.h:118`
  float, `:125` double). Non-finite test values are built from bit patterns through a `volatile`.
- No brace-init narrowing. Enum values from indices go through an explicit `static_cast` after a range check.
- Plugin types are spelled `::Profundum::…`; the test namespace is `ProfundumPluginTest`; never open `Krate::DSP::ProfundumTest`
  or `Krate::DSP::StereoFieldTest` with `using namespace` (qualify).
- Parameter titles, units and list strings are ASCII (Phase 10 rule).
- Every `Processor`, `Controller` and `ProfundumEngine` in a test lives on the heap.
- **No bit-exact float golden across binaries.** Every equality is same-binary, same-process (two renders, or a value against a
  constant computed in the same process). `memcmp` of two `getState` streams of one process is allowed.
- **Exact zero** means `x == 0.0f` (admits `-0.0f`).
- Display buffers are `char8 text[64]` (`-Wformat-truncation` margin).
- **ODR.** The new type names are those of plan §2 only (`SoundParamRow`, `EndpointParams`, `NoteParams`, `MorphControlParams`,
  `PerformanceParams`, `CompareMode`, `ParamRoute`, `RouteKind`, `DisplayUnit`, `PlainLaw`, `LeafBits`, `BandLevelReading`, the
  nested `Processor::PedalPoint`, and the promoted `Krate::Plugins::SustainLatch`). Any other new type name: run
  `grep -rnE "(class|struct|enum class|enum|using) <Name>\b" dsp/ plugins/ tests/` first and record the 0-hit result in the
  run-log. Never `AmpEnvParams` (`plugins/ruinae/src/parameters/env_params.h:85`), never `MorphParams` (Seraphis), never a roadmap
  hazard name (`CharacterProcessor`, `SubOscillator`, `StereoField`, `MorphEngine`, `MorphNode`, `TransientDetector`, …).
- **Real-time safety (FR-062).** Nothing reachable from `process()` allocates, locks, throws or does I/O. Only
  `flushControllerSync()` allocates (`IMessage`s) and it is never called from `process()` (plan §4.7 table).

### Precondition table (plan items the tasks build at the plan's reading)

| Item | What the tasks build | If ruled otherwise | Task(s) |
|---|---|---|---|
| **Plan OQ-2 / §10 FR-025 (BLOCKING)** | T001 checks that the FR-025 row of `spec.md` carries the user's Linux ruling. The tasks build **option 1** (50 ms SDK timer on Windows / macOS; on Linux the next `notify` / `setState` / `getState` / `setActive` flushes) | Option 2 needs a new transport: T020 is re-cut before the build continues | T001, T020 |
| Plan §10 FR-041 | `getState` writes a loaded-but-unapplied blob first (precedence 1, plan §4.5.2); SC-012 arm (3) pins it | Precedence 1 is removed; arm (3)'s pre-block clause becomes "equals the synthesized blob" | T001, T018 |
| Plan §10 FR-006 | Hz below 1 Hz prints `"%.2f Hz"` | The format changes in `formatSoundRow` and `Profundum_ParamDisplayFormats` only | T001, T014 |
| Plan OQ-1 (`p_a` of the synthesized blob) | `p_a = position` (the spec's text, the plan default) | `p_a = BassMorphEngine::curve(mode, position)` (`bass_morph_engine.h:188`) in T017's synthesizer and SC-012 arm (5) | T017 |
| Plan R3 (SC-017 exact zero) | The main loop runs the SC-017 render case first among the `[long]` cases. A non-zero sample is reported to the user; the 1250 ms term is never re-fitted | — | T022, T031 |
| Plan R2 (log-row default bitwise) | Registration computes defaults from `kDefaultLeafBits`; a failing row gets the bounded `nextafter` nudge (≤ 8 steps) | — | T008, T014 |

### Deviations from the plan made while cutting these tasks (surface them; do not silently revert)

1. **Test-TU registration comes early (G3, T005); the last group's single CMake task (T029) lists the 12 new plugin headers and
   audits every list.** CMake refuses a source list that names a missing file, and a test cannot fail before its TU is compiled.
   G2 creates every new TU (placeholders with no `TEST_CASE`, so nothing is vacuously green), G3 registers them once. The plugin
   CMake header list (`plugins/profundum/CMakeLists.txt:30–48`) is IDE-only, so it waits until every header exists. Phase 10
   deviation 1 is the precedent.
2. **SC-017 is split into two cases:** `Profundum_TailFormula` `[profundum][tail]` (formula, 0-before-prepare and 0-after-terminate;
   milliseconds, gated at every group) and `Profundum_TailCoversRelease` `[profundum][tail][long]` (the 8 render arms). Plan §6.2
   put all of SC-017 in one `[long]` case, which the gate filter never runs.
3. **One extra case, `Profundum_SoundParamTableRouting` `[profundum][params]`** (T013), proves the slot-level routing of all 60
   rows and the Sync Rate pair writes before any processor code exists. No SC is moved; SC-008 still runs end to end in T015.
4. **The pack headers (T009–T012) are proven by `static_assert`s at compile time** (FR-002 requires them) and by T013's runtime
   case in the next group: a `[P]` task cannot edit an existing test TU.
5. **SC-024's sections are written across tasks** as the paths they need appear: arithmetic in T015; never-written / mirror / no
   message / reset-on-load in T020; the audibility arms in T023 (`Profundum_MassOffsetAudibility`, plan §6.2).
6. **The Vorago alias (T006) is gated by name** (`vorago_tests.exe "Vorago_SustainLatchUnit,Vorago_SustainLatch"`, seconds). Ruled 2026-10-22 (pure code move): the close lane runs the
   same two cases, never the full `vorago_tests` suite.

### Verified API surface these tasks rely on (read this session at `565f195d`)

| Symbol | File:line |
|---|---|
| `kMorphStateFormatVersion = 1`, `kMorphLeafFieldCount = 62`, `kMorphStateBytes = 201` | `dsp/include/krate/dsp/systems/morph_state.h:131, :133, :135` |
| `template <std::size_t I> using Leaf = …`; `forEachMorphLeaf(F&&, S&...)` (in `detail::morph_state_io`) | `morph_state.h:330–334` |
| `leafBitsEqual`, `isValidMorphState`, `serializeMorphState(s, dest, cap)`, `deserializeMorphState(src, size, out)` | `morph_state.h:484, :513, :541, :574` |
| `BassMorphEngine::Endpoint {A, B}`, `AnchorMorphMode {Lock, Blend, Punch}` | `dsp/include/krate/dsp/systems/bass_morph_engine.h:48, :49` |
| `kMinPositionRampMs 5`, `kMaxPositionRampMs 2000`, `kDefaultPositionRampMs 40`, `kThetaUp 0.52`, `kThetaDown 0.48`, `kDefaultAnchorMorphMode Blend`, `kLawTable`, `kMorphRuntimeFormatVersion 1`, `kMorphRuntimeBytes 20` | `bass_morph_engine.h:67–76` |
| `setEndpoint`, `setPosition`, `setPositionRampMs`, `setAnchorMorphMode`, `curve(m, p)`, `serializeRuntime`, `restoreRuntime` | `bass_morph_engine.h:113, :140, :155, :168, :188, :235, :274` |
| `ProfundumEngine::kLimiterLookaheadSamples 64`, `kDefaultBendRangeSt 2`, `kMaxBendRangeSt 24`, `kDefaultSeed 0xB455` | `dsp/include/krate/dsp/systems/profundum_engine.h:107, :112–114` |
| `setObserver` (configuration-time), `setPitchBend` | `profundum_engine.h:252, :292` |
| Morph surface `setEndpoint … restoreMorphRuntime` | `profundum_engine.h:310–319` |
| Non-morphed setters `setMonoPriority … setSeed` (undeclared enum ignored, non-finite float kept) | `profundum_engine.h:331–373` |
| Read-backs `pitchBend()` :379, `getEndpoint(e)` :381, `morphPosition()`, `morphRampMs()`, `anchorMorphMode()`, `monoPriority()` … `pitchBendRange()` :396 | `profundum_engine.h:379–396` |
| Phase 10 processor: `initialize` :61, `terminate` :79, `setupProcessing` :111 (prepare :132, push :138), `setActive` :145 (reset :149), `process` :155 (push :183), `setState` :278, `getState` :296, `processParameterChanges` :312, `pushParamsToEngine` :347, `dispatchEvent` :393 | `plugins/profundum/src/processor/processor.cpp` |
| `getTailSamples` not overridden :58; `engineForTest()` const :61; `sizeof(Processor) < 64 KiB` :91 | `plugins/profundum/src/processor/processor.h` |
| "FR-019: NO IMidiMapping" :10; `setComponentState` :32; `getParamStringByValue` :33 | `plugins/profundum/src/controller/controller.h` |
| `kCurrentStateVersion = 1` :20, `kStateV1Bytes = 36` :23; band comment :44–49 | `plugins/profundum/src/plugin_ids.h` |
| `MultiPointParamValueQueue` :76, `MultiParamChanges` :141, `CountingConnectionPoint` :200, `ProcessorFixture` :226 (`prepare` / `processBlock`), `DirectEngine` :444 | `plugins/profundum/tests/profundum_test_fixture.h` |
| Phase 10 cases: `Profundum_ParamDenormRoundTrip` (param_denorm_test.cpp:116), `Profundum_StateRoundTrip` (state_roundtrip_test.cpp:230), `Profundum_ProcessorLifecycle` (lifecycle_test.cpp:346), `Profundum_ParamFlowReachesEngine` (param_flow_test.cpp:155), `Profundum_ProcessorCpu` (processor_cpu_test.cpp:206) | as cited |
| Phase 10 lines that v2 / `IMidiMapping` make wrong: lifecycle_test.cpp:238, :247, :491, :501, :523; state_roundtrip_test.cpp:93, :112, :233, :426–433; param_denorm_test.cpp:233; param_flow_test.cpp:385 | read this session (plan §6.5) |
| `Vorago::SustainLatch` (81-line header; class at :22); `Vorago_SustainLatchUnit` SECTIONs :57–135; `Vorago_SustainLatch` :343 | `plugins/vorago/src/processor/sustain_latch.h`, `plugins/vorago/tests/integration/sustain_test.cpp:51` |
| `StereoFieldTest::bandReading(L, R, n, fs, fLo, fHi)` :736; `kSubBandQBarDb = 1.0` :95; `kSubBandRhoBar = 0.99` :97 | `dsp/tests/unit/systems/bass_stereo_field_test_helpers.h` |
| `voiceEngineAs` :105, `voiceEngineBs` :113 | `dsp/tests/unit/systems/profundum_voice_engine_test_helpers.h` |
| Shared lists: `src/midi/midi_event_dispatcher.h` at `plugins/shared/CMakeLists.txt:35`; `shared_tests` list `plugins/shared/tests/CMakeLists.txt:7–38` | as cited |
| `base/source/timer.cpp` is compiled into the SDK base library | `extern/vst3sdk/cmake/modules/SMTG_VST3_SDK.cmake:39` |
| Phase 9 audibility CSV header `shard,row,contrast,base,note,descriptor,tap,lo,hi,value,bar,pass,gated,out_descriptor,…` | `specs/profundum-phase9-voice-engine/evidence/audibility-table.csv:1` |
| Phase 10 CPU harness constants (`kFs 48000`, `kBlock 512`, `kMonoPctBar 8.0`, `kPolyPctBar 25.0`, `kWrapperOverheadCeiling 1.05`) | `plugins/profundum/tests/integration/processor_cpu_test.cpp:56–65` |

---

## Group and task map

| Group | Tasks | Theme |
|---|---|---|
| G1 | T001 | Preflight: branch, base, ruling check (BLOCKING), ODR sweep |
| G2 | T002 [P], T003 [P], T004 [P] | New files: shared `SustainLatch` + its test; band-level metric + its test; 11 placeholder TUs |
| G3 | T005 | Test CMake registration (profundum tests, shared tests, shared header list) |
| G4 | T006 | Vorago `SustainLatch` alias |
| G5 | T007 | `plugin_ids.h`: 76 IDs, v2 layout constants, message IDs, band comment |
| G6 | T008 | `sound_param_row.h` + SC-006 leaf-law sections |
| G7 | T009 [P], T010 [P], T011 [P], T012 [P] | Section packs (new headers) |
| G8 | T013 | `sound_param_table.h` + `Profundum_SoundParamTableRouting` |
| G9 | T014 | Controller registration and display (SC-005, FR-006 strings, SC-016, SC-006 parameter arms) |
| G10 | T015 | Processor routing, endpoint slots, assembly, Mass offset, push (SC-007, SC-008, SC-009 1–3, SC-024 arithmetic) |
| G11 | T016 | Controller mirror, Edit Target re-display, endpoint `notify` (SC-009 4–6) |
| G12 | T017 | State v2 format, seed, v1 reset, runtime-blob publication; Phase 10 test amendments (SC-012 arms 1, 2, 5) |
| G13 | T018 | Restore hand-off seqlock (SC-010 processor, SC-012 arm 2 twin clause and arm 3, `RestoreHandOffNoTear`) |
| G14 | T019 | Controller `setComponentState` v2 (SC-010 controller, SC-012 arm 1 controller part and arm 4) |
| G15 | T020 | Copy, Compare, sync sender, timer (SC-013, SC-014, SC-025, SC-024 sync sections) |
| G16 | T021 | `IMidiMapping`, pedal, latch, pitch bend (SC-015) |
| G17 | T022 | `getTailSamples` (SC-017: formula case + `[long]` render case) |
| G18 | T023 | SC-018 descriptor port + `Profundum_ParamPathAudibility` + `Profundum_MassOffsetAudibility` (`[long]`) |
| G19 | T024 | SC-019 `Profundum_MonoSumViaParams` (`[long]`) |
| G20 | T025 | SC-011 `Profundum_StateRestoreRender` (`[long]`) + SC-022 `Profundum_Determinism` |
| G21 | T026 | SC-021 `Profundum_Phase11PathsDoNotAllocate` |
| G22 | T027 | SC-020 `Profundum_ProcessorCpuFullSurface` (written here; run only by T033) |
| G23 | T028 | Docs: roadmap band map (FR-072 / SC-026), plugin `CLAUDE.md`, `README.md` (FR-023), `CHANGELOG.md` |
| G24 | T029 → T033 | Integration: CMake registration (single task); scope fence + greps; suites lane (main loop); portability; CPU lane alone (main loop) |

**Run-log convention.** Every task writes the commands it ran (or the checks it performed) and their verbatim last lines to
`specs/profundum-phase11-parameters/evidence/run-log/T0NN.txt`. Compliance quotes those files, never memory.

---

# GROUP 1 — Preflight

## T001 — Branch, base, ruling check, ODR sweep (no code) — STOP AND SURFACE on a missing ruling

**Files created:** `specs/profundum-phase11-parameters/evidence/run-log/T001.txt`.

1. `git branch --show-current` prints `synth/profundum`; `git merge-base --is-ancestor 565f195d HEAD` exits 0;
   `git status --porcelain` lists only ` M .serena/project.yml` and paths under `specs/profundum-phase11-parameters/`. Anything
   else: STOP and return `blocked`.
2. **Ruling check (plan OQ-2 and §10; BLOCKING).** In `specs/profundum-phase11-parameters/spec.md`:
   - the **FR-025** row must carry the user's Linux sync ruling (ruled 2026-10-22: the "pull on Edit Target switch" bullet plus
     the sentence "On Linux, where the SDK provides no timer, an edit the controller does not observe reaches the mirror at the
     next `notify`, `setState`, `getState` or `setActive` call"; the pull is built in T016 / T020, it is not option 2);
   - the **FR-041** row must carry the pending-loaded-blob sentence ("A runtime blob loaded by `setState` and not yet applied is
     written as loaded") or a ruling rejecting it;
   - the **FR-006** row must state the Hz rule ("two decimal places below 1 Hz") or a ruling keeping the original wording;
   - plan OQ-1 (`p_a`) is ruled in the FR-041 row (ruled 2026-10-22: `p_a = BassMorphEngine::curve(mode, position)`); T017
     builds that.

   If any of the first three is missing, return status **`blocked`** with notes naming each missing row and quoting plan §10's
   proposed text. Do not edit `spec.md`. If FR-025 carries option 2, return `blocked` with "T020 must be re-cut for the ruled
   transport".
3. **ODR sweep** (plan §2). This prints 0 lines:
   `grep -rnE "(class|struct|enum class|enum|using) (SoundParamRow|EndpointParams|NoteParams|MorphControlParams|PerformanceParams|CompareMode|ParamRoute|RouteKind|DisplayUnit|PlainLaw|LeafBits|BandLevelReading)\b" dsp/ plugins/ tests/`.
   `grep -rnE "class SustainLatch\b" dsp/ plugins/ tests/` prints exactly one line, `plugins/vorago/src/processor/sustain_latch.h:22`.
4. **Scope baseline.** `git diff --name-only 565f195d -- dsp/` prints nothing.
5. Re-read `profundum_engine.h:310–396` and `bass_morph_engine.h:48–76` and confirm every row of "Verified API surface" that cites
   them. A mismatch is a `blocked` report naming the row.

**Verify.** `T001.txt` holds every command and its output. No source file changes.

---

# GROUP 2 — New files (all [P]; every file is brand new and owned by exactly one task)

## T002 [P] — Promoted `Krate::Plugins::SustainLatch` + `SustainLatch_SharedSemantics` (FR-061)

**Files created:** `plugins/shared/src/midi/sustain_latch.h`, `plugins/shared/tests/test_sustain_latch.cpp`.

1. **Test first** — `plugins/shared/tests/test_sustain_latch.cpp`, `TEST_CASE("SustainLatch_SharedSemantics",
   "[profundum][shared][midi]")` (the `[profundum]` tag puts it under the phase gate). Port the seven SECTIONs of
   `Vorago_SustainLatchUnit` (`plugins/vorago/tests/integration/sustain_test.cpp:57–135`: `PedalUpNoteOffIsImmediate`,
   `PedalDownLatchesThenReleasesOnPedalUp`, `ReStrikeOfLatchedNoteIsHeld`, `ReleaseAllReleasesEveryLatchedNote`,
   `ClearWithoutReleaseForgetsEverything`, `NoteNumbersAbove127AreIgnored`, `RandomOperationsAreAllocationFree`) against
   `Krate::Plugins::SustainLatch`, with the same assertions and numbers (read them; do not paraphrase) and the same `ReleaseLog`
   helper shape (copy it into an anonymous namespace in this TU). Include `"midi/sustain_latch.h"`. The allocation SECTION uses
   `TestHelpers::AllocationScope` (`tests/test_helpers/allocation_detector.h`); this TU **must not** include
   `<allocation_operator_overrides.h>` (owned by `test_allocation_detector.cpp`, per the comment in
   `plugins/shared/tests/CMakeLists.txt`).
2. **Implement** — `plugins/shared/src/midi/sustain_latch.h`: a **verbatim** copy of the includes and class body of
   `plugins/vorago/src/processor/sustain_latch.h` (lines 16–79), namespace changed to `Krate::Plugins`, banner rewritten to
   "Promoted from Vorago Phase 12 (Profundum Phase 11 FR-061)". No API change, no behaviour change, no new member.
3. The Vorago header is **not** touched here (T006).

**Verified by:** `shared_tests` once T005 registers the TU (G3 gate). Run-log `T002.txt`: a `diff` of the two class bodies showing
only namespace / banner lines differ.

## T003 [P] — `band_level_metric.h` + `Profundum_BandLevelMetricSelfTest` (spec Conventions; plan §6.3)

**Files created:** `plugins/profundum/tests/band_level_metric.h`, `plugins/profundum/tests/integration/band_level_metric_test.cpp`.

1. **Test first** — `TEST_CASE("Profundum_BandLevelMetricSelfTest", "[profundum][metric]")`, 48 kHz, 2 s buffers:
   - `SECTION("IdenticalIsZero")`: a 100 Hz sine at amplitude 0.25 on L and 0.1 on R, compared with itself →
     `bandLevelDifferenceDb(…) == 0.0` in aggregate and in frame-wise mode.
   - `SECTION("SixDbInItsBand")`: the same signal against itself scaled by 2 → aggregate difference in `[6.01, 6.03]`; the M reading
     of band 1 ([80, 160)) differs by 6.02 ± 0.01 between the two `bandLevels` readings.
   - `SECTION("SilentBandExcluded")`: render 1 has L == R (S identically 0); render 2 equals render 1 scaled by 2 on M plus a
     side signal of amplitude 1e-9 (S ≈ −180 dBFS). Assert `bandLevels(render1).db[1][k] < -100` and
     `bandLevels(render2).db[1][k] < -100` for every S band k, and the aggregate difference is in `[6.01, 6.03]` (the M difference).
     Without the exclusion rule the S bands would read a ≥ 100 dB difference, so the bound proves exclusion.
   - `SECTION("FrameWisePeaks")`: two renders identical except the second is scaled by 2 over its last 0.5 s → frame-wise result
     ≥ 6.0, aggregate result < 6.0.
2. **Implement** — namespace `ProfundumPluginTest`, exactly the API of plan §6.3: `BandLevelReading` (`db[2][5]`, M/S × band),
   `kBandEdgesHz {20, 80, 160, 320, 1280, 20000}` (last edge `min(20000, fs/2)`), `bandLevels(L, R, fs)` and
   `bandLevelDifferenceDb(L1, R1, L2, R2, fs, frameWise)`. Welch with `Krate::DSP::FFT` at size 8192
   (`dsp/include/krate/dsp/primitives/fft.h:147` `prepare`, `:186` `forward`), Hann window, 50 % hop; M = (L + R)/2,
   S = (L − R)/2; per-band bin-power sum; `10·log10(E + 1e-20)`; a band is excluded when **both** readings are below −100 dB re
   the band level of a 0 dBFS sine (computed once in the metric). Frame-wise: one reading per 8192-sample frame, 50 % hop; max over
   frames × bands × {M, S}. Allocation is test-side only.

**Verified by:** `profundum_tests` once T005 registers the TU (G3 gate).

## T004 [P] — Placeholder TUs (no TEST_CASE)

**Files created** (each holds only `#include <catch2/catch_test_macros.hpp>`, `#include "profundum_test_fixture.h"` and one comment
`// <TEST_CASE names> — written by T0NN (specs/profundum-phase11-parameters/tasks.md)`; **no TEST_CASE**):

| File | Filled by |
|---|---|
| `plugins/profundum/tests/unit/param_table_test.cpp` | T013, T014 |
| `plugins/profundum/tests/unit/state_v2_test.cpp` | T017, T018, T019 |
| `plugins/profundum/tests/unit/midi_mapping_test.cpp` | T015, T020, T021 |
| `plugins/profundum/tests/unit/phase11_alloc_test.cpp` | T026 |
| `plugins/profundum/tests/integration/param_wiring_test.cpp` | T015, T016 |
| `plugins/profundum/tests/integration/state_restore_render_test.cpp` | T025 |
| `plugins/profundum/tests/integration/copy_compare_test.cpp` | T020 |
| `plugins/profundum/tests/integration/tail_test.cpp` | T022 |
| `plugins/profundum/tests/integration/param_path_audibility_test.cpp` | T023 |
| `plugins/profundum/tests/integration/mono_sum_params_test.cpp` | T024 |
| `plugins/profundum/tests/integration/determinism_test.cpp` | T025 |

**Verified by:** compile at the G3 gate.

---

# GROUP 3 — Test registration

## T005 — Test CMake registration (profundum tests, shared tests, shared header list)

**Files edited:** `plugins/profundum/tests/CMakeLists.txt`, `plugins/shared/tests/CMakeLists.txt`, `plugins/shared/CMakeLists.txt`.

1. `plugins/profundum/tests/CMakeLists.txt`:
   - add to `add_executable(profundum_tests …)` after `integration/processor_cpu_test.cpp`, under a `# Phase 11` comment: the four
     `unit/` TUs (`param_table_test.cpp`, `state_v2_test.cpp`, `midi_mapping_test.cpp`, `phase11_alloc_test.cpp`) and the eight
     `integration/` TUs (`param_wiring_test.cpp`, `state_restore_render_test.cpp`, `copy_compare_test.cpp`, `tail_test.cpp`,
     `param_path_audibility_test.cpp`, `mono_sum_params_test.cpp`, `determinism_test.cpp`, `band_level_metric_test.cpp`);
   - add all 12 to the `-fno-fast-math -fno-finite-math-only` block (they build non-finite values or compare bits).
     `integration/processor_cpu_test.cpp` stays absent from that block;
   - add `${CMAKE_SOURCE_DIR}/dsp/tests/unit/systems` to `target_include_directories(profundum_tests PRIVATE …)` with the comment
     `# read-only Phase 9 test helpers for SC-018 / SC-019 (plan 6.4; FR-070 forbids edits, not includes)`;
   - `krate_test_pch(profundum_tests)` stays last.
2. `plugins/shared/tests/CMakeLists.txt`: add `test_sustain_latch.cpp` after `test_midi_event_dispatcher.cpp`.
3. `plugins/shared/CMakeLists.txt`: add `src/midi/sustain_latch.h` after `src/midi/midi_event_dispatcher.h` (:35).

**Verified by:** the G3 gate (wide: touches CMake): `Profundum profundum_tests shared_tests` build with 0 warnings;
`profundum_tests.exe "Profundum_BandLevelMetricSelfTest"` and `shared_tests.exe "SustainLatch_SharedSemantics"` pass; the 11
placeholders compile.

---

# GROUP 4 — Vorago alias

## T006 — `Vorago::SustainLatch` becomes an alias (FR-061)

**Files edited:** `plugins/vorago/src/processor/sustain_latch.h` (only).

1. **Test first:** none new. `Vorago_SustainLatchUnit` and `Vorago_SustainLatch` (`plugins/vorago/tests/integration/sustain_test.cpp:51,
   :343`) are the regression guard. Non-vacuity arm: they construct `Vorago::SustainLatch` and call every member, so a missing or
   wrong alias fails to compile; `Vorago::Processor::latchForTest()` (`plugins/vorago/src/processor/processor.h:147`) returns the
   aliased type.
2. **Implement:** replace the file with exactly the plan §4.4 alias header: `#pragma once`, `#include "midi/sustain_latch.h"`,
   `namespace Vorago { using SustainLatch = Krate::Plugins::SustainLatch; }`, with a banner citing FR-061. Confirm that Vorago's
   plugin and test targets already resolve `"midi/…"` from `plugins/shared/src` (read `plugins/vorago/CMakeLists.txt` and
   `plugins/vorago/tests/CMakeLists.txt`; Vorago already includes other shared headers). If not, STOP AND SURFACE: no CMake edit
   belongs to this task.
3. `grep -rnE "class SustainLatch\b" dsp/ plugins/ tests/` prints exactly one line, in `plugins/shared/src/midi/sustain_latch.h`.

**Verified by:** the gate builds `Vorago vorago_tests` besides the default targets and runs `vorago_tests.exe "[vorago][midi]"`
(seconds). The full Vorago per-push suite runs once in T031.

---

# GROUP 5 — Identity

## T007 — `plugin_ids.h`: 76 IDs, state v2 constants, message IDs, band comment (plan §3)

**Files edited:** `plugins/profundum/src/plugin_ids.h`.

1. **Test first (compile-time, in the header):** the four `static_assert`s of plan §3.2 (endpoint A offset 88 = 36 + 32 + 16 + 4,
   B = A + `Krate::DSP::kMorphStateBytes`, runtime = B + 201, total 510 = runtime + `BassMorphEngine::kMorphRuntimeBytes`), plus
   `static_assert(kMorphRampTimeId + 1 == kNumRoutedIds)` and
   `static_assert(kAmpReleaseCurveId == 426 && kMotionEnvDecayId == 914 && kFieldSyncRateId == 809 && kCoreExcitationAsymmetryId == 208 && kAnchorFoldFeedId == 306 && kNotePitchBendRangeId == 417)`.
2. **Implement** exactly plan §3.1 (the enumerators appended to `enum ParameterIDs`), §3.2 and §3.3:
   - **keep `kCurrentStateVersion = 1` here** (the bump to 2 is T017, with the Phase 10 test amendments); add
     `kStateV1Version = 1`, `kStateNoteBlockBytes = 32`, `kStateMorphControlBytes = 16`, `kStateSeedBytes = 4`,
     `kStateEndpointAOffset = 88`, `kStateEndpointBOffset = 289`, `kStateRuntimeOffset = 490`, `kStateV2Bytes = 510`;
   - add `#include <krate/dsp/systems/bass_morph_engine.h>`;
   - the four message-ID constants `kMsgCopyEndpoint = "ProfundumCopyEndpoint"`, `kMsgCompare = "ProfundumCompare"`,
     `kMsgEndpoint = "ProfundumEndpoint"`, `kMsgCompareMode = "ProfundumCompareMode"` (`inline constexpr const char*`);
   - amend the band comment (:44–49) to `400-499 Drop / note behaviour / amp envelope` (FR-072).

**Verified by:** the G5 gate: build + every existing `[profundum]` case stays green (no behaviour change).

---

# GROUP 6 — Row type and laws

## T008 — `parameters/sound_param_row.h` + SC-006 leaf-law sections (FR-002, FR-003; plan §4.1, §4.3 generic row path)

**Files:** create `plugins/profundum/src/parameters/sound_param_row.h`; edit `plugins/profundum/tests/unit/param_denorm_test.cpp`.

1. **Test first** — new SECTIONs inside the existing `Profundum_ParamDenormRoundTrip` (param_denorm_test.cpp:116; Phase 10 owns
   the name):
   - `SECTION("LeafLawRoundTrip")`: for every leaf `i` in 0..61 with `lawOf(i).kind == Kind::F32`, at 101 points `v = k / 100.0`:
     `std::abs(normalizedFromPlain(lawOf(i), plainFromNormalized(lawOf(i), v)) - v) <= 1e-9`; `plainFromNormalized(f, 0.0) == double(f.lo)`,
     `normalizedFromPlain(f, double(f.lo)) == 0.0`, `plainFromNormalized(f, 1.0) == double(f.hi)`, `normalizedFromPlain(f, double(f.hi)) == 1.0` (exact).
   - `SECTION("LeafLawBoxes")`: for each F32 leaf `std::bit_cast<float>(leafWordFromNormalized(i, 0.0)) == f.lo` bitwise; for each
     F32 leaf except 45, `std::bit_cast<float>(leafWordFromNormalized(i, 1.0)) == f.hi` bitwise; for leaf 45 (`motion.cyclePhaseDeg`,
     the only `hiOpen` row) `leafWordFromNormalized(45, 1.0) == std::bit_cast<std::uint32_t>(std::nextafter(360.0f, 0.0f))`, and
     `REQUIRE(lawOf(45).hiOpen)`; a count of `hiOpen` rows over 0..61 equals 1.
   - `SECTION("ListIndexRoundTrip")`: for every Enum8 / Bool leaf and each `k` in `[0, maxEnum]`:
     `indexFromNormalized(double(k) / maxEnum, maxEnum) == k` and `leafWordFromNormalized(i, double(k) / maxEnum) == std::uint32_t(k)`.
   - `SECTION("DefaultLeafBitsRoundTrip")` (FR-003, R2): for all 62 leaves
     `leafWordFromNormalized(i, defaultNormalizedOf(i)) == kDefaultLeafBits[i]`.
   - `SECTION("LeafBitsVisitor")`: `leafBitsEqual(morphStateFromLeafBits(kDefaultLeafBits), MorphState{})`; for a `MorphState`
     with `recipe.depth = 0.25f`, `characterAlgorithm` = enumerator 3 and `motion.cycleRetrigger = false`,
     `leafBitsEqual(morphStateFromLeafBits(leafBitsOf(s)), s)`.
   - `SECTION("NegativeZeroSnapsToLo")`: `leafWordFromNormalized(0, -0.0) == std::bit_cast<std::uint32_t>(0.0f)` (the plugin never
     writes −0).
   - `SECTION("SyncRateIndex")`: for idx 0..29,
     `syncRateIndexOf(std::uint8_t(kNoteValueDropdownMapping[idx].note), std::uint8_t(kNoteValueDropdownMapping[idx].modifier)) == idx`
     (`dsp/include/krate/dsp/core/note_value.h:136`).
   - `SECTION("WriteSoundRowSetsFlags")`: an `EndpointParams` starts `dirty == true`, `syncPending == false`; after clearing dirty,
     `writeSoundRow` of a hand-built row `{id 200, leaf 0, kNoLeaf, …}` at v = 1 sets `leaf[0]` to the bits of `lawOf(0).hi`,
     `dirty == true`, `syncPending == true`, and leaves the other 61 words equal to `kDefaultLeafBits`.
2. **Implement** `sound_param_row.h` exactly as plan §4.1 (namespace `Profundum`): `MorphState` / `FieldInfo` aliases,
   `kNumLeaves = Krate::DSP::kMorphLeafFieldCount`, `kNoLeaf = 0xFF`, `LeafBits`, `DisplayUnit`, `SoundParamRow`, `lawOf`,
   constexpr `leafBitsOf` / `morphStateFromLeafBits` via `Krate::DSP::detail::morph_state_io::forEachMorphLeaf` and `std::bit_cast`,
   `inline constexpr LeafBits kDefaultLeafBits = leafBitsOf(MorphState{})`, the double mapping pair with explicit endpoint snaps
   (Linear / EqualPower → linear; Logarithmic `o == 0` → `logMapFromNormalized` / `logMapToNormalized`,
   `plugins/shared/src/ui/parameter_helpers.h:80, :85`; `o > 0` → `(lo + o)·((hi + o)/(lo + o))^v − o` and its inverse; Stepped →
   index), `leafWordFromNormalized` (float cast; `hiOpen && !(x < hi)` → `std::nextafter(hi, lo)`), `normalizedFromLeafWord`,
   `indexFromNormalized` (`min(maxIndex, int(v·maxIndex + 0.5))`, `v` clamped to [0, 1]), `syncRateIndexOf`, `PlainLaw` and its
   pair, `defaultNormalizedOf(leaf)` (`normalizedFromLeafWord(leaf, kDefaultLeafBits[leaf])` plus the bounded `nextafter` nudge of
   plan §4.1 "Defaults"), and `EndpointParams` (plan §4.3: 62 `std::atomic<std::uint32_t>` initialised from `kDefaultLeafBits`,
   `dirty{true}`, `syncPending{false}`, `load()`, `store()`). Declare and define `inline` the generic row path: `writeSoundRow`
   (1 word, or for a Sync Rate row `kNoteValueDropdownMapping[indexFromNormalized(v, 29)]` → note into `leaf`, modifier into `leaf2`;
   then `dirty` and `syncPending`), `registerSoundRow` (list rows via the label array with `defaultNormalizedValue` set explicitly —
   `createDropdownParameterWithDefault` sets only the current value, plan §1.7; continuous rows as a `Parameter` with `stepCount 0`,
   flags `kCanAutomate`, units from the row, `defaultNormalizedValue = defaultNormalizedOf(leaf)`), and `formatSoundRow` (the
   display table of plan §4.3: Time, Hz `"%.2f Hz"` below 1 else `"%.1f Hz"`, Db `"%+.1f dB"` of `x + 0.0`, Semitone, Percent,
   BipolarPercent, Mass100, Degrees; ASCII; `char8 text[64]`). Header-only; no `.cpp`.

**Verified by:** G6 gate (`[profundum]` filter).

---

# GROUP 7 — Section packs (all [P]; each file brand new)

Every pack (plan §4.2) holds its `constexpr` row array, its label arrays, its `static_assert`s and three thin wrappers over the
generic row path: `register<Section>Params(Steinberg::Vst::ParameterContainer&)`,
`handle<Section>ParamChange(EndpointParams&, Steinberg::Vst::ParamID, double) noexcept` (returns `bool` = handled) and
`format<Section>Param(Steinberg::Vst::ParamID, double, Steinberg::Vst::String128)` (returns `tresult`). **Test first = the
`static_assert`s, written before the rows**: (a) the set of `leaf` / `leaf2` values over the pack's rows equals the table's leaf set
exactly (no duplicate, no gap); (b) every list row has `labelCount == lawOf(leaf).maxEnum + 1` (a Sync Rate row:
`(maxEnum(leaf) + 1)·(maxEnum(leaf2) + 1) == Krate::DSP::kNoteValueDropdownCount == 30`); (c) every row ID lies in the pack's band;
(d) every Bool row has `labelCount == 2`. **No range literal for a morphed control appears in any pack** (FR-002): ranges come from
`lawOf(leaf)` only. Titles are exactly those of the spec ID table. Runtime proof is T013.

## T009 [P] — Core, anchor, fold / shape, character, dynamics packs

**Files created:** `plugins/profundum/src/parameters/core_params.h` (200→0 … 208→8: Depth, Body, Body Curvature, Body Emphasis,
Edge, Spectral Shift, Excitation Mix, Excitation Drive, Excitation Asymmetry), `anchor_params.h` (300→55 Anchor Waveform list
`Sine, Triangle, Rounded Sine, Asymmetric Sine, Octave Down`; 301→56 Anchor Mode `Lock, Follow, Punch, Ghost`; 302→57 Anchor Level;
303→58 Punch Boost dB; 304→59 Punch Time; 305→60 Ghost Time; 306→61 Anchor Fold Feed), `fold_shape_params.h` (500→9 **"Harmonic
Fold"** Mass100 display; 501→10 "Shape" Percent, no corner name), `character_params.h` (600→11 Character list **`Tube, Wavefold,
Rectify, Bite, Crush, Fracture`**; 601→12 Character Amount; 602→13 Character Tone), `dynamics_params.h` (700→53 Transient Attack dB;
701→54 Transient Sustain dB). Leaf indices per plan §1.1 / §4.2.

## T010 [P] — Field and motion packs

**Files created:** `plugins/profundum/src/parameters/field_params.h` (800→25 Image Mode `Solid, Spread, Wide`; 801→26 Spread;
802→27 Gravity; 803→28 Crossover Hz; 804→29 Curve; 805→30 Field Motion Depth; 806→31 Field Motion Rate; 807→32 Field Motion Source
`Drift, Cycle`; 808→33 Field Sync `Off, On`; 809→34 + 35 Field Sync Rate, 30 labels from
`Krate::Plugins::Parameters::kNoteValueDropdownStrings`, `plugins/shared/src/ui/note_value_ui.h:21`), `motion_params.h` (900→36
Motion Source `Off, Env, Cycle, Drift`; 901→37 Motion Target `Mass, Depth, Body, Edge, Shift`; 902→38 Motion Amount (BipolarPercent);
903→39 Motion Depth; 904→40 Cycle Rate Hz; 905→41 Cycle Shape `Sine, Triangle, Sawtooth, Square, S&H, Smooth Random`; 906→42 Cycle
Sync `Off, On`; 907→43 + 44 Cycle Sync Rate (30 labels); 908→45 Cycle Phase Degrees; 909→46 Cycle Retrigger `Off, On`; 910→47 Cycle
Fade-In; 911→48 Motion Polarity `Bipolar, Unipolar`; 912→49 Drift Smoothness; 913→50 Env Attack; 914→51 Env Decay). Extra
`static_assert`s: the Field Sync Rate default index is 19 and the Cycle Sync Rate default index is 13
(`syncRateIndexOf(kDefaultLeafBits[34], kDefaultLeafBits[35]) == 19`, `…[43], [44]) == 13`; plan §1.6).

## T011 [P] — Note pack (Drop + amp envelope E-rows, `NoteParams`)

**Files created:** `plugins/profundum/src/parameters/note_params.h`.
- E rows: 400→14 Drop Amount (st), 401→15 Drop Time, 402→16 Drop Shape, 403→17 Drop Rebound; 420→18 Attack, 421→19 Decay, 422→20
  Sustain, 423→21 Release, 424→22 Attack Curve, 425→23 Decay Curve, 426→24 Release Curve. `static_assert` coverage {14..24}.
- `NoteParams` exactly as plan §4.2 (8 atomics; defaults LastNote / off / 0 ms / Always / ConstantTime / AlwaysRetrigger / Reset /
  `ProfundumEngine::kDefaultBendRangeSt`), `handleNoteParamChange` (410 list 3; 411 `Off, On`; 412 `PlainLaw{0, 10000, 1, true}`;
  413 list 2; 414 list 3; 415 list 4; 416 list 2; 417 `round(24·v)`), `registerNoteParams` (410–417 only: lists `Last, Low, High`,
  `Off, On`, Portamento continuous with units `"ms"`, `Always, Legato Only`, `Constant Time, Constant Rate, Quantized`,
  `Always Retrigger, Legato, Envelope Retrigger, Oscillator Continuous`, `Reset, Continue`, Pitch Bend Range a `RangeParameter`
  [0, 24] `stepCount 24` default 2), `formatNoteParam(id, v, glideModeIndex, String128)` (412 Time rule with suffix `ms` / `ms/oct` /
  `ms` for glide index 0 / 1 / 2; ConstantRate ≥ 1000 → `"%.2f s/oct"`; FR-007), `saveNoteParams` (32 bytes in spec bytes 36–67
  order), `loadNoteParams` (false at the first failed read; non-finite float keeps; finite float clamped; enum outside
  `[0, maxEnum]` not stored; `global_params.h:177–212` precedent), `loadNoteParamsToController`, `resetNoteParams`,
  `pushNoteParams(const NoteParams&, Krate::DSP::ProfundumEngine&) noexcept` (the 8 setters of FR-014 with range-checked
  `static_cast`s).
- `static_assert`s: the default normalised of 417 is `2.0 / 24.0`; the 412 law maps 0 → 0 ms exactly.

## T012 [P] — Morph pack (Mass E-row, `MorphControlParams`, `CompareMode`) and performance pack

**Files created:** `plugins/profundum/src/parameters/morph_params.h`, `plugins/profundum/src/parameters/performance_params.h`.
- `morph_params.h`: E row 104→52 "Mass" (Mass100). `enum class CompareMode : std::uint8_t { AB = 0, A = 1, B = 2 }`;
  `MorphControlParams` as plan §4.2 (position 0, ramp `BassMorphEngine::kDefaultPositionRampMs`, anchorMorphMode
  `int(kDefaultAnchorMorphMode)` = 1, editTarget 0); `handleMorphControlParamChange` (105 linear [0, 1] through
  `sanitizeMorphPosition`; 1000 list 3 **`Hold, Blend, Yield`** = engine `Lock, Blend, Punch`; 1001 list `A, B`; 1002
  `PlainLaw{5, 2000, 0, true}`); `registerMorphControlParams` (1001 built with `createDropdownParameter` then
  `getInfo().flags = kIsList`, i.e. **not** `kCanAutomate`; FR-004); `formatMorphControlParam`; `saveMorphControlParams` (16 bytes,
  spec bytes 68–83: position f32, ramp f32, mode i32, target i32); `loadMorphControlParams`; `loadMorphControlParamsToController`;
  `resetMorphControlParams` (0 / 40 / Blend / A); `sanitizeMorphPosition(float p, float previous)` exactly as plan §4.2
  (non-finite → previous; `p < Krate::DSP::kDenormalThreshold` → `+0.0f`; `p >= 1` → `1.0f`; else `p`).
  `static_assert(lawOf(52).lo == 0.0f && lawOf(52).hi == 100.0f)`.
- `performance_params.h`: `PerformanceParams` (pitchBendNorm 0.5, sustainNorm 0, modWheel 0, channelPressure 0),
  `handlePerformanceParamChange`, `registerPerformanceParams` (4 Pitch Bend and 5 Sustain Pedal `kCanAutomate | kIsHidden`;
  6 Mod Wheel and 7 Channel Pressure `kIsHidden` only; titles `Pitch Bend`, `Sustain Pedal`, `Mod Wheel`, `Channel Pressure`;
  defaults 0.5 / 0 / 0 / 0), `formatPerformanceParam`, `resetPerformanceParams` (centre / up / 0 / 0).

**Verified by (G7):** compile only once T013 includes them; the gate builds `Profundum profundum_tests`.

---

# GROUP 8 — Aggregate table

## T013 — `sound_param_table.h` + `Profundum_SoundParamTableRouting` (FR-001, FR-002, FR-010, FR-012, FR-054; plan §4.3)

**Files:** create `plugins/profundum/src/parameters/sound_param_table.h`; edit `plugins/profundum/tests/unit/param_table_test.cpp`.

1. **Test first** — `TEST_CASE("Profundum_SoundParamTableRouting", "[profundum][params]")` (deviation 3):
   - `SECTION("SixtyRowsCoverAllLeaves")`: `kSoundRows.size() == 60`; over all rows the multiset of `leaf` and non-`kNoLeaf` `leaf2`
     values equals {0..61}, each once.
   - `SECTION("RouteTable")`: the count of `kParamRoutes` entries whose kind is not `None` == 84; the routed IDs in [400, 500) are
     exactly {400–403, 410–417, 420–426} (FR-054); IDs 0–3 route `Global`, 100–103 `Macro`, 4–7 `Performance`, 104 `Sound`,
     105 / 1000–1002 `MorphControl`, 410–417 `Note`; `soundRowOf(9999) == nullptr` and `soundRowOf(1003) == nullptr`.
   - `SECTION("EveryRowWritesItsLeafOnly")`: for each of the 60 rows, on a fresh `EndpointParams`, `handleSoundParamChange(ep, row.id, 1.0)`
     changes `leaf[row.leaf]` (and `leaf[row.leaf2]` for 809 / 907) and **no other word** (compare against `kDefaultLeafBits`;
     a row whose hi equals its default — none expected — is driven to 0.0 instead; assert the changed-word count is 1, or 2 for the
     Sync Rate rows).
   - `SECTION("SyncRateAll30")`: for 809 and 907 and idx 0..29, `handleSoundParamChange(ep, id, idx / 29.0)` writes
     `kNoteValueDropdownMapping[idx]` (note into `leaf`, modifier into `leaf2`).
   - `SECTION("UnroutedIgnored")`: `handleSoundParamChange(ep, 999, 0.5)` returns false and changes nothing.
2. **Implement** plan §4.3: `kSoundRows` (concatenation of the nine packs' rows in band order), `RouteKind`, `ParamRoute`, a
   constexpr builder for `kParamRoutes` (size `kNumRoutedIds`), the compile-time proofs listed in plan §4.3 (unique IDs, band
   membership, leaf union, 84 routes, the 400–499 set), and the aggregates `handleSoundParamChange` (dispatch on `id / 100`; band 1 →
   `morph_params.h` for 104), `registerSoundParams` (the nine `registerXxxParams` in band order), `formatSoundParam`, `soundRowOf`.

**Verified by:** G8 gate.

---

# GROUP 9 — Controller registration

## T014 — Controller registration and display (FR-001 – FR-009; SC-005, SC-006 parameter arms, SC-016, FR-006 strings)

**Files edited:** `plugins/profundum/src/controller/controller.h`, `plugins/profundum/src/controller/controller.cpp`,
`plugins/profundum/tests/unit/param_table_test.cpp`, `plugins/profundum/tests/unit/param_denorm_test.cpp`.

1. **Test first** — in `param_table_test.cpp`:
   - `TEST_CASE("Profundum_ParamTableConformance", "[profundum][params]")`: `getParameterCount() == 84`; an explicit per-ID
     expected table **in the test** (84 rows: ID, title, units, `stepCount`, flags, default normalised) compared exactly via
     `getParameterInfoByTag`. Defaults: E rows `== defaultNormalizedOf(leaf)`; 105 → 0; 1000 → 0.5 (Blend, index 1 of 2); 1001 → 0;
     1002 → `normalizedFromPlain(PlainLaw{5,2000,0,true}, 40.0)`; 410–416 → index 0 / 0 / 0 / 0 / 0 / 0 / 0; 417 → 2/24; 4 → 0.5;
     5 / 6 / 7 → 0. IDs 0–3 and 100–103 match their Phase 10 values (read `global_params.h` / `macro_params.h`). Every registered ID
     lies in a documented band. **FR-009 strings**: ID 500 title `"Harmonic Fold"`; 600's list strings via `getParamStringByValue`
     at k/5 read exactly `Tube, Wavefold, Rectify, Bite, Crush, Fracture`; no parameter title contains `"Bite"`; 501 formatted at
     0, 0.5, 1 contains none of the six Character labels; 1000 at 0, 0.5, 1 reads `Hold, Blend, Yield`; 301 at k/3 reads `Lock,
     Follow, Punch, Ghost`. **Flags**: 6 and 7 have `kIsHidden` and not `kCanAutomate`; 4 and 5 have both; 1001 has `kIsList` and
     not `kCanAutomate`; every other new ID has `kCanAutomate`; every list has `kIsList`.
   - `TEST_CASE("Profundum_ParamDisplayFormats", "[profundum][params]")`: exact strings through the controller at
     `normalizedFromPlain(x)` of: Release 423: 999.9 → `"999.9 ms"`, 999.96 → `"1.00 s"`, 1000 → `"1.00 s"`, 2500 → `"2.50 s"`;
     Cycle Rate 904: 0.01 → `"0.01 Hz"`, 0.3 → `"0.30 Hz"`, 2.5 → `"2.5 Hz"`, 20 → `"20.0 Hz"`; Transient Attack 700: −12 / 0 / +12 →
     `"-12.0 dB"`, `"+0.0 dB"`, `"+12.0 dB"`; Drop Amount 400: 24 → `"24.0 st"`; Spread 801: 0.5 → `"50%"`; Motion Amount 902: −1 / 1 →
     `"-100%"`, `"+100%"`; Mass 104: 50 → `"50"`; Harmonic Fold 500: 100 → `"100"`; Cycle Phase 908: 90 → `"90 deg"`.
   - `TEST_CASE("Profundum_PortamentoLabelFollowsGlideMode", "[profundum][params]")` (SC-016): set 414 to 0, 0.5, 1 on the
     controller (`setParamNormalized`); format 412 at normalised 0.3 → strings ending `" ms"`, `" ms/oct"`, `" ms"`; the numeric
     prefix is identical across the three.
   - In `param_denorm_test.cpp` (amend): the Phase 10 assertion at :233 `getParameterCount() == 8` → `== 84`; review the loop at
     :236 so it iterates the new IDs (no assertion relaxed). New SECTION `"RegisteredParamRoundTrip"` inside
     `Profundum_ParamDenormRoundTrip`: for 412, 1002, 105, 4, 6, 7 and every continuous E row, 101 points
     `|normalizedFromPlain(law, plainFromNormalized(law, v)) − v| ≤ 1e-9`; lo → 0.0 and hi → 1.0 exactly; for every list parameter,
     index → normalised → index exact through the controller's `toPlain` / `toNormalized`.
2. **Implement** in the controller: `initialize` registers, in this order, global, macro, performance, sound
   (`registerSoundParams`), note (`registerNoteParams`), morph control (`registerMorphControlParams`). `getParamStringByValue`
   tries global → macro → sound → note (glide index read from `getParamNormalized(kNoteGlideModeId)` → `indexFromNormalized(v, 2)`)
   → morph control → performance → the base class. No `IMidiMapping` yet (T021).

**Verified by:** G9 gate.

---

# GROUP 10 — Processor routing and assembly

## T015 — Processor routing, endpoint slots, assembly, Mass offset, push (FR-010 – FR-015, FR-055, FR-067; SC-007, SC-008, SC-009 1–3, SC-024 arithmetic)

**Files edited:** `plugins/profundum/src/processor/processor.h`, `plugins/profundum/src/processor/processor.cpp`,
`plugins/profundum/tests/profundum_test_fixture.h`, `plugins/profundum/tests/integration/param_wiring_test.cpp`,
`plugins/profundum/tests/unit/midi_mapping_test.cpp`.

1. **Fixture additions** (plan §6.1): `setParam(id, v)` and `setParamsInBlock({{id, v}, …})` (one block, queues in the given
   order) over the existing `MultiParamChanges` (:141); `randomValidMorphState(std::uint32_t seed, const RandomStateConstraints& = {})`
   (`std::mt19937`, one normalised draw per leaf through `leafWordFromNormalized`, constraints `motionSourceOffOrEnv`,
   `fieldMotionDepthZero`); `engine()` accessor returning `proc->engineForTest()`.
2. **Test first** — in `param_wiring_test.cpp`:
   - `TEST_CASE("Profundum_DefaultsMatchEngine", "[profundum][params]")` (SC-007): fresh prepared processor, one 512-sample block:
     `leafBitsEqual(getEndpoint(A), MorphState{})` and for B; `monoPriority() == MonoMode::LastNote`, `legato() == false`,
     `portamentoTimeMs() == 0.0f`, `portamentoMode() == PortaMode::Always`, `glideMode() == GlideMode::ConstantTime`,
     `noteMemoryMode() == NoteMemory::Mode::AlwaysRetrigger`, `noteRetrigger() == NoteMemory::Retrigger::Reset`,
     `pitchBendRange() == 2.0f`, `pitchBend() == 0.0f`, `morphPosition() == 0.0f`, `morphRampMs() == 40.0f`,
     `anchorMorphMode() == AnchorMorphMode::Blend`; `endpointRejectionsForTest() == 0`. `SECTION("HostResendsDefaults")` (FR-003):
     every registered parameter's `defaultNormalizedValue` (read from a controller) sent in one block, one more block → the same
     getters equal the same values **bitwise** and both endpoints `leafBitsEqual` `MorphState{}`.
   - `TEST_CASE("Profundum_EveryParamReachesEngine", "[profundum][params]")` (SC-008): an explicit 72-row oracle table (60 E
     params via `getEndpoint(A)` leaf words, 410–417 via getters, 105 / 1002 / 1000 via `morphPosition()` / `morphRampMs()` /
     `anchorMorphMode()`, 4 via `pitchBend()`). For each: norm 0 → block → read-back == lo / first entry; norm 1 → block → hi / last
     entry. Cycle Phase reads `std::nextafter(360.0f, 0.0f)`; Sync Rate checks both leaves against `kNoteValueDropdownMapping[0]` /
     `[29]`; Pitch Bend reads −1 / +1; 105 at 1 reads 1.0f. **No cross-talk**: for each E row, `leafBitsOf` of the other 61 leaves
     of A and all 62 of B are identical before and after. The 72 passes are counted (`REQUIRE(passes == 72)`). `SECTION("EditTargetRoutes")`
     (1001): 1001 → 1 then a Depth (200) edit lands in B and A is unchanged (counter reaches 73). `SECTION("SyncRateAll30")`: for 809
     and 907, idx 0..29, `idx / 29.0` → one block → both engine leaves equal `kNoteValueDropdownMapping[idx]`.
   - `TEST_CASE("Profundum_EditTargetAddressing", "[profundum][params]")` (SC-009, parts 1–3 here; 4–6 in T016):
     (1) target A, Depth 0.9 → A's depth == mapped 0.9, B unchanged; (2) two processors fed the same 1 s stream (E1 note), one of
     which switches 1001 → 1 at block 10 with no other edit: every engine leaf of both endpoints `leafBitsEqual` and every rendered
     sample bit-identical (`std::memcmp` of the two output buffers == 0); (3) one block with queues `[200 = 0.9, 1001 = 1]` in that
     order → B's depth is the mapped 0.9, A unchanged (FR-011: Edit Target first).
   - In `midi_mapping_test.cpp`, `TEST_CASE("Profundum_MassPerformanceOffset", "[profundum][midi]")`, `SECTION("Arithmetic")`
     (SC-024): Mass A = 20, B = 80 set through 104 with Edit Target A then B; Mod Wheel (6) = 0.25, one block →
     `getEndpoint(A).mass == 45.0f`, `getEndpoint(B).mass == 100.0f`; + Channel Pressure (7) = 0.5 → 95 / 100; Mod Wheel 0.8 +
     Pressure 0.7 → 100 / 100; with both Mass at 0, 6 alone at 0.5 → 50 / 50 and 7 alone at 0.5 → 50 / 50; both back at 0 → the
     engine's Mass equals the slot values bitwise (20 / 80). `SECTION("SlotsUntouched")`: after those moves, both endpoints' slot
     word 52 (read via a new seam `endpointSlotForTest(e, leaf)`, allocation-free, const) equals the bits of 20 / 80.
3. **Implement** plan §4.5 members (`endpoints_`, `noteParams_`, `morphParams_`, `perfParams_`, `endpointRejections_`,
   `lastPerfOffsetC_`, and `compareMode_` initialised to `CompareMode::AB`) and §4.5.1 steps 1, 3, 7, 8 only (copy, restore, compare and pedal come later; `cmp` is fixed at AB for now
   but read as in step 1 from `compareMode_` initialised to AB): two-pass `processParameterChanges` (pass 1: the last point of queue
   1001; pass 2: every other queue, last point wins, `detail::isFinite`, clamp [0, 1], route by `kParamRoutes[id]`, id ≥
   `kNumRoutedIds` or `None` ignored; ID 5 stored into `perfParams_.sustainNorm` only for now); `applyEndpoints()` with the Mass
   offset of FR-055 (change detection on the bits of `c`, both endpoints dirty on change, add skipped when `c == 0`, clamp to
   `lawOf(52)`; `!engine_->setEndpoint(e, s)` → `endpointRejections_++`); extended `pushParamsToEngine()` (Phase 10 setters +
   `pushNoteParams` + `setPitchBend(2v − 1)` + `setMorphRampMs` + `setAnchorMorphMode` + `setMorphPosition`); `setupProcessing`
   calls `applyEndpoints()` before its `pushParamsToEngine()` (:138). Seams: `setEngineObserverForTest`,
   `endpointRejectionsForTest`, `endpointSlotForTest`. Keep `static_assert(sizeof(Processor) < 64u * 1024u)` green.

**Verified by:** G10 gate.

---

# GROUP 11 — Controller mirror and Edit Target

## T016 — Controller mirror, Edit Target re-display, endpoint `notify` (FR-021, FR-022, FR-024, FR-025 controller side; SC-009 4–6)

**Files edited:** `plugins/profundum/src/controller/controller.h`, `plugins/profundum/src/controller/controller.cpp`,
`plugins/profundum/tests/profundum_test_fixture.h`, `plugins/profundum/tests/integration/param_wiring_test.cpp`.

1. **Fixture additions:** `RecordingComponentHandler` (implements `IComponentHandler` and `IComponentHandler2`; records per-ID
   `beginEdit` / `performEdit` / `endEdit` counts, every `restartComponent` flag in call order, `setDirty(true)` count, and an
   optional `std::function<void(int32)>` hook invoked inside `restartComponent`); `ControllerFixture` (heap `Controller`, `initialize`, handler attached via
   `setComponentHandler`); `makeEndpointMessage(Endpoint e, const MorphState&)` building a `Steinberg::Vst::HostMessage` with ID
   `kMsgEndpoint`, int `"endpoint"`, binary `"blob"` (201 bytes from `serializeMorphState`).
2. **Test first** — add to `Profundum_EditTargetAddressing` (param_wiring_test.cpp):
   - `SECTION("ControllerRedisplay")` (4): deliver `makeEndpointMessage(B, randomValidMorphState(0x9B))` to `controller->notify`;
     then `setParamNormalized(1001, 1.0)`; all 60 E-parameter values equal the B mirror's normalised values within 1e-9
     (`normalizedFromLeafWord`, Sync Rate via `syncRateIndexOf / 29`).
   - `SECTION("NoHostEdits")` (5): across that switch the handler records **0** `beginEdit` / `performEdit` / `endEdit` for the 60 IDs.
   - `SECTION("OneRestartAfterRedisplay")` (6): exactly **one** `restartComponent(kParamValuesChanged)` per switch (A → B → A gives
     2); `redisplayCountForTest()` rises by 1 per switch; the handler's `restartComponent` hook reads the controller's
     `getParamNormalized` for all 60 E IDs **at the moment of the call** and they already equal the newly selected mirror's values
     within 1e-9 (so the restart comes after the re-display); still 0 `performEdit`.
   - `SECTION("RejectedBlobKeepsMirror")`: a message whose blob version byte is flipped leaves `mirrorForTest(B)` unchanged; a
     message for the current Edit Target re-displays (values change) without a restart call.
3. **Implement** plan §4.6: `mirror_`, `inStateLoad_`, `setParamNormalized` override (base call; if `tag == kMorphEditTargetId`, the
   index changed and `!inStateLoad_` → `redisplayEditTarget()` then, if `componentHandler`, one
   `restartComponent(kParamValuesChanged)`), `redisplayEditTarget()` (60 × `EditControllerEx1::setParamNormalized`), `notify`
   for `kMsgEndpoint` (`getInt("endpoint")`, `getBinary("blob")`, size 201 and `deserializeMorphState` → replace; current target →
   redisplay, no restart, no performEdit) and `kMsgCompareMode` (sets `compare_`); `mirrorForTest`, `compareModeForTest`,
   `redisplayCountForTest`. Other messages → `EditControllerEx1::notify`.

**Verified by:** G11 gate.

---

# GROUP 12 — State v2 (format, seed, v1, publication)

## T017 — State v2 format, seed, v1 reset, runtime-blob publication; Phase 10 amendments (FR-040, FR-041 synthesis, FR-043, FR-044, FR-046; SC-012 arms 1, 2, 5)

**Files edited:** `plugins/profundum/src/plugin_ids.h`, `plugins/profundum/src/processor/processor.h`,
`plugins/profundum/src/processor/processor.cpp`, `plugins/profundum/tests/profundum_test_fixture.h`,
`plugins/profundum/tests/unit/state_v2_test.cpp`, `plugins/profundum/tests/unit/lifecycle_test.cpp`,
`plugins/profundum/tests/unit/state_roundtrip_test.cpp`, `plugins/profundum/tests/integration/param_flow_test.cpp`.

1. **Fixture additions:** `writeV2Stream(proc)` (a real `getState` stream as bytes), `withEndpoints(bytes, A, B)` (overwrite bytes
   `kStateEndpointAOffset` / `kStateEndpointBOffset` with `serializeMorphState` output; layout constants from `plugin_ids.h` only),
   `v1Stream(globals, macros)` (header `kStateV1Version`), `makePriorStateP(fixture)` (plan §6.2 SC-012: random A / B 0x12A /
   0x12B, non-default note fields, position 0.7, ramp 200 ms, Punch, Edit Target B, seed 0x5EED, rendered 1 s).
2. **Test first** — `state_v2_test.cpp`, `TEST_CASE("Profundum_StateVersions", "[profundum][state]")`:
   - `SECTION("V1ResetsPhase11Fields")` (arm 1, processor part): on P, `setState(v1)` then one block: globals / macros equal the
     stream; both slots and both engine endpoints `leafBitsEqual` `MorphState{}`; every note and morph-control getter at its
     default; Edit Target A; `getState` bytes 84–87 == `kDefaultSeed` LE; `getState` bytes 0–489 equal those of a fresh processor
     that loaded the same v1 stream and rendered one block (bytes 490–509 excluded, plan §4.5.3).
   - `SECTION("CutKeepsPrior")` (arm 2): for k = 0 … 509, a P instance, `setState(cut_k(S))` of a different non-default v2 stream S;
     **before any block** `getState` bytes 0–489 follow the field oracle (each field fully inside `[0, k)` takes S's bytes, all
     others P's; an endpoint needs all 201 bytes; a 4-byte field split by the cut keeps P's); bytes 490–509 equal P's last published
     blob. Then one block under `AllocationScope` → count 0. (The twin-engine clause is added by T018.)
   - `SECTION("Unchanged")` (arm 5): a fresh prepared, unrendered processor's `getState` bytes 490–509 equal the hand-built
     synthesized blob (`[0] = 1`, target / p = bits of the position, p_a = bits of `BassMorphEngine::curve(mode, position)`
     (FR-041 ruling 2026-10-22; Lock / Blend / Punch at 0.7 → 0 / 0.7 / 0.343), `[13]` = mode, `[14..17]` = ramp, `[18] = 0`,
     `[19] = 0`); for Blend / position 0 they also equal a real `ProfundumEngine`'s `serializeMorphRuntime` after `prepare` (proves
     the re-stated offsets `kRtTargetOffset = 1` …). Version 3 → `kResultFalse` and `getState` unchanged byte for byte. A flipped
     endpoint version byte, or a bool byte of 2, leaves that endpoint's slot at its prior value.
   - `SECTION("PerformanceResetOnLoad")` (FR-046): after Pitch Bend 1.0, `setState` + one block → `pitchBend() == 0.0f`.
3. **Amend the Phase 10 tests** (plan §6.5; keep each behavioural assertion, relax none): lifecycle_test.cpp :238 / :491 write
   `kStateV1Version`, :247 / :501 compare the processor's `getState` size with `kStateV2Bytes`; state_roundtrip_test.cpp :93 / :112
   build v1 streams with `kStateV1Version`, and `ByteIdentical36` (:233, :426–433) becomes: size `kStateV2Bytes`, and bytes 4–35 equal
   the v1 body; param_flow_test.cpp:385 writes `kStateV1Version`.
4. **Implement** `kCurrentStateVersion = 2` (plugin_ids.h); `getState` / `setState` exactly as plan §4.5.3 (setState touches atomics
   only; version > 2 refused before any field; v1 branch resets; v2 chain stops at the first short read; rejected endpoint keeps
   prior; always: `compareMode_ = AB`, `compareSyncPending_ = true`, `resetPerformanceParams`, `clearLatchPending_ = true`, both
   `syncPending = true` — `flushControllerSync()` does not exist yet, T020 adds the call); the seed (`seed_` atomic; `setupProcessing`
   calls `engine_->setSeed(seed_.load())` right before `engine_->prepare` :132; `setActive(false)` right before the reset :149);
   the runtime-blob **writer** `publishRuntime()` at §4.5.1 step 12 (the Boehm seqlock of plan §4.5.2, every block while `cmp == AB`)
   and the bounded `getState` **reader** (1024 tries, fallback to synthesized) with precedence 2 → 3 of plan §4.5.2 (precedence 1
   is T018). The 20-byte blob read in `setState` is stored nowhere yet (T018 adds the hand-off); a pending flag is not needed.

**Verified by:** G12 gate (every Phase 10 case stays green with the amendments).

---

# GROUP 13 — Restore hand-off

## T018 — Restore hand-off seqlock and runtime restore (FR-042, FR-041 precedence 1; SC-010 processor, SC-012 arm 2 twin clause and arm 3, `RestoreHandOffNoTear`)

**Files edited:** `plugins/profundum/src/processor/processor.h`, `plugins/profundum/src/processor/processor.cpp`,
`plugins/profundum/tests/unit/state_v2_test.cpp`.

1. **Test first** — in `state_v2_test.cpp`:
   - `TEST_CASE("Profundum_StateRoundTripV2", "[profundum][state]")` (SC-010 processor part): A / B = `randomValidMorphState(0x11A)` /
     `(0x11B)`; every note field non-default; position ramped 1 → 0.5 at 40 ms (selections B inside the hysteresis band); Edit
     Target B; seed 0x1234; 2 s rendered. `getState` (size 510) → a fresh processor's `setState` **before** `setupProcessing` /
     `setActive(true)` → prepare → one block → `getState`: `memcmp == 0` over 510 bytes; restored endpoints `leafBitsEqual` the
     originals; `morphPosition()` equal; bytes 490–509 equal; `restoreApplyCountForTest() == 1`; a following Depth edit lands in B
     and A is unchanged. `SECTION("TinyPosition")` (R10): position sent through 105 as `1e-20f` and, in a second run, the denormal bit
     pattern `0x00000001` (through a `volatile`), ramp rendered and settled: the stored position's bits are 0; save → fresh `setState`
     → one block → `restoreApplyCountForTest() == 1`; save-load-save byte-identical over 510; the same two values hand-written into
     bytes 68–71 of a stream give the same result.
   - `Profundum_StateVersions` additions: in `SECTION("CutKeepsPrior")`, after the block: the engine's `serializeMorphRuntime`
     (via `engineForTest()`) equals that of a **twin** with history P that received a full v2 stream holding the oracle's expected
     explicit fields and endpoints and a blob whose version byte is 0xFF; `restoreApplyCountForTest()` unchanged; the post-block
     bytes 490–509 are never part of the byte oracle. New `SECTION("FullStreamAppliesBlob")` (arm 3, k = 510): S's blob from a source
     saved mid-ramp (p ≠ target); before any block `getState` bytes 490–509 equal S's blob (precedence 1); after one block
     `restoreApplyCountForTest()` rose by 1 and `lastRestoreBlobForTest()` equals S's blob; precondition: the 0xFF twin's engine blob
     differs from the restored engine's in p, p_a or the mask byte.
   - `TEST_CASE("Profundum_RestoreHandOffNoTear", "[profundum][state][rt]")`: S1 / S2 with identical explicit fields and endpoints
     and different mid-ramp blobs (words 1–4 differ, word 0 equal: it carries the version byte; masks 0x0F vs 0x00). 2000 rounds: `process()` on a dedicated `std::thread`
     for 8 blocks while the test thread calls `setState(S1)` then `setState(S2)`. After the join `lastRestoreBlobForTest()` equals
     S1's or S2's blob byte for byte, never a mix; after one more block it equals S2's. `AllocationScope` on the audio thread only →
     0. Total wall clock must stay under ~10 s (reduce nothing; if slower, report it in the run-log).
2. **Implement** plan §4.5.2 "Restore hand-off" and §4.5.1 step 9: `restoreSeq_`, `restoreWords_`, `restoreAppliedSeq_`,
   `restoreSeenSeq_`, `restoreWriterBusy_`, `publishRestore(words)` (writer; cancel = five zero words), `applyPendingRestore()` (one
   read try per block; bitwise target / mode / ramp check against `morphParams_`; `engine_->restoreMorphRuntime(blob, 20)`; true →
   copy to `lastRestoreBlob_`, `++restoreApplyCount_`), `restoreAppliedSeq_.store(restoreSeenSeq_, release)` after
   `publishRuntime()` in step 12, `getState` precedence 1. `setState` calls `publishRestore(blob)` or `publishRestore(cancel)` per
   plan §4.5.3. Seams `lastRestoreBlobForTest`, `restoreApplyCountForTest`.

**Verified by:** G13 gate.

---

# GROUP 14 — Controller state

## T019 — Controller `setComponentState` v2 (FR-045; SC-010 controller, SC-012 arm 1 controller part and arm 4)

**Files edited:** `plugins/profundum/src/controller/controller.cpp`, `plugins/profundum/src/controller/controller.h`,
`plugins/profundum/tests/unit/state_v2_test.cpp`.

1. **Test first:**
   - `Profundum_StateRoundTripV2`, `SECTION("ControllerDisplaysB")`: `setComponentState` of the saved stream → `mirrorForTest(A/B)`
     `leafBitsEqual` the originals; the 60 E values equal B's normalised values within 1e-9; `getParamNormalized` of 410–417, 105 and
     1000–1002 equals the saved values' normalised forms (exact for lists and stepped rows, 1e-9 for 412, 1002, 105); 1001 reads 1.0;
     the handler records 0 `restartComponent` and 0 `performEdit` during the load.
   - `Profundum_StateVersions`, `SECTION("ControllerV1")`: a controller holding P's state gets `setComponentState(v1)` → mirror is
     `MorphState{}` for both; 410–417, 105 and 1000–1002 display their registered defaults.
   - `SECTION("ControllerCutKeepsPrior")` (arm 4): on a controller holding P's state, `setComponentState` of S cut at
     k ∈ {50, 150, 300, 500} and of a full S with endpoint A's version byte flipped: the mirror and the displayed 410–417, 105 and
     1000–1002 follow FR-044 (complete fields take S, the rest — including a cut or rejected endpoint — keep P's).
   - Hidden 4 / 5 / 6 / 7 read 0.5 / 0 / 0 / 0 after every load.
2. **Implement** plan §4.6 `setComponentState` steps 1–6 (version gate ≤ 2; v1 reset of the mirror and of 410–417 / 105 / 1000–1002 to
   defaults with `inStateLoad_ = true` around 1001; v2: note block, morph block (105 through `sanitizeMorphPosition`), skip the seed,
   endpoints into the mirror under FR-044, skip the blob; hidden IDs reset; final `redisplayEditTarget()` with no restart call).

**Verified by:** G14 gate.

---

# GROUP 15 — Copy, compare, sync

## T020 — Copy, Compare, controller sync sender, timer (FR-025, FR-030 – FR-033, FR-056; SC-013, SC-014, SC-025, SC-024 sync sections)

**Precondition:** T001 recorded the FR-025 ruling (2026-10-22: pull on Edit Target switch + 50 ms timer on Windows / macOS).

**Pull on Edit Target switch (FR-025, SC-025 "Edit Target pull").** Add `kMsgRequestSync` to `plugin_ids.h` (plan §3.3). The
processor's `notify` sets both `syncPending` flags and `compareSyncPending_`, then flushes (plan §4.5.5). The controller's 1001
hook (T016) sends `kMsgRequestSync` before it re-displays; when an endpoint message replaces the Edit Target endpoint after a
re-display, the controller re-displays and calls `restartComponent(kParamValuesChanged)` again. Test first in
`copy_compare_test.cpp`, inside `Profundum_ProcessorControllerSync`: `SECTION("EditTargetPull")` (stale mirror → exactly one
request → re-displayed Depth equals the processor's B slot, leaf bitwise) and `SECTION("EditTargetPullDelayedReply")` (the
recorder holds the reply until after the re-display → 2 re-displays, 2 restarts, final values equal the slots). Runs on every OS.

**Files edited:** `plugins/profundum/src/processor/processor.h`, `plugins/profundum/src/processor/processor.cpp`,
`plugins/profundum/src/controller/controller.h`, `plugins/profundum/src/controller/controller.cpp`,
`plugins/profundum/tests/profundum_test_fixture.h`, `plugins/profundum/tests/integration/copy_compare_test.cpp`,
`plugins/profundum/tests/unit/midi_mapping_test.cpp`.

1. **Fixture addition:** `RecordingConnectionPoint` (plan §6.1): an `IConnectionPoint` recording each message's ID, its int and
   binary attributes, and `std::this_thread::get_id()`, then forwarding to a target `notify`; `ConnectedPair` wiring processor ↔
   controller through two recorders (the processor's peer forwards to `controller->notify`, the controller's to `processor->notify`).
2. **Test first** — `copy_compare_test.cpp`:
   - `TEST_CASE("Profundum_CopyEndpoint", "[profundum][morph]")` (SC-013): A = random 0x13A, B = random 0x13B; `copyEndpoint(A)`;
     one block; `flushControllerSync()`: the processor's B `leafBitsEqual` A (engine and slots); `mirrorForTest(B)` equals A and the
     recorder shows a `kMsgEndpoint` message for endpoint 1 carrying it; `setDirty(true)` count == 1; 1001 and 105 unchanged. The
     same for B → A.
   - `TEST_CASE("Profundum_CompareDoesNotWriteState", "[profundum][morph]")` (SC-014): showcase-distinct endpoints, position 0.6,
     E1 MONO held; `setCompare(A)`; render past the 40 ms ramp; `bandLevelDifferenceDb` (aggregate) against a position-0 reference
     processor fed the same stream ≤ **0.1 dB** over [140 ms, 1140 ms) after entering compare; `getState` during the compare is
     byte-identical to the stream taken just before entering it; after `setCompare(AB)` and one block `morphPosition() == 0.6f`;
     `setState` → `compareModeForTest() == CompareMode::AB`; second arm: compare B → `setActive(false)` → `compareModeForTest() == AB`.
   - `TEST_CASE("Profundum_ProcessorControllerSync", "[profundum][morph]")` (SC-025): `process()` runs on a dedicated `std::thread`;
     `flushControllerSync()` on the test thread. Unobserved Depth edit → the mirror's leaf 0 equals the slot bitwise. Copy → exactly
     one 201-byte message, for B. `setState` → two blobs and a compare-mode AB message. Compare B + `setActive(false)` → an AB
     message and the controller's compare reads AB. Every recorded send's thread id ≠ the audio thread's id. Mod Wheel / Pressure
     moves alone → 0 endpoint messages. `AllocationScope` with the per-thread filter on the audio thread only → 0.
     `SECTION("TimerArm")`: an unobserved Depth edit, one block, then `proc->onTimer(nullptr)` on the test thread (never
     `flushControllerSync()` directly) → mirror leaf equals the slot; `hasSyncTimerForTest()` true after `initialize` on Windows
     and macOS (on Linux `SKIP("SDK Timer::create returns nullptr without a factory (timer.cpp:346-351)")` for that check only); after
     `terminate`, `hasSyncTimerForTest()` false on every OS.
   - In `midi_mapping_test.cpp`, `Profundum_MassPerformanceOffset` (SC-024): `SECTION("NeverWritten")`: after Mod Wheel / Pressure
     moves `getState` is byte-identical to the stream before the moves; `SECTION("MirrorAndNoMessage")`: through a `ConnectedPair`,
     the mirror's Mass equals the persisted leaf and a 6 / 7 change alone produces 0 `kMsgEndpoint` messages;
     `SECTION("ResetOnLoad")`: after 6 = 1 and 7 = 1, `setState` + one block → `getEndpoint(A/B).mass` equal the persisted values.
3. **Implement** plan §4.5.1 step 2 (`applyPendingCopy`), step 1 (`cmp` read once from `compareMode_`), step 8 compare-aware position
   (A → 0, B → 1, AB → the registered sanitised position), step 12 publication only when `cmp == AB` (FR-032), §4.5.4
   `flushControllerSync()` (with `flushBusy_`; sends persisted slots only), the 50 ms `Steinberg::Timer` created in `initialize`
   and released in `terminate`, `onTimer`, the calls at the end of `notify` / `setState` / `setActive` / `getState`, §4.5.5 `notify`,
   §4.5.6 `setActive(false)` compare reset; seams `compareModeForTest`, `hasSyncTimerForTest`. Controller: `copyEndpoint(from)`
   (send `kMsgCopyEndpoint` + `setDirty(true)`; mirror untouched), `setCompare(m)` (send `kMsgCompare`; only the reply updates
   `compare_`). The processor class gains `Steinberg::ITimerCallback`.

**Verified by:** G15 gate.

---

# GROUP 16 — MIDI mapping and sustain

## T021 — `IMidiMapping`, pedal points, latch, pitch bend (FR-050 – FR-052; SC-015)

**Files edited:** `plugins/profundum/src/controller/controller.h`, `plugins/profundum/src/controller/controller.cpp`,
`plugins/profundum/src/processor/processor.h`, `plugins/profundum/src/processor/processor.cpp`,
`plugins/profundum/tests/unit/midi_mapping_test.cpp`, `plugins/profundum/tests/unit/lifecycle_test.cpp`.

1. **Test first** — `midi_mapping_test.cpp`, `TEST_CASE("Profundum_MidiMapping", "[profundum][midi]")`:
   - `SECTION("AssignmentTable")`: on channels 0 and 15, `getMidiControllerAssignment(0, ch, 1, id)` → `kResultOk`, id 6; 64 → 5;
     128 (`kAfterTouch`) → 7; 129 (`kPitchBend`) → 4. Bus 1 with CC 1 and bus 0 with CC 7 → `kResultFalse`.
   - `SECTION("PitchBend")`: range 417 = 12/24, Pitch Bend 4 = 1.0, one block → `pitchBend() == 1.0f`, `pitchBendRange() == 12.0f`.
     With an observer attached through `setEngineObserverForTest` (before `setupProcessing`), E1 MONO held: the voice-0 `fCore` tap's
     mean over [200, 700) ms after the bend against the same length before it gives `12·log2(ratio)` within **12 ± 0.05 st**.
   - `SECTION("SustainEquivalence")`: max |diff| == **0** for (a) note-off at 0.5 s with pedal down from before it until pedal-up at
     1.0 s vs a pedal-free render whose note-off is at the pedal-up offset; (b) note-off with pedal up vs the plain render. Slicing
     stand-ins in each reference: a Sustain point of value 0 and a note-off for a non-sounding key at the moved events' offsets.
   - `SECTION("MultiPointQueue")`: one block with Sustain queue [down @ 64, up @ 192, down @ 320] and note-off @ 128 renders
     identically (max |diff| == 0) to a reference with the note-off at 192 and the stand-ins; a second note-off @ 384 stays latched
     until a later pedal-up (the voice is still sounding one block later; it releases after the pedal-up).
   - `SECTION("ResetOnLoad")`: Pitch Bend 1.0 and pedal down, then `setState` + one block → `pitchBend() == 0.0f` and
     `sustainDownForTest() == false`.
   - `SECTION("StateIndependentOfPerformance")`: `getState` bytes equal with and without pedal down / bend 1.0.
   - Amend `lifecycle_test.cpp:523`: `IMidiMapping` query → `kResultOk` (FR-050 removes Phase 10 FR-019's exclusion);
     `INoteExpressionController` stays `kNoInterface`.
2. **Implement** controller: derive from `IMidiMapping`, `DEFINE_INTERFACES … DEF_INTERFACE(IMidiMapping) END_DEFINE_INTERFACES(EditControllerEx1)`,
   `REFCOUNT_METHODS(EditControllerEx1)` (Vorago `controller.h:105–108` precedent), `getMidiControllerAssignment` per FR-050; remove
   the `controller.h:10` exclusion comment. Processor: `latch_` (`Krate::Plugins::SustainLatch`), `PedalPoint`, `pedalPoints_`,
   `numPedalPoints_`, `collectPedalPoints` (Vorago `processor.cpp:803–829` semantics: every point, non-finite skipped, clamp,
   `down = value >= 0.5`, coalesce past 128), the slice-loop pedal cursor (events first at equal offsets; slice end = min(next event,
   next pedal point, total)), `applyPendingPedalPointsImmediately` on early returns after step 3, latch-aware `dispatchEvent`
   (`latch_.noteOn` then `engine_->noteOn`; note-off / velocity-0 → `if (latch_.noteOff(p)) engine_->noteOff(p)`), step 6
   `clearLatchPending_`, `setActive(false)` `releaseAll` before the reset, `setupProcessing` `clearWithoutRelease`;
   `sustainDownForTest`.

**Verified by:** G16 gate.

---

# GROUP 17 — Tail

## T022 — `getTailSamples` (FR-060; SC-017) — render arms run by the main loop

**Files edited:** `plugins/profundum/src/processor/processor.h`, `plugins/profundum/src/processor/processor.cpp`,
`plugins/profundum/tests/integration/tail_test.cpp`.

1. **Test first** — `tail_test.cpp` (deviation 2):
   - `TEST_CASE("Profundum_TailFormula", "[profundum][tail]")`: `getTailSamples() == 0` on an initialised, unprepared processor and
     again after `terminate()`. With macros neutral and release pairs {A 100, B 5000} and {A 5000, B 100} ms at 44.1 / 48 / 96 kHz:
     `getTailSamples() == uint32(std::ceil((5000.0 + 1250.0) * fs / 1000.0)) + 64`. Tight↔Loose (102) at 1.0: expected computed in
     the test from `ProfundumMacroMatrix::contributions()[AmpReleaseOct]` (stack-local matrix with the same macro values) and the
     float `R · exp2(c)` clamped to [0.1, 10000] → equality, and the value exceeds the neutral one (release pair {100, 100} so the
     clamp does not bite).
   - `TEST_CASE("Profundum_TailCoversRelease", "[profundum][tail][long]")`: 8 render arms (macros neutral / Tight↔Loose 1.0 × release
     pairs {100, 5000} / {5000, 100} × position 0 / 1; E1 MONO; 48 kHz; note held 0.5 s): after the note-off, every sample beyond
     `getTailSamples()` satisfies `== 0.0f` on both channels; on failure print the first non-zero index and value. **A non-zero sample
     is a failure reported to the user; the 1250 ms term is never re-fitted** (plan R3).
2. **Implement** plan §4.5.7 exactly (`tailSampleRate_` stored on successful `setupProcessing`, cleared in `terminate`; leaf 21
   named constant with a constexpr name check against `kLawTable[21].name == "ampEnv.releaseMs"`; the ±0 skip; float `exp2`;
   `detail::isFinite`; `+ kLimiterLookaheadSamples`). Remove the `processor.h:58` "NOT overridden" comment.

**Executor note:** the `[long]` case is not run by the gate. Return done with notes "deferred to the main loop at close: run
`profundum_tests.exe "Profundum_TailCoversRelease"` FIRST among the [long] cases (plan R3)".

**Verified by:** G17 gate (`Profundum_TailFormula`); the render case by T031.

---

# GROUP 18 — Audibility through the parameter path

## T023 — SC-018 descriptor port + `Profundum_ParamPathAudibility` + `Profundum_MassOffsetAudibility` (FR-064; `[long]`)

**Files:** create `plugins/profundum/tests/param_path_audibility_helpers.h`; edit
`plugins/profundum/tests/integration/param_path_audibility_test.cpp`.

1. **Port first** (plan §6.4): the descriptors of the 15 rows of spec SC-018's table, ported **verbatim** from the anonymous
   namespace of `dsp/tests/unit/systems/profundum_engine_audibility_test.cpp` (open it; the plan cites `auWindowESubDb` :464,
   `auLkOutShort` :784, AT-27 :1496–1508, AT-01 :1066–1074, AT-38 :1816, AT-42 :1913–1919, AT-43 :1994 — re-locate each row's
   descriptor and cite the line you read), each with a `// ported from …:<line>` comment, in namespace `ProfundumPluginTest`.
   Include read-only `profundum_voice_engine_test_helpers.h` and `bass_stereo_field_test_helpers.h` (qualified names only). The bars
   are a constexpr table transcribed from the CSV `bar` column (cite the CSV line per row); never re-derived. A helper symbol that is
   not in a header is ported, never edited in `dsp/`.
2. **Test first** — `TEST_CASE("Profundum_ParamPathAudibility", "[profundum][audibility][long]")`: one SECTION per spec SC-018 row
   (15), at every (row, note) pair whose CSV `gated` column reads `gated`:
   - base loaded through the host surface (`withEndpoints` + `setState`, or E-param writes under Edit Target A then B);
   - lever moved **only** by the normalised parameter change of the spec table (e.g. AT-11: 301 index 0 → 2, norm 0 → 2/3 with 302 = 1;
     AT-23: 412 0 → 200 ms with Voice Mode 1 = Mono; AT-43: 415 index 2 → 0 with 416 = Reset; AT-38: 900 Off → Cycle (norm 2/3) with
     901 = Shift, 902 = 1, 903 = 1, 904 = 0.1 Hz; AT-42: 105 0 → 0.5 with A = A_s, B = B);
   - descriptor read on the row's CSV `tap` through `setEngineObserverForTest` (attached before `setupProcessing`);
   - `REQUIRE(reading >= bar)`; `out_descriptor` computed on the processor output and recorded, not gated;
   - **fidelity teeth:** each row also evaluated engine-direct (a `ProfundumEngine` driven exactly as Phase 9 did); the port's
     reading must be within 10 % (relative) of the CSV `value` column, else FAIL "descriptor port drift" before the processor reading
     is trusted;
   - results appended to `specs/profundum-phase11-parameters/evidence/param-path-audibility.csv` (columns: row, note, descriptor,
     tap, phase9_value, engine_direct, param_path, bar, out_descriptor, pass). Result: **15 / 15 rows ≥ bar**.
   - `TEST_CASE("Profundum_MassOffsetAudibility", "[profundum][audibility][long]")` (SC-024 "every Morph position"): both endpoints
     Mass 0; Mod Wheel 0 → 1 only; the ported AT-27 descriptor on `dynL/R` and `sumL/R` ≥ its bar at Morph Position 0, 0.5 and 1,
     each settled; the case's wall clock printed.
3. **Executor note:** both cases are `[long]` and are not run by the gate. Return done with notes "deferred to the main loop at
   close: run `profundum_tests.exe "Profundum_ParamPathAudibility"` and `"Profundum_MassOffsetAudibility"` as separate processes;
   record the CSV and wall clocks in run-log T023.txt".

**Verified by:** compile at the G18 gate; the readings by T031.

---

# GROUP 19 — Mono sum through the host surface

## T024 — `Profundum_MonoSumViaParams` (FR-065; SC-019; `[long]`)

**Files edited:** `plugins/profundum/tests/integration/mono_sum_params_test.cpp`.

1. **Test (the phase's mono gate over code built in T015 / T016; non-vacuity arm below)** —
   `TEST_CASE("Profundum_MonoSumViaParams", "[profundum][mono][long]")`: grid Image Mode SOLID / SPREAD / WIDE × MONO / 4 voices, E1,
   A / B = `randomValidMorphState(0x119A)` / `(0x119B)`. ID 800 written under Edit Target A and again under B. Position ramped 0 → 0.5
   (selections stay A). Observer attached via `setEngineObserverForTest`. Assertions:
   - `Krate::DSP::StereoFieldTest::bandReading(L, R, n, fs, 0, f_c)` on the field-stage and output taps, gated as in Phase 9
     (`valid`, sub in band): `|Q| ≤ kSubBandQBarDb` (1.0 dB) and `ρ ≥ kSubBandRhoBar` (0.99);
   - SOLID: output L == R bitwise from (start of the block that set the mode + `ceil(10 · fs / 1000) + 64`) on;
   - every voice's `anchorL` == `anchorR` bitwise in every chunk, in every mode and both voice counts;
   - **non-vacuity arm**: in SPREAD with Spread 801 = 1, the output L ≠ R somewhere (so the L == R checks can fail).
2. **Executor note:** `[long]`; return done with notes "deferred to the main loop at close: run
   `profundum_tests.exe "Profundum_MonoSumViaParams"`".

**Verified by:** compile at the G19 gate; the readings by T031.

---

# GROUP 20 — Restored render and determinism

## T025 — `Profundum_StateRestoreRender` (SC-011, `[long]`) + `Profundum_Determinism` (FR-063; SC-022)

**Files edited:** `plugins/profundum/tests/integration/state_restore_render_test.cpp`,
`plugins/profundum/tests/integration/determinism_test.cpp`.

1. `TEST_CASE("Profundum_StateRestoreRender", "[profundum][state][long]")`: setup as SC-010 with endpoints
   `randomValidMorphState(0x11C, {motionSourceOffOrEnv, fieldMotionDepthZero})` / `(0x11D, …)`; every note released and
   `getTailSamples()` samples elapsed before `getState`; a fresh processor runs `setState` **before** `setupProcessing` /
   `setActive(true)`; both receive an identical stream starting with new note-ons (E1, MONO, held 2 s): `bandLevelDifferenceDb`
   (aggregate) over [100 ms, 2.1 s) after the first new note-on ≤ **0.1 dB**; the selection-mask byte (18) and Anchor byte (19) of
   `engineForTest()->serializeMorphRuntime` equal. **Control arm:** the stream's byte 490 forced to 0xFF → the restored mask byte
   reads 0x00 against the original's 0x0F (precondition: the original's is 0x0F).
2. `TEST_CASE("Profundum_Determinism", "[profundum][determinism]")`: two processors, identical state and stream (E1 MONO, Motion on,
   2 s) → per-sample max |diff| == **0**. Drift arm: 900 = Drift, 901 = Mass, 902 = 1, 903 = 1, E1 MONO, 4 s: seed changed through
   the state stream then `setActive(false)` / `setActive(true)` → frame-wise `bandLevelDifferenceDb` over [0.5 s, 4 s) ≥ **0.5 dB**;
   the same arm with the seed unchanged → exactly 0.0. Non-vacuity of the null arm: the changed-seed arm is the control. If this case
   exceeds ~15 s at its first gate run, report it (do not tag it `[long]` silently).
3. **Executor note:** SC-011 is `[long]`: return done with notes "deferred to the main loop at close: run
   `profundum_tests.exe "Profundum_StateRestoreRender"`".

**Verified by:** G20 gate (`Profundum_Determinism`); SC-011 by T031.

---

# GROUP 21 — Real-time safety

## T026 — `Profundum_Phase11PathsDoNotAllocate` (FR-062; SC-021)

**Files edited:** `plugins/profundum/tests/unit/phase11_alloc_test.cpp`.

1. `TEST_CASE("Profundum_Phase11PathsDoNotAllocate", "[profundum][rt]")`: containers built before the scope; a copy requested via
   `processor->notify` and a pending `setState` hand-off placed before the scope; then 64 blocks under `AllocationScope` exercising
   every FR-062 path: all 84 IDs routed (one point each, rotating), Edit Target switches, compare A → AB, the restore applied, a
   multi-point pedal queue, pitch bend, the Mass offset (6 / 7 moves), sync flags set (`flushControllerSync` **not** called inside
   the scope). `REQUIRE(count == 0)`. Non-vacuity: a `SECTION("Teeth")` in which `flushControllerSync()` is called inside a scope
   with a connected peer → count > 0 (proves the detector sees `IMessage` allocation).

**Verified by:** G21 gate.

---

# GROUP 22 — CPU case

## T027 — `Profundum_ProcessorCpuFullSurface` (FR-066; SC-020) — written here, run only by T033

**Files edited:** `plugins/profundum/tests/integration/processor_cpu_test.cpp`.

1. `TEST_CASE("Profundum_ProcessorCpuFullSurface", "[profundum][.perf][performance]")`, extending the Phase 10 interleaved harness
   (:54 onward; reuse `kFs`, `kBlock`, `kMonoPctBar`, `kPolyPctBar`, `kWrapperOverheadCeiling`): arms processor / engine-direct ×
   {MONO, POLY 4, MAX-MONO, MAX-POLY (all four macros 100–103 at 1.0)} with distinct A / B endpoints, position 0.5 and Motion on;
   plus the automation arm (every block `[1001 = k % 2, 200 = v_k]`, so `setEndpoint` runs every block) at neutral and at macros
   1.0. Bars: MONO ≤ 8 %, POLY ≤ 25 %, ratio ≤ 1.05× in every arm; every percentage printed with `WARN`. No observer attached.
   **Never relax a bar or shrink a workload.**
2. **Executor note:** hidden `[.perf]`; never run by the gate. Return done with notes "deferred to the main loop at close: T033".

**Verified by:** compile at the G22 gate; readings by T033.

---

# GROUP 23 — Docs

## T028 — Roadmap band map, plugin `CLAUDE.md`, `README.md`, `CHANGELOG.md` (FR-072, FR-023; SC-026)

**Files edited:** `specs/Profundum-roadmap.md`, `plugins/profundum/CLAUDE.md`, `plugins/profundum/README.md`,
`plugins/profundum/CHANGELOG.md`.

1. Roadmap line 781: `400–499 drop / note` → `400–499 drop / note behaviour / amp envelope`.
   **Check:** `grep -n "drop / note behaviour / amp envelope" specs/Profundum-roadmap.md` hits the Phase 10 band map line, and that
   line no longer reads drop-only (SC-026).
2. `plugins/profundum/CLAUDE.md`: band table (:32) row 400–499 → "Drop / note behaviour / amp envelope" with the new count; state v2
   (510 bytes, layout table); 84 parameters; `IMidiMapping` table; the new test cases and their tags (`[long]` ones named).
3. `README.md` parameter section: the band list; the target-relative automation note (FR-023: "host automation of a sound parameter
   writes the endpoint selected by Edit Target when it plays back"); Copy / Compare are controller operations (no UI until Phase 12);
   the Linux sync note per the FR-025 ruling.
4. `CHANGELOG.md`: bullets under the unreleased `## [0.1.0] - Unreleased` entry (line 8); **no version bump** (version.json stays
   0.1.0).

**Verified by:** the greps above, recorded in `T028.txt`. Docs-only: no build needed beyond the group gate.

---

# GROUP 24 — Integration (sequential)

## T029 — CMake registration (single task)

**Files edited:** `plugins/profundum/CMakeLists.txt` (and only fixes found by the audit).
- Add under `# Parameter Packs` (after `src/parameters/macro_params.h`): `sound_param_row.h`, `sound_param_table.h`,
  `core_params.h`, `anchor_params.h`, `note_params.h`, `fold_shape_params.h`, `character_params.h`, `dynamics_params.h`,
  `field_params.h`, `motion_params.h`, `morph_params.h`, `performance_params.h` (all `src/parameters/`). No new plugin `.cpp`.
- **Audit** (counts into `T029.txt`): each of those 12 paths `grep -c` = 1; `plugins/profundum/tests/CMakeLists.txt` names each of
  the 22 test TUs once in `add_executable`, the 12 Phase 11 TUs once in the `-fno-fast-math` block, `processor_cpu_test.cpp` in no
  flag block, the `dsp/tests/unit/systems` include once, `krate_test_pch` last; `plugins/shared/tests/CMakeLists.txt` names
  `test_sustain_latch.cpp` once; `plugins/shared/CMakeLists.txt` names `src/midi/sustain_latch.h` once. No `/wd`, `-Wno-` or
  `pragma warning` added anywhere (`git diff 565f195d -- '*.txt' '*.h' '*.cpp' | grep -cE "/wd|-Wno-|pragma warning"` = 0).

## T030 — Scope fence, ODR re-sweep, registration greps (FR-061, FR-070, FR-002; SC-026)

**Files created:** `specs/profundum-phase11-parameters/evidence/close/scope-fence.js`, run-log `T030.txt`.
- `scope-fence.js` (Node): the union of `git diff --name-only 565f195d` and `git status --porcelain --untracked-files=all` must
  match the allow-list `plugins/profundum/**`, `plugins/shared/src/midi/sustain_latch.h`, `plugins/shared/tests/test_sustain_latch.cpp`,
  `plugins/shared/CMakeLists.txt`, `plugins/shared/tests/CMakeLists.txt`, `plugins/vorago/src/processor/sustain_latch.h`,
  `specs/profundum-phase11-parameters/**`, `specs/Profundum-roadmap.md` (`.serena/project.yml` excluded by name). Any `dsp/` path
  is RED (FR-070). Prints offending paths and `SCOPE GREEN|RED`.
- Greps, each with expected count, into `T030.txt`: `class SustainLatch\b` over `dsp/ plugins/ tests/` = 1 (shared header);
  the ODR names of T001 step 3 each defined once (in `plugins/profundum/src/parameters/` or the test band metric);
  `grep -rnE "[0-9]+\.[0-9]+f?\s*,\s*[0-9]+\.[0-9]+f?" plugins/profundum/src/parameters/{core,anchor,fold_shape,character,dynamics,field,motion}_params.h`
  reviewed by hand: no morphed range literal (FR-002); SC-026 grep from T028.
- **Executor:** write and run both (seconds); quote outputs. A FAIL in a Phase 11 file is fixed here.

## T031 — Suites lane, pluginval, clang-tidy — **deferred to the main loop** (SC-001 Windows, SC-002, SC-003, SC-017 renders, SC-018, SC-019, SC-011, SC-023)

**Files created:** `specs/profundum-phase11-parameters/evidence/close/suites.sh`, `evidence/close/tidy.sh`.

**Lane scope (encoded 2026-10-18, Phase 6 ruling G9-4; spec SC-023).** Executables: `profundum_tests` (phase TUs + amended Phase 10
TUs), `shared_tests` and the two `sustain_test.cpp` cases of `vorago_tests` (consumers of the moved `SustainLatch`, FR-061; a pure
code move, user ruling 2026-10-22: the full `vorago_tests` suite is not run). Never every `[long]` case of an executable.

`suites.sh` runs detached and writes `evidence/run-log/close/status.txt` (one line per lane: name, exit code, wall clock):
1. **Change-set scope.** `git diff --name-only 565f195d -- dsp/include dsp/src tests/test_helpers` must print nothing (FR-070; RED
   otherwise, stage stops). `git diff --name-only 565f195d -- plugins/shared` (tracked, modified): expected only the two CMake lists.
   If any existing `plugins/shared/src` header is modified, the script writes its consumers to the status file and runs their
   per-push suites too; the expected set is shared + Vorago only.
2. One build of `Profundum Vorago profundum_tests shared_tests vorago_tests` with `-- /nr:false` to `build.log`; warning count
   (`warning C[0-9]|warning:`) must be 0 (SC-001). Wall clock recorded.
3. Concurrently, each to its own log with its wall clock:
   - (a) `profundum_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"` → last line `All tests passed` (SC-002);
   - (b) `shared_tests.exe "~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"` and
     `vorago_tests.exe "Vorago_SustainLatchUnit,Vorago_SustainLatch"` (the moved class's own cases, SC-002 / SC-023);
   - (c) the `[long]` cases of the **phase's own TUs only**: list them with
     `profundum_tests.exe --list-tests "[profundum][long]"` and keep exactly the Phase 11 names
     (`Profundum_TailCoversRelease` first, then `Profundum_ParamPathAudibility`, `Profundum_MassOffsetAudibility`,
     `Profundum_MonoSumViaParams`, `Profundum_StateRestoreRender`); a listed name outside that set is reported, not run. One process
     per case via `xargs -P <min(5, cores − 4)>`;
   - (d) `--list-tests` shows every SC case name of spec §SC (SC-002), and `"[.perf]" --list-tests` shows
     `Profundum_ProcessorCpuFullSurface`;
   - (e) `tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Profundum.vst3` and the same on
     `Vorago.vst3` → exit 0 (SC-003).
4. A summary block with every lane's verdict and wall clock.

`tidy.sh`: reconfigure `build/windows-ninja` inside vcvars64 (test CMake changed; stale-database hazard), confirm the new TUs are in
`compile_commands.json`, then `./tools/run-clang-tidy.ps1 -Target profundum`, `-Target shared` and `-Target vorago` with
`-BuildDir build/windows-ninja`, each to its own log under a 900 s wall-clock ceiling; last line `TIDY GREEN|RED` (0 warnings).

**Executor:** write both scripts, `bash -n` each, and return done with notes "deferred to the main loop at close: run `suites.sh`
detached and `tidy.sh` concurrently; quote each lane's last lines into `evidence/run-log/T031.txt`; a Phase 11 red is owned (fix,
re-run only the affected case or lane); a SC-017 non-zero sample goes to the user (R3)". A timing-budget assertion of an untouched
case that fails under the concurrent lanes is recorded as load, not re-run.

## T032 — Portability (SC-001)

**Files created:** `specs/profundum-phase11-parameters/evidence/close/portability.sh`, run-log `T032.txt`.
1. The `.cpp` list = `git diff --name-only 565f195d` ∪ untracked, filtered to `\.cpp$` (expected: `plugins/profundum/src/processor/processor.cpp`,
   `src/controller/controller.cpp`, the 12 new TUs, the amended `unit/lifecycle_test.cpp`, `unit/state_roundtrip_test.cpp`,
   `unit/param_denorm_test.cpp`, `integration/param_flow_test.cpp`, `integration/processor_cpu_test.cpp`, and
   `plugins/shared/tests/test_sustain_latch.cpp`). `node tools/check-portability.js <files…>` with a 300 s ceiling; required:
   `check-portability: all clear -- N compiled.` with **no** skipped suffix and N equal to the list length.
2. `node tools/lint-apple-globals.js` over the new headers and TUs (re-read its usage line first).
3. `wsl --shutdown`.
Output to `evidence/run-log/close/portability.log`, last line `PORTABILITY GREEN|RED`.
**Executor:** write and run once. If it fails with `wsl: Failed to translate` or exceeds 5 minutes, stop it and return done with
notes "deferred to the main loop at close: run `bash specs/profundum-phase11-parameters/evidence/close/portability.sh` unsandboxed".

## T033 — CPU lane, alone (SC-020) — **deferred to the main loop**

- Precondition: T031 and T032 lanes finished; the machine idle.
- **Executor:** return done with notes "deferred to the main loop at close: run `node tools/run-cpu-tests.js profundum_tests` last and
  alone; quote every arm's percentage and ratio (MONO, POLY, MONO-MAX, POLY-MAX, both automation arms) into `evidence/run-log/T033.txt`".
- A red reading is re-run once after the machine has idled; still red → a defect reported to the user. No bar relaxed, no workload
  shrunk.

---

## Close procedure (main loop, in this order)

1. G1: if T001 is `blocked`, take the FR-025 / FR-041 / FR-006 rulings into the spec rows, then resume with `unblock: ["T001"]`.
2. G2 – G23 gates as the workflow runs them.
3. T031 `suites.sh` detached + `tidy.sh` concurrently (and `portability.sh` if T032 deferred it); `Profundum_TailCoversRelease`
   first among the `[long]` shards (R3).
4. Any red → triage into "Defects found", fix, re-run only the affected case or lane.
5. T033: the CPU lane, alone, after the machine has idled.
6. Comply quotes the run-log files. SC-004 (`auval`) and the macOS / Linux legs of SC-001 are **PENDING-CI** until the user pushes
   and a run ID exists.

## Open questions carried from the plan (built at the plan's reading; surface, do not decide)

- **Plan OQ-2:** RULED 2026-10-22 — pull on Edit Target switch + 50 ms timer on Windows / macOS (FR-025, built in T016 / T020).
- **Plan §10 FR-041 / FR-006 amendments:** RULED 2026-10-22 — both accepted and written into the rows.
- **Plan OQ-1:** RULED 2026-10-22 — synthesized blob `p_a = curve(mode, position)` (FR-041, T017).

## Defects found

(Empty at task-cut time. Gates, fixers and the main loop record entries here: task, case / section, verbatim reading, and triage.)

- **G3 gate, T003, `Profundum_BandLevelMetricSelfTest` / `FrameWisePeaks`** (2026-10-22). Reading: `REQUIRE( aggregate < 6.0 )` with
  `65.2940192647 < 6.0`, message `frame-wise = 77.2875 dB, aggregate = 65.294 dB`. **Triage: test-stimulus defect, metric correct.**
  A diagnostic dump of `bandLevels` showed M band 1 (the 100 Hz band) moving only -18.15 -> -15.91 dB (2.24 dB, the expected
  10·log10(1.75)), while M bands 0/2/3/4 moved -78.0 -> -50.7, -101.4 -> -61.2, -129.0 -> -71.8, -155.5 -> -90.2 dB: the hard x2 gain
  step at sample 72000 (a slope discontinuity) splatters broadband energy 40-65 dB above the Hann-leakage floor of render 1, and
  those bands sit above the -100 dB exclusion floor, so the aggregate reports 65 dB. The metric follows spec Conventions / plan §6.3
  exactly; T003 does not specify the transition shape. **Fix:** the test reaches x2 through a 0.25 s raised-cosine ramp ending at
  the start of the last 0.5 s, so the last 0.5 s is still fully x2 and no bound changed. Reading after: `frame-wise = 8.26605 dB,
  aggregate = 3.40089 dB`; `profundum_tests [profundum]` 9/9 cases pass. Downstream note for SC-022 / SC-014 authors: any hard gain
  or parameter step in a stimulus will read as a large out-of-band difference under this metric.

- **G17 gate, T022, `Profundum_TailFormula` / `Tight <-> Loose 1.0 lengthens the tail by the macro law`** (2026-10-22). Reading:
  `CHECK( neutralTail == formulaTail(100.0, fs) )` with `59599 == 59600` (44.1 kHz), `64864 == 64865` (48 kHz), `129664 == 129665`
  (96 kHz). **Triage: test-oracle defect under MSVC /fp:fast; the processor was right.** The exact formula is
  ceil(1350 · fs / 1000) + 64 = 59599 / 64864 / 129664. With the constant 100.0 inlined, /fp:fast folds (100 + 1250) · 0.001 into
  1.35 and rounds 1.35 · fs one ulp above the integer, so ceil() adds one sample (a node replica of that order reproduces +1 at all
  three rates; every other order is exact). **Fix:** `formulaTail` (tail_test.cpp) and `getTailSamples` (processor.cpp,
  `ceilMsToSamples`) both correct the quotient against q · 1000, which is exact, so neither depends on how the compiler
  reassociates the division; the neutral section's inline formula now calls `formulaTail`. No bound or expectation changed.
  Reading after: `profundum_tests [profundum]` 25/25 cases pass.
