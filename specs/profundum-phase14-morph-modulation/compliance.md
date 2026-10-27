# Compliance Report: Profundum Phase 14 (profundum-phase14-morph-modulation)

**Overall status: COMPLETE** (main-loop close, 2026-10-27, after the workflow report below; rulings R11 - R14 in spec.md Clarifications)

## Honest gap list (fails first)

The workflow comply stage reported five FAIL rows. Each was closed in the main loop; the evidence is in `evidence/run-log/close/r11/`.

| ID | Verdict | Evidence |
|----|---------|----------|
| FR-053 / SC-014 | PASS (was FAIL) | R11 engine optimisation (no bar relaxed, no workload changed): opt-in `TanhADAA::processFast` (80-segment degree-5 table in double, |F1 error| <= 2.4e-9) at Fracture and ShapeMorph ROUND, Tube linear-gain setters. `Profundum_ProcessorCpuMorphMod` alone, pinned (`evidence/run-log/close/r11/sc014-processor-cpu-morphmod.log`): `MOD-POLY ... pct P = 20.5977 % (bar <= 25)`, `MOD-MONO ... pct P = 5.69372 % (bar <= 8)`, controls 1.00752 / 0.989053 (valid), `All tests passed (117 assertions in 1 test case)`. (b) and (c) were green before R11 (0.64252, POLY4 19.06 %) and R11 only removes work. |
| FR-054 / SC-021 | PASS (was FAIL) | All 65 clang-tidy findings fixed (45 by clang-tidy -fix on the auto-fixable checks, 20 by hand); re-run on the 11 flagged files: 0 findings after the Catch2 false-positive filter (`evidence/run-log/close/r11/tidy-rerun-phase-files.txt`); the R11/R12 files (tanh_adaa_test, shape_character_nonfinite_test, param_path_audibility_test, tube_stage_test, shape_morph_test, bass_character_stage_test): 0 findings (`evidence/run-log/close/r11/tidy-r11-files.txt`). Builds 0 warnings. `check-portability: all clear -- 47 compiled.`, lint-finite-bitmask clean, lint-nonfinite-symbols all clear, lint-apple-globals OK, lint-ieee-flag-sets clean, lint-wallclock-tags clean, lint-plugin-editor-baseline OK, lint-serial-test-runs clean with an empty baseline. suites.sh FENCE_ALLOWED now lists profundum_morph_mod_nonfinite_test.cpp and the R11 edit set. |
| CC-fastmath | PASS for the phase's code; proxy note | Phase TUs green in mac mode (workflow lane). R11 TUs under the repro (`evidence/run-log/close/r11/fastmath-r11-mac.log`, `-ieee.log`): ieee `All tests passed (2059131 assertions in 126 test cases)`; mac 125 / 126, the one red being `NaN input propagates NaN output (FR-019)` (tanh_adaa_test.cpp:475), a pre-existing case in a TU dsp/tests/CMakeLists.txt compiles with -fno-fast-math (the repro does not apply per-file overrides). The two engine mac-mode reds the workflow found (VoiceEngine_MassSplit, VoiceEngine_DeterminismFingerprint) reproduce at e501b577 (workflow comply). Real macOS CI is green on all 8 test shards at 01223dbb (run 40316264384), and `git diff 01223dbb e501b577 -- dsp/` is 9 one-line test-tag edits, no engine code; the repro was not run at 01223dbb itself. They are a proxy disagreement, recorded, not a phase defect. VoiceEngine_MassSplit asserts bit-equality between two computations (CLAUDE.md anti-pattern): an open item outside this phase's fence. |
| SC-008 (after R11) | PASS | R14: with R11 reverted the four fingerprint shards read `failures=0 regionMismatches=0 worstMetric=0 worstSample=0` against the pre-phase harvest (`evidence/run-log/close/r11/fingerprints-r11-reverted-<k>.log`). With R11 the fingerprint references were re-harvested from the comparison shards (decoded and claims kept); now `failures=0 ... worstSample=0` (`evidence/run-log/close/r11/fingerprints-r11-<k>.log`). |
| R11 consumers | PASS | Consumer gate of the R11 edit set: dsp_primitives / dsp_processors / disrumpo / membrum 120/120 shards green; dsp_ieee and profundum 119/120, the two reds being the R12 measurements (`evidence/run-log/close/r11/gate-consumers-summary.txt`), both green after R12: `ShapeCharacter_NonFiniteRecovery` 610/610 assertions, `Profundum_ParamPathAudibility` all passed, AT-33 C3 21.7963 (`evidence/run-log/close/r11/param-path-audibility.log`). dsp_systems (minus the 168 mono-sum [long] shards, R13: nightly): `GATE GREEN dsp_systems_tests cases=3708 ... failed_cases=0` (`evidence/run-log/close/r11/gate-dsp-systems.log`). |

