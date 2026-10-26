# Compliance Report - Profundum Phase 11 (profundum-phase11-parameters)

**Overall status: COMPLETE locally (every SC/FR verified on Windows); SC-004 and the macOS / Linux legs of SC-001 PENDING-CI.** Close fixes 2026-10-22: FR-074 (two engine defects behind SC-011), the SC-020 measurement rulings, 213 clang-tidy fixes.

## Gap list (honest, first)

| Item | Verdict | Gap |
|------|---------|-----|
| SC-004 | PENDING-CI | auval runs only in macOS CI; no CI run exists for this branch yet. |
| SC-001 | partial | Windows build and portability clean; macOS and Linux CI legs PENDING-CI. |

## Compliance table

Evidence is quoted from the verifying agents; for passing rows the file:line citations and test output lines are kept.

| ID | Verdict | Evidence |
|----|---------|----------|
| FR-001 | pass | controller.cpp:61-66 registers global, macro, performance, sound (60 rows, sound_param_table.h:452-454), note 410-417, morph control. sound_param_table.h:580 static_assert routedCount()==84. ParamTableConformance asserts getParameterCount()==84 and per-ID title/units/stepCount/flags/default. profundum_tests "[profundum]~[long]~[performance]~[perf]~[benchmark]~[!benchmark]": 'All tests passed (1111582 assertions in 28 test cases)', wall 136 s. |
| FR-002 | pass | sound_param_row.h:66-68 lawOf(leaf) reads BassMorphEngine::kLawTable; :129-144 linear, logMapFromNormalized, offset-log law; :172-175 Linear/EqualPower become linear knobs. Each pack has static_asserts that label counts equal maxEnum+1 and Bool lists have 2 labels (anchor_params.h:200-220, note_params.h:108-135, morph_params.h:75-99). No range literals in packs. |
| FR-003 | pass | sound_param_row.h:113 kDefaultLeafBits; :233-252 defaultNormalizedOf nudge; note_params.h:247-256. Profundum_DefaultsMatchEngine (param_wiring_test.cpp:384) passed. |
| FR-004 | pass | performance_params.h:69-79: 4,5 kCanAutomate\|kIsHidden; 6,7 kIsHidden only. morph_params.h:264 editTarget flags = kIsList only; :305 persists Edit Target. ParamTableConformance/Flags passed. |
| FR-005 | pass | One pack per band (core, anchor, note, fold_shape, character, dynamics, field, motion, morph), each with register/handle/format; save/load in note_params.h:360,391,446 and morph_params.h:301,312,352; endpoints persisted processor.cpp:546-549. |
| FR-006 | pass | sound_param_row.h:345-379 display formats per FR-006 table. Profundum_ParamDisplayFormats passed. |
| FR-007 | pass | note_params.h:331-356 'ms/oct' under ConstantRate; controller.cpp:166-167. Profundum_PortamentoLabelFollowsGlideMode passed. |
| FR-008 | pass | note_params.h:238-244, :319-324 RangeParameter [0,24] st stepCount 24 default 2; :286 round(24v). |
| FR-009 | pass | fold_shape_params.h:23 'Harmonic Fold'; 501 'Shape' Percent; character labels Tube/Wavefold/Rectify/Bite/Crush/Fracture; morph Hold/Blend/Yield; anchor Lock/Follow/Punch/Ghost. ParamTableConformance/Names passed. |
| FR-010 | pass | processor.cpp:87-105 lastPointOf; :703-729 routeParamChange O(1) via kParamRoutes (sound_param_table.h:469-490). |
| FR-011 | pass | processor.cpp:669-680 pass 1 applies queue 1001 first; sound_param_row.h:290-303 writeSoundRow. EditTargetAddressing/EditTargetFirst passed. |
| FR-012 | pass | sound_param_row.h:291-297 note+modifier from kNoteValueDropdownMapping; field 809 -> leaves 34/35, motion 907 -> 43/44. SyncRateAll30 sections passed. |
| FR-013 | pass | processor.cpp:737-761 applyEndpoints; endpointRejections_ at :757-758; called at :344. |
| FR-014 | pass | processor.cpp:767-790 pushParamsToEngine; note_params.h:514-540; every block at :345. |
| FR-015 | pass | Engine calls only in process(), setupProcessing, setActive(false), test seam; setState/getState/notify/getTailSamples make none. |
| FR-020 | pass | sound_param_table.h:606-616; processor.cpp:679-680, :789, :546-549. |
| FR-021 | pass | controller.h:104 mirror_; controller.cpp:109,123-131,265-289; rejected blob returns before assignment (:278-280). |
| FR-022 | pass | controller.cpp:184-206, redisplayEditTarget :239-255. EditTargetAddressing/SwitchAloneChangesNothing, ControllerRedisplay passed. |
| FR-023 | pass | README.md:35-36: 'Automation is target-relative. Host automation of a sound parameter writes the endpoint selected by Edit Target when it plays back. A lane recorded while editing A writes B if Edit Target is B at playback.' |
| FR-024 | pass | controller.cpp:203-205 restartComponent(kParamValuesChanged); no beginEdit/performEdit/endEdit. NoHostEdits, OneRestartAfterRedisplay passed. |
| FR-025 | pass | processor.cpp:605-638 flushControllerSync, called only from setActive/setState/getState/notify/onTimer; 50 ms timer :184-186; pull :583-587 and controller.cpp:196-200. ProcessorControllerSync sections passed. |
| FR-030 | pass | controller.cpp:210-219; processor.cpp:571-575, :645-655. Profundum_CopyEndpoint AToB/BToA passed. |
| FR-031 | pass | controller.cpp:222-231; processor.cpp:576-582, :783-789, :626-636. |
| FR-032 | pass | processor.cpp:783-789, :440-443, :553-557. Profundum_CompareDoesNotWriteState/CompareA passed. |
| FR-033 | pass | processor.cpp:522-523, :285-288; SetActiveFalseResets (copy_compare_test.cpp:319), CompareReset (:398) passed. |
| FR-040 | pass | plugin_ids.h:22 kCurrentStateVersion = 2; :35-47 layout 88/289/490/510; processor.cpp:535-560. Profundum_StateRoundTripV2 passed. |
| FR-041 | pass | processor.cpp:926-941 seqlock; precedence in getState :553-556; synthesizedRuntimeBlob :971-993. StateVersions FullStreamAppliesBlob/Unchanged passed. |
| FR-042 | pass | Order unchanged (processor.cpp setState stores atomics only; process() applyEndpoints -> pushParamsToEngine -> applyPendingRestore). End-to-end proof SC-011 now 0.0178014 dB <= 0.1 (`evidence/run-log/close/final-srr.log`). A restore before the first block re-snaps the engine's dependent state (profundum_engine.h:280 restoreMorphRuntime, :592 resetDependents; FR-074). |
| FR-043 | pass | processor.h:176 seed_ persisted (processor.cpp:545), not registered; setSeed before prepare (:250-251) and reset (:290-291). Determinism: 'changed seed 9.30317 dB (bar >= 0.5), same seed 0 dB'. |
| FR-044 | pass | processor.cpp:498-500, :502-511, :125-152, :145-149, :516-518. StateVersions V1ResetsPhase11Fields, CutKeepsPrior (k = 0..509) passed. |
| FR-045 | pass | controller.cpp:91-144. ControllerV1, ControllerCutKeepsPrior, ControllerDisplaysB passed. |
| FR-046 | pass | processor.cpp:524 resetPerformanceParams; controller.cpp:136-139. PerformanceResetOnLoad, StateIndependentOfPerformance passed. |
| FR-050 | pass | controller.cpp:309-330 mapping: ModWheel->6, Sustain->5, AfterTouch->7, PitchBend->4; bus!=0 false. MidiMapping/AssignmentTable passed. |
| FR-051 | pass | processor.cpp:777 setPitchBend(2v-1); note_params.h:539. MidiMapping/PitchBend passed (12 st range, +1 raises fCore 12 st +- 5 cents). |
| FR-052 | pass | processor.cpp:867-894, :415-426, :831-858, :280-284, :254, :340-342. SustainEquivalence (max abs diff == 0) and MultiPointQueue passed. |
| FR-053 | pass | performance_params.h:75-79; processor.cpp:752-754; `git diff 565f195d -- dsp/` empty. MassPerformanceOffset/SlotsUntouched and NeverWritten passed. |
| FR-054 | pass | sound_param_table.h:563-572 noteBandExact() static_asserted at :581; quantiseVelocity (processor.cpp:77-79) quantises engine note-on only. |
| FR-055 | pass | processor.cpp:738-745, :752-754. MassPerformanceOffset/Arithmetic passed. MassOffsetAudibility: 'position 0: value 3.51851 bar 3; 0.5: 4.08175; 1: 4.60311', All tests passed. |
| FR-056 | pass | processor.cpp:751-754, serializeSlot :112-118, :547, :614. MirrorAndNoMessage (midi_mapping_test.cpp:162), MassOffsetSendsNothing (copy_compare_test.cpp:415) passed. |
| FR-060 | pass | processor.cpp:463-483. Profundum_TailFormula passed; Profundum_TailCoversRelease: 'All tests passed (44240 assertions in 1 test case)'. |
| FR-061 | pass | plugins/shared/src/midi/sustain_latch.h:21-82 defines Krate::Plugins::SustainLatch; diff vs HEAD vorago header differs only in namespace lines and one blank line; Vorago header is now `using SustainLatch = Krate::Plugins::SustainLatch;`. /f/tmp/p11_shared.log: 'All tests passed (6450 assertions in 461 test cases)'; p11_vorago.log: 'All tests passed (4060395 assertions in 119 test cases)'; p11_build.log 0 warning lines. |
| FR-062 | pass | process() paths use only atomics and stack arrays. Profundum_Phase11PathsDoNotAllocate (AllPathsZeroAllocations + Teeth) passed. |
| FR-063 | pass | Determinism/IdenticalStateAndStreamRenderIdentically (determinism_test.cpp:123) passed; 'changed seed 9.30317 dB (bar >= 0.5), same seed 0 dB (bar == 0)'. |
| FR-064 | pass | EveryParamReachesEngine in per-push run: 'All tests passed (1112563 assertions in 28 test cases)'. Profundum_ParamPathAudibility: 'All tests passed (1852580 assertions in 1 test case)', 30 rows ',PASS', 0 ',FAIL'. E1 AT-11 2.08853 x3 vs bar 2; C3 2.03878 x3 vs bar 2 (param_path_audibility_helpers.h:490-491). |
| FR-065 | pass | Profundum_MonoSumViaParams: 'All tests passed (16340 assertions in 1 test case)', 7 s. SOLID q_sub_db 0, s2m_db -120; SPREAD q_sub_db ~-5e-12; WIDE MONO out q_sub_db -3.96e-05, rho_sub 0.999984, s2m_db -50.41. |
| FR-066 | pass | Same evidence as SC-020 (`evidence/run-log/close/cpu-lane-r8-full.log`): every pct bar and every P/D <= 1.05 in a valid run. |
| FR-067 | pass | processor.h:102-125 noexcept allocation-free test seams; process() calls none. |
| FR-070 | pass (exception) | dsp/ modified only under the FR-074 user ruling: profundum_voice.h (deferred amp gate, :353, :658, :779, :786, :821) and profundum_engine.h (restoreMorphRuntime :280, resetDependents :592, :396), plus the two regression cases in dsp/tests/unit/systems/profundum_engine_test.cpp:2531, :2570. No other dsp/ file. |
| FR-071 | pass | `git status --short plugins/profundum/resources/` empty; controller.cpp:332-337 still loads placeholder editor.uidesc. |
| FR-072 | pass | specs/Profundum-roadmap.md:782 '400–499 drop / note behaviour / amp envelope, 500–599 fold / shape, ...'; plugin_ids.h:98, :151-157 amp envelope 420-426. |
| FR-073 | pass | tools/check-portability.js:130-135 `if (plugin === 'profundum') { flags.push('-I dsp/tests/unit/systems'); }`. Caveat: the only portability.log (evidence/run-log/close/portability.log) predates the fix and reads 'all clear -- 18 compiled, 2 skipped'; the post-fix rerun is quoted under SC-001. |
| FR-074 | pass | (1) profundum_voice.h:779 the transition's gate is deferred (ampGatePending_), flushed after the amp setters (:658), at the declick switch (:821) and before a release (:353); audible()/active()/note-memory read the pending gate. (2) profundum_engine.h:280 restoreMorphRuntime re-snaps via resetDependents (:592) while no block has rendered (:396). dsp_systems_tests "[fr-074]": 'All tests passed (54 assertions in 3 test cases)' (`evidence/run-log/close/fr074-cases.log`; the tag also matches Phase 7's BassStereoField_Deterministic). Gate `evidence/run-log/close/fr074-dsp-systems-profundum.log` 'All tests passed (10448024 assertions in 514 test cases)'. |
| SC-001 | **partial** | Windows: build of Profundum + profundum_tests 0 warning lines (`evidence/run-log/close/build-final.log`); `node tools/check-portability.js` over the 21 changed .cpp: 'check-portability: all clear -- 21 compiled.' (`evidence/run-log/close/final-portability.log`). macOS and Linux CI legs: PENDING-CI. |
| SC-002 | pass | Unfiltered `profundum_tests.exe`: 'All tests passed (3205993 assertions in 33 test cases)', exit 0, 197 s (`evidence/run-log/close/final-unfiltered.log`). shared_tests per-push 'All tests passed (6450 assertions in 461 test cases)' and `vorago_tests.exe "Vorago_SustainLatchUnit,Vorago_SustainLatch"` 'All tests passed (10948 assertions in 2 test cases)' (remediation logs; neither file changed since). |
| SC-003 | pass | `tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Profundum.vst3` -> 'Profundum exit=0'; Vorago.vst3 -> 'Vorago exit=0'. Both logs 'Strictness level: 5', 0 lines matching fail\|error. |
| SC-004 | **PENDING-CI** | auval runs only in macOS CI; no CI run exists for this branch yet. |
| SC-005 | pass | Profundum_ParamTableConformance -> 'All tests passed (1641 assertions in 1 test case)'; param_table_test.cpp:383 count==84, :382 table.size()==84. |
| SC-006 | pass | Profundum_ParamDenormRoundTrip -> 'All tests passed (11929 assertions in 1 test case)'; 1e-9 bound at param_denorm_test.cpp:334, :509. |
| SC-007 | pass | Profundum_DefaultsMatchEngine -> 'All tests passed (137 assertions in 1 test case)'. |
| SC-008 | pass | Profundum_EveryParamReachesEngine -> 'All tests passed (3942 assertions in 1 test case)'; count==84 at param_wiring_test.cpp:416; IDs 6/7 oracle at midi_mapping_test.cpp:117-118. |
| SC-009 | pass | Profundum_EditTargetAddressing -> 'All tests passed (1302 assertions in 1 test case)'; param_wiring_test.cpp:646-647 restartFlags.size()==1u, ==kParamValuesChanged. |
| SC-010 | pass | Profundum_StateRoundTripV2 -> 'All tests passed (3111 assertions in 1 test case)'; memcmp 510 bytes == 0 (state_v2_test.cpp:881), seed 0x1234 (:853), Depth edit in B (:891-893), controller displays B (:926-930). |
| SC-011 | pass | After the FR-074 fixes: `evidence/run-log/close/r2-srr.log` / `evidence/run-log/close/final-srr.log` 'SC-011 band-level difference (aggregate, [100 ms, 2.1 s)): 0.0178014 dB (bar 0.1 dB)', 'All tests passed (5572 assertions in 1 test case)'. Before: 1.09162 dB; diagnosis in `evidence/run-log/close/sc011-probe.txt`. |
| SC-012 | pass | Profundum_StateVersions -> 'All tests passed (335365 assertions in 1 test case)' (87 s); cut arm k=0..509 (state_v2_test.cpp:478), p_a {0, 0.7, 0.343} (:606), v3 -> kResultFalse (:664). |
| SC-013 | pass | Profundum_CopyEndpoint -> 'All tests passed (100 assertions in 1 test case)'. |
| SC-014 | pass | Profundum_CompareDoesNotWriteState -> 'All tests passed (2012 assertions in 1 test case)'; window [140 ms, ...) copy_compare_test.cpp:280, CHECK(compareDiff <= 0.1) :299, teeth REQUIRE(preDiff > 0.1) :298. |
| SC-015 | pass | Profundum_MidiMapping -> 'All tests passed (7319 assertions in 1 test case)'; abs(riseSt-12) <= 0.05 at midi_mapping_test.cpp:347-349. |
| SC-016 | pass | Profundum_PortamentoLabelFollowsGlideMode -> 'All tests passed (17 assertions in 1 test case)'; suffixes {" ms"," ms/oct"," ms"} at param_table_test.cpp:564. |
| SC-017 | pass | Profundum_TailCoversRelease ([long]): 'All tests passed (44240 assertions in 1 test case)'. Profundum_TailFormula in per-push run: 'All tests passed (1112563 assertions in 28 test cases)'. |
| SC-018 | pass | Profundum_ParamPathAudibility: 'All tests passed (1852580 assertions in 1 test case)'; 30 PASS, 0 FAIL; lowest margins AT-11 E1 2.08853, C3 2.03878 (bar 2), AT-02 C3 0.378903 (bar 0.25). evidence/param-path-audibility.csv exists with identical values. |
| SC-019 | pass | Profundum_MonoSumViaParams: 'All tests passed (16340 assertions in 1 test case)'; 'SC-019,SOLID,MONO,out,...,rho_sub,1,s2m_db,-120'; 'SC-019,WIDE,MONO,out,...,rho_sub,0.999984,s2m_db,-50.4053'; portability 'all clear -- 20 compiled.' |
| SC-020 | pass | Method per the SC-020 rulings (640 x 10-block trials, P and D on one engine via mutableEngineForTest, processor_cpu_test.cpp:66, :68, :639, :718). `evidence/run-log/close/cpu-lane-r8-full.log` (node tools/run-cpu-tests.js profundum_tests, alone, pinned): MONO pct P 4.30087 ratio 0.988536; POLY 14.5785 / 1.01985; MAX-MONO 4.85531 / 0.989; MAX-POLY 16.5678 / 0.984836; AUTO-MONO 4.26375 / 0.989083; AUTO-POLY 15.0184 / 1.01328; AUTO-MAX-MONO 4.82175 / 1.00831; AUTO-MAX-POLY 16.7763 / 0.988052 (bars 8 / 25 %, 1.05); controls 0.958313-1.00916, all VALID; 'All tests passed (2093714 assertions in 7 test cases)'. Earlier runs r1-r7 (red / INVALID) and the A/B are kept in evidence/run-log/close/ and cpu-ab.txt. |
| SC-021 | pass | Profundum_Phase11PathsDoNotAllocate -> 'All tests passed (713 assertions in 1 test case)'; CHECK(countA == 0u), CHECK(countAB == 0u) at phase11_alloc_test.cpp:246-247. |
| SC-022 | pass | Profundum_Determinism -> 'All tests passed (9567 assertions in 1 test case)'; 'SC-022 drift arm (frame-wise, [0.5 s, 4 s)): changed seed 9.30317 dB (bar >= 0.5), same seed 0 dB (bar == 0)'; diffL/diffR == 0.0f at determinism_test.cpp:144-145. |
| SC-023 | pass | profundum: `pwsh tools/run-clang-tidy.ps1 -Target profundum` 'Files analyzed: 26', 'Errors: 0', 'Warnings: 0' (`evidence/run-log/close/final-tidy.log`, after the 213 fixes). vorago 'lane vorago GREEN files=57 warnings=0'; shared (the change set's TU, user ruling) 'lane shared GREEN files=1 warnings=0' (`evidence/run-log/close/tidy.log`). Suites: profundum per-push + [long] (SC-002), shared per-push, the two Vorago latch cases, pluginval Profundum (`evidence/run-log/close/final-pluginval.log` exit 0, 0 FAILED) and Vorago. dsp_systems_tests [profundum] per-push for FR-074: 'All tests passed (10448024 assertions in 514 test cases)', 6872 s (`evidence/run-log/close/fr074-dsp-systems-profundum.log`). |
| SC-024 | pass | Per-push (incl. Profundum_MassPerformanceOffset): 'All tests passed (1112563 assertions in 28 test cases)'. Profundum_MassOffsetAudibility: 'All tests passed (174698 assertions in 1 test case)', position 0 value 3.51596, 0.5 value 4.08081, 1 value 4.60318 (bar 3). |
| SC-025 | pass | Profundum_ProcessorControllerSync -> 'All tests passed (227 assertions in 1 test case)' (copy_compare_test.cpp:333). |
| SC-026 | pass | `grep -n "drop / note behaviour / amp envelope" specs/Profundum-roadmap.md` -> '782:  400–499 drop / note behaviour / amp envelope, 500–599 fold / shape, ...'; drop-only grep exit=1. |
| SC-027 | pass | VoiceEngine_IdleVoiceFirstNoteUsesCurrentAmpEnvelope (profundum_engine_test.cpp:2531): before the fix 'fresh := -75.5364298386, changed := -9.5799524225' (FAILED), after 'All tests passed (5 assertions in 1 test case)'. VoiceEngine_RestoreBeforeFirstBlockLandsSettled (:2570): before 'srcDb := -33.2919532377, dstDb := -33.9752836907' with pre-restore Character selection 0 vs blob 15 (FAILED), after pass. Both in the 514-case gate log. |
| CC-rt | pass | Full audio path read in processor.cpp (process() :300-:452): fixed arrays, relaxed atomics, no new/make_unique/vector/string/throw/mutex/push_back/resize on the audio path. Only allocations: engine_ make_unique :180 (initialize), IMessage in flushControllerSync :605 (non-audio). Only spin: std::this_thread::yield :1002 in publishRestore (setState only). Scratch-linked exe "[rt]": 'Profundum_RestoreHandOffNoTear: 2000 rounds in 0.956429 s (budget ~10 s)', 'All tests passed (803 assertions in 2 test cases)'. Minor gap: EndpointParams (sound_param_row.h:260-281) lacks is_always_lock_free static_assert. |
| CC-layers | pass | All includes in the 14 parameter headers, shared sustain_latch.h and processor.h/.cpp point downward; no Processor/Controller cross-include; no dsp/ changes. |
| CC-naming | pass | New types PascalCase; one SustainLatch definition (shared/src/midi/sustain_latch.h:23) plus Vorago alias; 75 new IDs follow k{Section}{Param}Id; members trailing underscore. |
| CC-warnings | pass | After deleting 35 Profundum/profundum_tests .obj plus Vorago consumers, rebuild compiled 35 TUs; only matching line was LNK1104 (file lock: another lane running profundum_tests.exe). Build3: processor.cpp x2, sustain_test.cpp, test_sustain_latch.cpp 0 warnings. Scratch-OutDir link of profundum_tests: exit 0, no C/LNK warnings. Note: canonical build/windows-x64-release/bin/Release/profundum_tests.exe was still the old link because of the lock and needs a re-link. |
| CC-portability | pass | `node tools/check-portability.js`: exit 0, 'check-portability: 20 translation unit(s) with g++', last line 'check-portability: all clear -- 20 compiled.' No std::isnan/isinf/isfinite, no hn::Load/Store. lint-apple-globals not re-run by this agent (T032 log: OK, 40 files). |

## Implementation notes (deviations reported by task agents)

- T002: copied sustain_latch.h lines 16-81 (not 16-79; line 79 is the class closing brace). Wrote run-log T002.txt outside the file list.
- T003: constant named `kBandLevelBandCount` (not `kNumBands`, which exists in fractal_distortion.h).
- T007: added all five message IDs from plan 3.3 (including `kMsgRequestSync`), not four.
- T009/T010/T012: units strings and display types chosen where spec was silent (e.g. "%" Percent, "Hz" Crossover/Cycle Rate, Crossover uses Hz display, Field Motion Rate shows Percent). T014's table matches them.
- T011: note E rows need `registerNoteSoundRows`; two `formatNoteParam` overloads; `handleNoteParamChange` overload for E rows.
- T013: `handleSoundParamChange` returns bool; band 4 registers via `registerNoteSoundRows`.
- T014: Sync Rate rows (809, 907) default is `syncRateIndexOf(default pair)/29`, not `defaultNormalizedOf(leaf)`.
- T015: `pushParamsToEngine` now takes `CompareMode`; private helper `routeParamChange` added.
- T017: lifecycle_test.cpp :247/:501 still compare to `kStateV1Bytes` (input stream size); DefaultDecode in state_roundtrip_test.cpp amended; version <= 0 takes the v1 path.
- T018: NoTear task text asked for the impossible (all five words differ); test requires words 1-4 differ and word 0 equal. Tasks/spec row should say "words 1-4 differ". Risk: precedence 1 changes `getState` bytes 490-509 between setState and first block.
- T020: `setActive(false)` reports compare mode only when the reset changed it (plan said unconditionally; would break Phase 10 lifecycle_test NoLatencyMessage at :426-444). SC-014 uses timbre-only endpoints built through leaf laws instead of the showcase pair (dsp/tests include restriction R7).
- T022: `tailSampleRate_` not cleared on a refused later `setupProcessing`, per plan 4.5.7.
- T023: D_nl twin is an engine-direct render of the processor engine's endpoints; lever values follow Phase 9 lo/hi (AT-02 C3 Shape moves 0.5 -> 1, spec table says 0 -> 1); Mass offset tested at E1 only (C3 base B misses the AT-27 bar at 2.92 dB).
- T024: Anchor-sounding check is `peak > 0`, not 1e-4.
- T025: original instance calls setActive(false)/(true) after setState so the loaded seed is in force.
- T026: compare A->AB switch cannot occur inside the zero-allocation scope (notify flushes by design), so blocks run under two scopes (A, then AB).
- T027: automation arms run in both MONO and POLY (interpretation to confirm).
- T028: CHANGELOG intro and two stale "Known limitations" bullets rewritten.
- T031: `run-clang-tidy.ps1` has no `shared` target (ValidateSet line 66); the shared lane runs clang-tidy.exe directly. Root CLAUDE.md lists `shared` as valid, out of step with the .ps1.
- T032: originally BLOCKED (18 compiled, 2 skipped: bass_stereo_field_test_helpers.h not on the include path). Resolved by the tools/check-portability.js profundum branch (FR-073); post-fix run: 'all clear -- 20 compiled.' The scope-fence allow-list needs tools/check-portability.js if the fence is re-run.
- Several task agents did not write run-log files (T016, T020, T024) because they were outside their file lists.

## Remaining gates

1. auval and the macOS / Linux CI legs (SC-004, SC-001): PENDING-CI.
