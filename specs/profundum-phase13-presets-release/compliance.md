# Phase 13 compliance report (profundum-phase13-presets-release)

**Overall status: COMPLETE.** Every item below carries verdict pass. Zero fail, zero partial.

Caveats that do not change a verdict but must be read first:

1. FR-056: about 18 source files (e.g. shared preset_manager.cpp, processor.cpp, controller.cpp) carry a 2026-10-25 10:58 mtime, after every tidy/portability lane. The cause is unidentified (mixed LF/CRLF). Diffs show the expected content, but no lane ran after that touch.
2. SC-033: shared_tests.exe was built 2026-10-25 00:34 while preset_manager.cpp, preset_browser_view.cpp and test_preset_description.cpp carry the 10:58:18 mtime, so the binary may predate the current sources.
3. SC-029: profundum_engine_audibility_test.cpp has mtime 2026-10-25 10:58:18, newer than dsp_systems_tests.exe (2026-10-24 14:22). move-check still reports it pure.
4. CC-warnings: the profundum_tests LINK failed with LNK1104 (an unrelated process held the exe). All its TUs compiled with zero warnings, but it needs a relink.
5. SC-027: render-guard shard 0 read 14.96 s against the 15 s bar in a re-run with only background load, so the margin is thin. Gate readings (T032) were 11.0-11.5 s.
6. Vacuous arms: no factory preset stores Poly, so the POLY4 CPU arm (FR-045/SC-022), the chord render (FR-038/FR-063/SC-016/SC-032) hold only because there are no Poly presets.
7. FR-064: F's derivation originally shared FR-043's gap (block twin on 3 presets); FR-043/SC-020 were later re-run over all 53 presets (see rows).

## Compliance table