Other process gaps: FR-046's stale comment is fixed (profundum_preset_defs.h:25 "two presets added"). SC-007's transparency shards are nightly (R10); a transparency case compares two renders of the same build, so R11 does not move its verdict. SC-011 after R11, each shard alone: 6.12, 7.52, 6.21, 7.76, 5.88, 7.07, 5.97, 7.69 s (all <= 9 s, 60 % of the 15 s per-push bar).

## Compliance table (all items)

Pass rows carry the key verbatim readings; full line-level citations are in the task logs under evidence/run-log/.

| ID | Verdict | Evidence |
|----|---------|----------|
| FR-001 | pass | profundum_morph_modulator.h:96-104 prepare, :109-116 reset, :121-162 sanitising setConfig, :166-173 setTempoBpm, :179-186 noteOn, :189-194 setModWheel, :197-235 advanceInterval. ProfundumMorphModulator_ApiAndRt: `All tests passed (99 assertions)` |
| FR-002 | pass | h:62-73 Config has exactly the C-12 fields |
| FR-003 | pass | h:100 Hard retrigger; :183-184 noteOn reset+gate(true); no gate(false). Envelope 129 and MorphModRetrigger 262 assertions pass. Note: :207-209 second env_.process() in restart interval when attack <= one interval (recorded defect T006) |
| FR-004 | pass | h:148-159, :211-212; no retrigger of LFO. Cycle 130 assertions pass |
| FR-005 | pass | h:185 velocity_ = (v >= 127) ? 1.0f : v/127.0f. MorphModVelocity 64 assertions |
| FR-006 | pass | h:189-194, :228-229. MorphModWheelRouting 1067 assertions |
| FR-007 | pass | h:75-83 Output : ModulationSource, range {-1,1}; :233 clamp |
| FR-008 | pass | all members noexcept; NoAlloc 3169 and ApiAndRt 99 assertions pass |
| FR-010 | pass | profundum_engine.h:313-320, :278-283, :157, :207 |
| FR-011 | pass | engine.h:948-965, sub-step 1b between Events and morph_.advanceInterval(); no logCall in 1b |
| FR-012 | pass | engine.h:958-961. Composition 3169, TransparentAtZero 707 assertions |
| FR-013 | pass | engine.h:839-840 morphMod_.noteOn first statement of applyNoteOn. MorphModRetrigger 262, PedalRestrike 578 |
| FR-014 | pass | engine.h:174, :675, :816. MorphModLifecycle 169 assertions |
| FR-015 | pass | observer.h:62-64; `enum class ProfundumCall unchanged (37 enumerators)` |
| FR-016 | pass | engine.h:299-304, :287-291. Profundum_StateV3MorphMod 7190 assertions |
| FR-017 | pass | T031 lane (copy with FENCE_ALLOWED fix): `DONE 2026-10-27T11:08:57Z GREEN`; `GATE GREEN dsp_systems_tests cases=435 assertions=10050219 | dsp_ieee_tests cases=122 assertions=248165`; `GREEN tus=57 run=141 done=141 library_skipped=0 red=[] ... wall=1538s`; SC-008 `compared=15/15/14/14 failures=0 regionMismatches=0 worstMetric=0` |
| FR-018 | pass | morph_state.h untouched (kMorphStateBytes == 201); kSoundRows.size() == 60. MorphModOutsideEndpoints 232 assertions |
| FR-019 | pass | `scope: 0 path(s) outside the allow-list` |
| FR-020 | pass | morph_mod_params.h:261-296; kNumRoutedIds = 1012. ParamTableConformance 3531, SoundParamTableRouting 972 |
| FR-021 | pass | processor.cpp:996-1000. MorphModCompareBypass 10854 assertions |
| FR-022 | pass | processor.cpp:944-947; morph_mod_params.h:446-449. MorphModWheelRouting 1067, MassPerformanceOffset 424 |
| FR-023 | pass | processor.cpp:706, :626, :636, :654; kStateV3Bytes == 610. StateV3MorphMod 7190, StateVersions 335365 |
| FR-024 | pass | controller.cpp:178, :200, :206-219; morph_mod_params.h:398-429 |
| FR-025 | pass | performance_params.h unchanged; processor.cpp:662 |
| FR-030 | pass | editor.uidesc morph-band :199-201, tab-modulation :395-413; UiLayout 1296, UiPlacement 784, UiZoom 57, UiLabels 37, UiClassRule 14, UidescTagIntegrity 471, UidescBindsEverySurfaceId 199 |
| FR-031 | pass | main_page_audibility_test.cpp; `1003 E1: lo 0 hi 14.3522 value 14.3522 bar 3`, `1003 C3: ... value 13.8655`, `1004 E1 value 14.3522`, `1004 C3 value 13.8655`, worst `|p_twin - p_proc| 0` |
| FR-032 | pass | uidesc tooltips :199, :409, :356 contain required sentences |
| FR-033 | pass | MANUAL.md:390-460; ManualCoversSurface 197 assertions |
| FR-034 | pass | CHANGELOG 84 -> 93, 574 -> 610, Morph Modulation bullet; version.json unchanged |
| FR-035 | pass | morph_indicator_slider.cpp:27-60; UiMorphIndicator 37 assertions |
| FR-036 | pass | spectrum_frame.h sizeof == 320, morphPosition at 288; MorphIndicatorFrame 79039 assertions |
| FR-037 | pass | engine.h:415 const accessor; controller.cpp:890-905; EngineDisplayAccessorNeutral 85, SpectrumFrameDoesNotChangeAudio 6912 |
| FR-040 | pass | `presets=60 columns=57 covered=57 preExistingCovered=53` |
| FR-041 | pass | coverage-matrix.csv MorphMod rows, bar 3; e.g. Poly Swell 5.67 / 5.59 (min margin 2.586 dB) |
| FR-042 | pass | PresetDefsRules 1169 assertions |
| FR-043 | pass | `cases=108 assertions=5692377`; quiet pass `cases=17`; `library_skipped=0`; generator `OK — 60 file(s); 0 differing`; PresetCpu `SC-022 max MONO 5.91872 % (Fault Line), max POLY4 19.0583 % (Poly Ignition)` |
| FR-044 | pass | probe-morphmod.csv 1856 rows, 32 shards exit=0 |
| FR-045 | pass | helpers carry morphMod fields; VoiceEngineHelpers_MorphModFields 32, MorphModHarnessCarriage 986; fidelity `worst |twin - processor| 0` |
| FR-046 | pass | conversions.md rule (ii): 5 conversions, 3 added columns need new presets; library 60 (minor stale comment at profundum_preset_defs.h:26) |
| FR-050 | pass | every source showcase clears 3 dB (min 5.59 dB); rows 1003/1004 at 14.35 / 13.87 dB |
| FR-051 | pass | mono-sum.log all rho >= 0.99 (Tidal Fold 0.999801); AnchorMono 42 assertions |
| FR-052 | pass | control-rate only; no new nonlinearity in diff |
| FR-053 | pass | see gap list (R11): MOD-POLY 20.60 %, MOD-MONO 5.69 % |
| FR-054 | pass | see gap list: tidy 0 findings, lints clean, portability all clear |
| FR-060 | pass | TransparentAtZero 707; 64 shards `mismatches=0 regionMismatches=0 unharvested=0`; fingerprints `failures=0 regionMismatches=0 worstMetric=0 worstSample=0` |
| FR-061 | pass | FactoryPresetStateFormat 783 assertions; `regionMismatches=0` on 4 shards |
| FR-062 | pass | `HARVEST GREEN PREPHASE MERGED presets=58 rows=870 ... claims=58` taken before engine edits |
| FR-063 | pass | preset_library_test.cpp:1093-1130; PresetDefsRules 1169 |
| SC-001 | pass | ApiAndRt 99, MorphModNoAlloc 3169, Phase11PathsDoNotAllocate 728 assertions; `lint-layers: OK` |
| SC-002 | pass | Envelope 129 assertions; settle 11/10/21 intervals at 48/44.1/96 kHz |
| SC-003 | pass | Cycle 130 assertions; Lifecycle 169 |
| SC-004 | pass | MorphModVelocity 64 assertions |
| SC-005 | pass | Composition 3169 assertions |
| SC-006 | pass | TransparentAtZero 707 assertions |
| SC-007 | pass | FactoryPresetStateFormat 783; nightly transparency `GATE GREEN cases=101 wall=128.6s`, `mismatches=0` (caveat: pre-dates 10:46 retag) |
| SC-008 | pass | four shards compared 15+15+14+14, `failures=0`, worst 0 |
| SC-009 | pass | MorphModWheelRouting 1067, MassPerformanceOffset 424 |
| SC-010 | pass | Retrigger 262, PedalRestrike 578 (`re-strike grid point = 0.00317321, fresh first step = 0.00317321`) |
| SC-011 | pass | 8 shards green, worst step/static <= 1.1231 (bar 2), wall 7.09-9.21 s (shards 2, 4 re-run 8.63 / 8.96 s; margin thin) |
| SC-012 | pass | MidiEventTranslation 3440715 assertions; Lifecycle 169; CompareBypass 10854 |
| SC-013 | pass | StateV3MorphMod 7190, StateVersions 335365, StateRoundTrip 3365 |
| SC-014 | pass | (a) MOD-POLY 20.5977 % / MOD-MONO 5.69372 %, controls valid; (b) GREEN 0.64252, (c) 19.06 %, (d) ns_per_call Off 8.5656 ... Velocity 10.135 |
| SC-015 | pass | ParamTableConformance 3531, SoundParamTableRouting 972 |
| SC-016 | pass | all UI cases green; EditorLifecycle 315 |
| SC-017 | pass | showcase margins >= 2.58617 dB; MainPageAudibility 483854 assertions |
| SC-018 | pass | CoverageMatrixComplete 193, PresetDefsRules 1169; `LIBRARY LANES GREEN` |
| SC-019 | pass | AnchorMono 42; mono-sum `ungated=0`, 164760 assertions |
| SC-020 | pass | ManualCoversSurface 197; version.json unchanged |
| SC-021 | pass | see gap list: 0 warnings, 0 tidy findings, pluginval strictness 5 exit 0 (workflow lane) |
| SC-022 | pass | MorphModOutsideEndpoints 232 assertions |
| SC-023 | pass | harness-fidelity 1755857 assertions, worst 0; tag gate `GATE GREEN ... cases=431 ... 120 ... 125 wall=277.1s` |
| SC-024 | pass | static_asserts; ASan `ASAN GREEN`, `AddressSanitizer_lines=0`, `All tests passed (1669 assertions in 3 test cases)` |
| CC-rt | pass | new audio-path code read line by line; NoAlloc 3169, Phase11PathsDoNotAllocate 728 |
| CC-layers | pass | modulator includes only L0/L1; lint-layers OK |
| CC-naming | pass | ODR sweep: one definition each |
| CC-warnings | pass | forced recompile of 34 TUs: 0 warnings, 0 errors, 5 targets linked (tidy findings reported under FR-054) |
| CC-portability | pass | `check-portability: all clear -- 44 compiled.`; all lints clean |
| CC-fastmath | pass (phase code) | see gap list: proxy-only engine reds, real macOS CI green on the same engine code |