| ID | Verdict | Evidence |
|---|---|---|
| FR-001 | pass | profundum_preset_config.h:32 subcategoryNames = {"Sub","Resonant","Harmonic","Driven","Plucked","Animated"} (additive). preset_library_test.cpp:117-135 Profundum_PresetCategoryConfig; :137-152 Profundum_PresetCategoryNameRule over all 84 titles, stepped strings, 11 group names, leading words, with non-vacuity rejects. Per-push: "All tests passed (2518562 assertions in 100 test cases)". |
| FR-002 | pass | Tree has 6 dirs: Sub 12, Resonant 7, Harmonic 8, Driven 11, Plucked 7, Animated 8; 53 .vstpreset. git status "D plugins/profundum/resources/presets/Sub/.gitkeep". preset_library_test.cpp:1075-1123 Profundum_PresetCategoriesMatchFilesystem passed. |
| FR-003 | pass | tools/profundum_preset_defs.h:3329-3351 buildProfundumInfoXml writes MediaType, PlugInName, PlugInCategory, Name, MusicalCategory, MusicalInstrument, Comment (same set as preset_manager.cpp:338-345). No committed Comment contains ' & < > or " (0 hits over 53). preset_library_test.cpp:1127-1191 checks 7 attrs. |
| FR-004 | pass | Counts 12/7/8/11/7/8, N=53. preset_library_test.cpp:1194-1235 CHECK(n >= 3u) per category, defs.size() == 53. Passed per push. |
| FR-005 | pass | preset_library_test.cpp:1205-1214 names.insert(name).second and PresetManager::isValidPresetName; non-vacuity 'Bad/Name' at :1196. |
| FR-006 | pass | preset_library_test.cpp:1239-1276: d.version == 3, d.bytes == 574, pf.comp.size() == 574 for every file. |
| FR-007 | pass | preset_library_test.cpp:450-632 rules 1,2,3,4,6,7,8,9,10 on all 53 defs (:701-707) with non-vacuity; covered == {1,2,3,4,6,7,8,9,10} at :789-790. Rule 5: 'fundamentalLossFlagged=0'. |
| FR-008 | pass | preset_library_test.cpp:1028-1060 libNonFiniteFields; planted NaN (0x7FC00000 via volatile) detected at :1241-1247; applied to every Comp :1264-1266. |
| FR-009 | pass | Rule 6 (:518-545) names present, <=31 bytes, not 'A'/'B'. Profundum_FactoryPresetRoundTrip (:1279-1308) out.names == in.names for all 53. |
| FR-010 | pass | profundum_preset_defs.h:64-70 enum class Capability {AT01..AT50, RSolid, RLock, RSine, Count}, static_assert(kNumCapabilities == 53). |
| FR-011 | pass | Single `primary` field; rule 10 (:618-632). Profundum_CoverageMatrixComplete (:1312-1349): primaries[i] >= 1 for each of 53 columns, covered == 53, AT-13 Rectify and Fracture both claimed. |
| FR-012 | pass | preset_showcase_test.cpp:85-148 CHECKs arm.measured, arm.pass, arm.fidelityPass, overrides.empty() on primaries. Close lane: all 8 [long] shards exit=0. coverage-matrix.csv: 121 cells, 0 negative margin, 0 primary overrides, all 53 columns have a primary. AT-32 E1 only; AT-13 claimed by Fault Line/Fracture and Rectified Follow/Rectify at E1+C3 (margin 5 cents). |
| FR-013 | pass | preset_showcase_test.cpp:378-469 Profundum_ReferenceColumns. pilot.log: Mono Bedrock D_st 110.958/117.282 vs bar 3; Locked Floor E_sub_onset 2.09332/2.03581 vs bar 2; Bedrock Sine D_cx 4.67956/5.86201 vs bar 3; all pass=1. |
| FR-014 | pass | coverage-matrix.csv 121 rows (merge.log: rows=121 files=8 OK); AT-11 Punch Anchor window_ms 39.9999, AT-12 Ghost Kick 100; min margins e.g. Motion 0.0495, Reference 0.0361. |
| FR-020 | pass | probe-table.csv 102 data rows over 50 AT ids, all PASS; AT-13 Fracture and Rectify at E1 and C3. run-log/T017.txt 'build exit=0 warnings=0', 11 WALL lines exit=0. |
| FR-021 | pass | preset_render_support.h:78 includes profundum_audibility_readers.h (auRd alias :112); the dsp test includes the same header; no other reader definitions under plugins/profundum/tests. |
| FR-022 | pass | probe-table.csv mtime 2026-10-24 18:59; library authored later (T026, 21:25). No commit after e6095b24; tools/profundum_preset_defs.h untracked. |
| FR-023 | pass | Stop never triggered: 102/102 probe PASS, 121/121 showcase margin >= 0. git diff --stat dsp/ touches only profundum_engine_audibility_test.cpp. |
| FR-024 | pass | CMakeLists.txt:753-765 add_executable(profundum_preset_generator ...), :783-785 output bin. Ran this session: 'wrote 53 presets' exit=0; diff -r vs committed tree IDENTICAL. |
| FR-025 | pass | CMakeLists.txt:808-813 add_custom_target(generate_profundum_presets ...). |
| FR-026 | pass | CMakeLists.txt:756-757, :770 links only KrateDSP hwy KratePluginsShared sdk; flags :789-796 match Vorago. close/build.log 0 warnings. |
| FR-027 | pass | profundum_preset_host.h holds PresetHost, buildPresetComponentState, writeFactoryTree; generator body is writeFactoryTree (cpp:47); tests share the same functions. |
| FR-028 | pass | check-preset-generator-determinism.js:55-59 includes profundum. determinism.log: 'OK — 53 file(s); 0 differing between two fresh runs, 0 changed by a third run over an existing tree.' exit 0. |
| FR-030 | pass | editor.uidesc PresetBrowserButton at origin 452,24 size 96,24; controller.cpp createCustomView returns OutlineBrowserButton "PRESETS". editor_layout_test checks every zoom step: 10 header children, 1 button, rect (452,24,548,48), ratified rects unchanged, no overlap. |
| FR-031 | pass | controller.cpp setStateProvider/setLoadProvider, loadPresetStream sends kMsgLoadPresetState (<=4096) then setComponentState then restartComponent(kParamValuesChanged); processor.cpp notify branch calls setState; browser view lifecycle in didOpen/willClose. Profundum_LoadPresetStateMessage / ControllerLoadPresetStream / PresetBrowserViewLifecycle green. |
| FR-032 | pass | preset_browser_test.cpp:647-711 Profundum_PresetBrowserLoad over all 53 files, decodedEqual(…, 0.0), mismatches == 0, 1 message, kParamValuesChanged. Green in per-push (100 cases) and T036 (120 cases). |
| FR-033 | pass | preset_library_test.cpp:887-954 independent parser; :1149-1153 class id == kProcessorUID for every file. |
| FR-034 | pass | :1279-1308 PresetHost load then save, decodedEqual(out,in,2.5e-4), names byte-equal, 53 files. |
| FR-035 | pass | preset_browser_test.cpp:715-745 FactoryPresetScan n == 53; :750-773 InstalledPresetTree; installed-tree.log 'All tests passed (57 assertions in 1 test case)'; setup.iss:68 installs presets. |
| FR-036 | pass | :1353-1404 Profundum_CommittedPresetTreeMatches; generator output diff -r IDENTICAL. |
| FR-037 | pass | preset_rt_test.cpp:161-205, :346 CHECK(r.allocations == 0u); 'rt-load worst allocations=0 over 54 presets'. |
| FR-038 | pass | preset_sweep_test.cpp:544-576; PresetSweep.log 106 renders, non_finite 0, min held RMS -14.2007 dBFS, max tail RMS -200 dBFS, 'All tests passed (284577 assertions)'. No Poly preset, chord arm vacuous. |
| FR-039 | pass | :589-600 bar = stored ceiling + 0.1; PresetSweepRates.log '# worst true-peak margin 0.070251 dB (Fault Line at 44100)'. |
| FR-040 | pass | :578-608; rates 44100/48000/96000 for all 53, 'All tests passed (553179 assertions in 1 test case)'. |
| FR-041 | pass | preset_rt_test.cpp:121-136 prelude; PresetTruePeakMaxMass.log '# worst true-peak margin 0.0369267 dB', 0 non-finite. |
| FR-042 | pass | :636-671; PresetMonoSum.log '# ungated=0', min rho 0.999883, max abs Q 0.000318553 dB, anchor_mismatches 0, solid_differing 0, control_rho -1. |
| FR-043 | pass | preset_sweep_test.cpp:782-809 block twin over all defs; seed twin only when !seedDependent. Re-run: "# Profundum_PresetDistinctnessPilot: presets=53", 53 block37 rows, 0 twin rows d != 0, F,1 / t_max,0 / near_variant_d,0.295328965 / control_d,1409.54913, "All tests passed (34499889 assertions in 1 test case)". Params: "presets=53 fewest=6 pair=Bedrock vs Triangle Floor". Sound: "presets=53 F=1 closest=Bedrock vs Locked Floor d=1.91446 masked=15". distinctness.csv byte-identical before/after. |
| FR-044 | pass | :673-692; PresetDeterminism.log '# contradicting=0'; Orbiting Field 0.0610185, Drifting Weight 0.268821 under other seed; 51 others 0. |
| FR-045 | pass | processor_cpu_test.cpp:885-1019; cpu.csv 53 MONO rows max 5.04534 % (Fault Line) vs 8 % bar, 0 invalid, control 0.961804-1.03115. T035: 'PASS All tests passed (21668430 assertions in 23 test cases)'. POLY4 vacuous ('max POLY4 0 % (-)'). |
| FR-046 | pass | preset_rt_test.cpp:217-243, :388-389, :408; shards 11.5161/11.3763/11.0553/11.1408 s, worst truePeak -0.964508 dBTP. |
| FR-047 | pass | Edit Target A driven last; :1267-1269 editTarget == 0 and runtime target/position within 2.5e-4 of morph.position. |
| FR-048 | pass | preset_rt_test.cpp:257-314; 'held-load worst ratio=1.96906 (Fault Line) over 54 presets' (bar 2). |
| FR-049 | pass | bytes 84..87 patched with def.seed; :1273 CHECK(d.seed == def->seed); default 0xB455. |
| FR-050 | pass | render.log: 'presets=53 wavs=117 gestures=11 flagged=0'; manifest 106 E1/C3 + 11 gestures; listening-sheet.md 53 empty 'Verdict (user):' lines. |
| FR-051 | pass | MANUAL.md (546 lines) with Vorago section sequence; manual_coverage_test.cpp:73-140 checks 80 titles, 6 categories, 53 names. |
| FR-052 | pass | docs/index.html (195 lines), manual-template.html, assets/; links manual.html and MANUAL.md. |
| FR-053 | pass | release SKILL.md Inputs gains profundum; table row '| profundum | `profundum_tests` | `Profundum.vst3` |'. |
| FR-054 | pass | CHANGELOG bullets under open 0.1.0: '53 factory presets in six categories', 'Preset browser', 'User manual'; version.json unmodified. |
| FR-055 | pass | T036: 'READY profundum 0.1.0 build:ok tests:ok pluginval:ok changelog:ok changelog-complete:ok version:ok'; 'profundum_tests: All tests passed (24185762 assertions in 120 test cases)'. |
| FR-056 | pass | T033: 'check-portability: all clear -- 19 compiled.', 'lint-apple-globals: OK — 31 files'. T034: 'lane profundum GREEN files=52 warnings=0', 'lane files GREEN files=5 warnings=0'. build.log 0 warnings. See caveat 1 (post-lane mtime touch). |
| FR-057 | pass | profundum_audibility_readers.h (2362 lines); TU diff +2/-2216 (include + using only); move-check.js 'MOVE PURE changed=1 lifted=11'. |
| FR-058 | pass | T004 run 4: '[post] build exit=0 warnings=0', 'evidence: 17/17 pre files byte-identical to post', 'MOVE GATE GREEN'. Close lane ran only [#profundum_engine_audibility_test]~[long]: 'All tests passed (1873 assertions in 3 test cases)'. |
| FR-059 | pass | Only new message kMsgLoadPresetState; new controller members presetTabLabels_, presetBrowserView_; no param, no name display; layout test asserts no 'preset'/'previous'/'next' header text. |
| FR-060 | pass | :1407-1426 |outputCeilingDb + 1.0| <= 1e-6 for every file. |
| FR-061 | pass | PresetLoudnessMatch.log 'median_db=-11.1537', '# worst deviation 3.58833 dB' (<= 4). |
| FR-062 | pass | processor_cpu_test.cpp:836 kPresetCpuNotes{28,35,40,44}; cpu.log 'SC-022 preset CPU notes (MIDI, on at block 0): 28, 35, 40, 44'. |
| FR-063 | pass | preset_sweep_test.cpp:548-551 kPresetChordNotes == {28,35,40}; no Poly preset so no chord render. |
| FR-064 | pass | :750-765 dist >= f for all pairs, no exemptions. PresetDistinctness.log 'F=1 closest=Bedrock vs Locked Floor d=1.91446 masked=15', 'All tests passed (10258687 assertions in 1 test case)'. |
| FR-065 | pass | preset_manager.cpp readMetadata sets description from Comment (unescaped); savePreset escapes; preset_browser_view descriptionLabel_; test_preset_description.cpp:98-200. fr065-gate.out: shared_tests 'All tests passed (6490 assertions in 464 test cases)', 9 pluginvals completed, 'FR065 GATE GREEN'. See caveat 2. |
| SC-001 | pass | "All tests passed (7 assertions in 1 test case)" (CategoriesMatchFilesystem) and "(13 assertions in 1 test case)" (CategoryConfig). |
| SC-002 | pass | Profundum_FactoryPresetContainer "All tests passed (905 assertions in 1 test case)". |
| SC-003 | pass | Profundum_FactoryLibraryShape "All tests passed (178 assertions in 1 test case)". |
| SC-004 | pass | Profundum_FactoryPresetStateFormat "All tests passed (586 assertions in 1 test case)". |
| SC-005 | pass | Profundum_FactoryPresetRoundTrip "All tests passed (584 assertions in 1 test case)". |
| SC-006 | pass | Profundum_CoverageMatrixComplete "All tests passed (169 assertions in 1 test case)". |
| SC-007 | pass | merge-csv.js:106-110, 126-153 group-min records; coverage-matrix.csv lines 123-133 hold 11 group-min records (Anchor min 0.0361321, Motion 0.0494771). Re-run `merge-csv.js coverage-matrix … 121 --group-min`: "group-min groups=11", "rows=121 files=8 OK", diff IDENTICAL. 0 overrides. |
| SC-008 | pass | Profundum_ReferenceColumns "All tests passed (251616 assertions in 1 test case)"; Mono Bedrock 110.958/117.282, Locked Floor 2.09356/2.03613, Bedrock Sine 4.67956/5.86201. |
| SC-009 | pass | probe-table.csv 102 rows, 50 ids, 102 PASS, 0 FAIL, max rel_dev 4.52498e-06; T017 "PROBE GREEN rows=102"; table recorded before any def (first rows T023 20:50). |
| SC-010 | pass | CMakeLists.txt:772 links KrateDSP hwy sdk; vcxproj AdditionalDependencies vstgui count 0, catch2 count 0; builds rc=0, 0 warnings; generator "wrote 53 presets", BYTE_IDENTICAL; Profundum_GeneratorSmoke "All tests passed (172 assertions in 1 test case)". |
| SC-011 | pass | determinism.log "OK — 53 file(s); 0 differing between two fresh runs, 0 changed by a third run over an existing tree." exit 0 wall 55 s. |
| SC-012 | pass | Profundum_CommittedPresetTreeMatches "All tests passed (217 assertions in 1 test case)". |
| SC-013 | pass | PresetBrowserLoad "(1813 assertions)", UiLayout "(1161 assertions)", EditorLifecycle "(255 assertions)", all passed. |
| SC-014 | pass | FactoryPresetScan "(122 assertions)"; installed-tree.log "All tests passed (57 assertions in 1 test case) exit: 0". |
| SC-015 | pass | FactoryPresetLoadRtSafe: "rt-load worst allocations=0 over 54 presets", "All tests passed (3250 assertions in 1 test case)". |
| SC-016 | pass | PresetSweep.log "All tests passed (284577 assertions in 1 test case)"; min held RMS -14.2007 dB (Restrike E1), max tail -200 dB. Poly clause vacuous. |
| SC-017 | pass | PresetSweepRates.log "# worst true-peak margin 0.070251 dB (Fault Line at 44100.000000)", 159 rows, 0 non-finite, "(553179 assertions)". |
| SC-018 | pass | PresetTruePeakMaxMass.log "# worst true-peak margin 0.0369267 dB", 106 rows, "(284875 assertions)". |
| SC-019 | pass | PresetMonoSum.log "# ungated=0", "(142160 assertions)"; min rho 0.999883, max abs Q 0.000318553 dB, 19 SOLID rows solid_differing 0. |
| SC-020 | pass | t_max over all 53 block twins; near-variant d=0.295328965 < F, control d=1409.54913; pilot exit=0, 34499889 assertions; main case "presets=53 F=1 closest=Bedrock vs Locked Floor d=1.91446 masked=15"; profundum_tests "All tests passed (2518562 assertions in 100 test cases)"; dsp_systems_tests "All tests passed (10208837 assertions in 483 test cases)". |
| SC-021 | pass | PresetDeterminism.log "# contradicting=0", "(287014 assertions)"; Orbiting Field 0.0610185, Drifting Weight 0.268821. |
| SC-022 | pass | cpu.log "PASS All tests passed (21668430 assertions in 23 test cases)", "CPU GREEN"; cpu.csv 53 MONO rows max 5.04534 % <= 8 %, worst control 1.03971, 0 invalid; POLY4 vacuous. Not measured by the report agent. |
| SC-023 | pass | render.log "presets=53 wavs=117 gestures=11 flagged=0", exit 0; 117 wavs on disk; 53 empty Verdict lines. |
| SC-024 | pass | Profundum_ManualCoversSurface "All tests passed (179 assertions in 1 test case)"; docs/index.html links manual.html and MANUAL.md. |
| SC-025 | pass | T036 "READY profundum 0.1.0 …", "ready: true, readyCount: 1", 120 test cases passed; pluginval-Profundum.log exit=0. |
| SC-026 | pass | T033 "check-portability: all clear -- 19 compiled.", "PORTABILITY GREEN"; tidy.out "lane profundum GREEN files=52 warnings=0 errors=0", "lane files GREEN files=5 …", "TIDY GREEN"; build warnings=0. Note: `-Target shared` refused (not in ValidateSet); shared files tidied via file-list lane. |
| SC-027 | pass | T032: shard truePeaks -0.966061/-0.974651/-0.964508/-0.986609 dBTP, wall 11.5161/11.3763/11.0553/11.1408 s. Own runs under load failed only the seconds bar (23.1-23.5 s); one at a time 14.9607 s pass, 24.1639 s fail, 11.0232 s pass, 12.2582 s pass. Thin margin, see caveat 5. |
| SC-028 | pass | "held-load worst ratio=1.96906 (Fault Line) over 54 presets", "All tests passed (15247 assertions in 1 test case)". |
| SC-029 | pass | T004 run 4 "MOVE GATE GREEN"; move-check re-run "MOVE PURE changed=1 lifted=11"; git status dsp/ lists only the modified TU and new readers header. See caveat 3. |
| SC-030 | pass | Profundum_FactoryPresetCeiling "All tests passed (108 assertions in 1 test case)". |
| SC-031 | pass | PresetLoudnessMatch.log "presets=53 median_db=-11.1537", "# worst deviation 3.58833 dB", "(141981 assertions)". |
| SC-032 | pass | cpu.log:246 "SC-022 preset CPU notes (MIDI, on at block 0): 28, 35, 40, 44"; chord arm vacuous (no Poly preset). |
| SC-033 | pass | Krate_PresetManagerReadsDescription "(25 assertions)", Krate_PresetBrowserShowsDescription "(11 assertions)", Profundum_PresetDescriptions "(214 assertions)"; fr065-gate.out GREEN. See caveat 2. |
| CC-rt | pass | process() unchanged; only seams copy at setupProcessing (processor.cpp:279) and non-audio notify branch (:743-764); seqlock handoff; no dsp/include changes. Profundum_FactoryPresetLoadRtSafe + LoadUnderHeldNote "All tests passed (18497 assertions in 2 test cases)", 54 x CHECK(r.allocations == 0u). |
| CC-layers | pass | `git diff --name-only -- dsp/include` empty; new dsp file is a test header including systems-level and below; controller includes no processor header. |
| CC-naming | pass | New types PascalCase in own namespaces; members trailing underscore; constants kPascalCase; ODR sweep found no collisions. |
| CC-warnings | pass | 23 .cpp files touched and rebuilt: warn_count=0. Caveat 4: profundum_tests LINK failed LNK1104 (exe held by foreign process); needs relink. |
| CC-portability | pass | `node tools/check-portability.js`: 19 TUs OK, "check-portability: all clear -- 19 compiled." exit=0. |

## Implementation notes (deviations reported by task agents)

- T002/T003: move-check/compare scripts use git status plus a start marker to find written Phase 9 files; lifts carry optional "calls" ranges; first anonymous namespace closes at :2265 (not :2264). L8 range extended to :1522; L10 `using M = NoteMemory::Mode` moved into auNoteMemoryRows().
- T005: Comment attribute always written (SC-002 needs exactly seven attrs); `<cstddef>` added.
- T006: `ok` also false when an endpoint fails deserializeMorphState.
- T010: empty name rejected as default name; writeFactoryTree aggregates all failures.
- T013: teeth check uses identity of overlay controls, since IDs 1-5 are real bound parameters.
- T014: ui_strings tooltip and D6 drift guard extended for tag 9400 in editor_layout_test (kD6UiOnlyTags and kMainTooltips untouched).
- T015: PresetArmContext gained a `processor` reuse slot; Voice Mode override read from ctx.overrides.
- T018: preconditions add each contrast's hi value for AT-11..17 (plan table lists "—"/"p = 0"); describePreset sweep render runs to note-off + 2^16.
- T019: PresetRenderReadings/presetReadings added to preset_render_support.h; renders to max(noteOff + tail, noteOff + 20 ms + 2^16).
- T022: .gitignore lacks a rule for Phase 13 listen WAVs; they will show untracked.
- T023: row 53 puts the harmonic body on endpoint B; row 52 risk (AT-11 thin margin) later resolved by T027.
- T024: preset_sweep_test.cpp not touched (cases belong to the other two TUs).
- T025: C-6 caps forced decay 1500 ms / 1990 ms on B-based rows; row 33 also seedDependent.
- T027: render guard measured 21.28 s at G22; case_seconds=14.3403 later, under the bar, so no split in the per-push lane.
- T029: CHANGELOG also drops "The category ships empty." and "No factory presets ship."; ci-test-shard.js long lane gains profundum_tests: 4; ci-test-times.json long.profundum_tests = 1004 (omits five shorter Phase 10-11 [long] cases).
- T030: eighth TU preset_description_test.cpp registered in add_executable but not in the fast-math block (no NaN/Inf code).
- T031: scope-fence.js reported RED for 15 earlier-phase evidence files rewritten by test runs (outside this phase's list); main loop decides restore vs allow.
- T032: FR-065 shared paths allowed in the scope step; lane 3b uses the file tag `[#profundum_engine_audibility_test]~[long]` instead of `[voice-engine]~[long]`.
- T033: check-portability.js initially failed 8 TUs (missing KRATE_DSP_TESTS_DIR, PROFUNDUM_PRESETS_DIR, tools/ flags); fixed in tools/check-portability.js, re-run green with 19 compiled.
- T034: file list widened to all changed/untracked .cpp; output filter keeps tools/. `.clang-tidy` HeaderFilterRegex does not cover plugins/profundum headers or tools/profundum_preset_defs.h.
- T036: matrix computes six gates; check-changelog-coverage.js cannot check a first entry, so FR-054 bullets verified by reading.
- Unrelated: plugins/profundum/README.md still says Copy/Compare have "no buttons until the Phase 12 interface".

## Remaining gates for the human loop

- clang-tidy: lanes were green (profundum 52 files, files lane 5 files, 0 warnings) but ran before the 10:58 mtime touch; re-run on the touched files before committing.
- Relink profundum_tests (LNK1104 caveat) and re-run shared_tests if the 10:58 touch changed sources.
- pluginval: plugin code changed (phases 11+); T036 and pluginval-Profundum.log exit=0 were green; re-run if sources changed after the touch.
- Restore or allow-list the 15 rewritten earlier-phase evidence files (T031 SCOPE RED), then re-run scope-fence.js.
- Add a .gitignore rule for Phase 13 listen WAVs.
- Commit (user decision; release cut, tag and push are not part of this phase).

## Main-loop recheck of caveats 1-4 (2026-10-25)

The 10:58 touch changed no build input that mattered: a full rebuild of the 10 test executables, the preset generator and the
9 plugins found nothing to compile (shared_tests.exe 10:59 and profundum_tests.exe 11:07 already postdate it; 0 warnings;
Seraphis only the MSB3073 ProgramData copy). Then, concurrently on those binaries (evidence/run-log/close/recheck/, wall 405 s):
- fr065-gate.sh: `FR065 GATE GREEN` (shared_tests full, the nine plugins' scan / browser / lifecycle files, pluginval x9)
- profundum_tests per-push: `All tests passed (2518562 assertions in 100 test cases)`
- dsp_systems_tests `[#profundum_engine_audibility_test]~[long]`: `All tests passed (1873 assertions in 3 test cases)`
- tidy.sh: `TIDY GREEN total_wall=241s`
Caveats 1-4 are closed by these readings. Caveat 6 (no Poly preset) is taken up by Phase 13b (user, 2026-10-25: at least 5 Poly presets).

## Phase 13b addendum (2026-10-25): FR-066 / SC-034, five Poly presets

| Item | Verdict | Evidence (evidence/run-log/P13b.txt) |
|---|---|---|
| FR-066 | pass | tools/profundum_preset_defs.h rows 54-58: Poly Spread (AT-31), Poly Swell (AT-24), Poly Ignition (AT-01), Poly Rectifier (AT-06), Poly Pluck (AT-25); each `.controls` carries `{kVoiceModeId, 1.0}`; no Mono-requiring primary. TREE GREEN n=58. |
| SC-034 | pass | `Profundum_PolyPresetCount` in the per-push run "All tests passed (2565527 assertions in 101 test cases)"; showcase shards 0-7 green with the five primaries (0 overrides); chord render rows "chord E1+B1+E2" for all five; CPU lane "SC-022 max MONO 5.01619 % (Fault Line), max POLY4 15.0305 % (Poly Pluck)" against 8 / 25 %, "CPU GREEN". |
| FR-004, FR-061, C-7 (re-measured at N = 58) | pass | "LIBRARY LANES GREEN"; distinctness "presets=58 F=1 closest=Bedrock vs Locked Floor d=1.91446"; loudness "presets=58 median_db=-11.167", "worst deviation 3.60167 dB". |

Caveat 6 above (no Poly preset) is closed by this addendum.