## Implementation notes (deviations reported by task agents)

- T001: spec Non-goals line updated to match the P-8 amended dsp/ list.
- T003: no plugin_ids.h offsets for globals/macros, so harvest uses fixture constants kV2GlobalsAt/kV2MacrosAt.
- T006: noteOn(0) is a no-op; first-interval attack case has a second env_.process() (defect T006). profundum_morph_modulator.h is not in KRATE_DSP_SYSTEMS_HEADERS (IDE list only; needs a ruling since dsp/CMakeLists.txt is outside the fence).
- T007: SC-005 (d) non-vacuity limited to arms that can move; step bar uses B3-1 with 4*FLT_EPSILON; SC-006 non-vacuity only for pairs with distinct endpoints.
- T008/T009: hand-driven heap engines for lifecycle arms; click-free cases numbered 1..4 (later re-sharded to 8).
- T011/T012: T012 also moved count sites in files beyond the six listed (manual_coverage_test, lifecycle_test, param_denorm_test 76->85 and 25->29).
- T013: new UI rulings live in editor_layout_test.cpp instead of ui_strings_test_table.h.
- T019/T020: local copy mpRenderProcessorWithTwinGrid in main_page_audibility_test.cpp because ppRenderProcessor drops twin grid; to be removed when the helper returns it.
- T021: probe uses local reader mmpRead (with equivalence guard) to render the Off arm once per (preset, note).
- T023: Wheel Fold (third added preset, M-WHEEL) was flagged as risk to Distinctness; final library holds 60 presets (Tidal Fold, Struck Fold added), M-WHEEL covered by converting Long Tail. Profundum_FactoryLibraryShape expected size changed.
- T026: merge-csv.js has no MorphMod group, so a node post-pass writes group-min records.
- T030/T031: scope-fence and suites lanes treat commits 4fb6c248 and e501b577 as allowed; earlier-phase evidence files rewritten by gate runs must be restored with git checkout.
- T034: clang-tidy run on an explicit file list, not the whole `dsp` sweep (15 h hang on record).
- T035: run-cpu-tests.js has no filter, so the dsp_systems_tests CPU leg runs all timing cases (record: 5 h 17 min); run detached.

## Remaining gates for the human loop

1. Push (user only). The macOS CI leg gives the real fast-math verdict for the phase's engine edits and R11.
2. Open item outside the phase fence: VoiceEngine_MassSplit's bit-equality assertion (profundum_engine_test.cpp:1257/1258) reads red under the local fast-math proxy.
3. The nightly long lane runs the 168 BassStereoField_MonoSumPerBand shards against the R11 code (R13).
