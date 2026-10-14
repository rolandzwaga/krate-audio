# Compliance: Profundum Phase 4 (profundum-phase4-drop-note-behaviour)

**Overall status: INCOMPLETE** (2 partial items, 0 fail)

## Honest gap list (first)

1. **SC-020 PARTIAL.** `VoragoMacro_SweepAxes` fails: "Movement per-band total variation rho=0.8667" against 0.9 (vorago_macro_test.cpp:1318), 1 failed, 2002 s. This is the same figure as the inherited record in profundum-phase2-edge-fold/compliance.md:14, measured on main 89b512d1 before Profundum. No Vorago file is modified and no Vorago source includes the changed headers. spec.md:1012-1028 classifies it INHERITED. The SC text says all of dsp_systems_tests must pass and its [long] lane does not, so this cannot be a pass.
2. **SC-021 PARTIAL.** The row needs a user listening verdict, which an agent cannot supply. No verdict is recorded (grep of the phase dir finds only the requirement text, spec.md:649). Renders, WAV/CSV files and the (b) bound are in place.
3. Caveats on passing rows: the full `-Target dsp` clang-tidy sweep was not re-run (T027.txt:6-8 records it hanging on a non-Phase-4 TU); clang-tidy was run on the 12 Phase 4 TUs only. Task notes for T026 and T027 report "blocked" for their first executions; the evidence below comes from later epochs and re-runs, and the T028 CPU lane was deferred by the executor and measured by the main loop (T028.txt epoch 3). SC-013 prints "SC013-R2-DEFECT" diagnostics (up to 0.4495 deg against a 0.3 investigate threshold) that are not gated.

## Compliance table

| ID | Verdict | Evidence |
|---|---|---|
| FR-001 | pass | drop_envelope.h:1-2,19-20,27-29: Layer 2, includes only core/db_utils.h and core/drop_curve.h, class DropEnvelope in Krate::DSP; every public method (57-159) noexcept. |
| FR-002 | pass | drop_envelope.h:57-79: prepare clamps to [22050,192000] then reset(); process returns 0.0f before prepare; fixed-size state (drop_curve.h:41,81). DropEnvelope_Lifecycle passed: "All tests passed (63989 assertions in 41 test cases)". |
| FR-003 | pass | drop_envelope.h:83-98, 238-244: sanitize() keeps previous on non-finite, clamps [0,48]/[1,4000]/[0,1]/[0,1]; latched in trigger() (113-114). "[drop-nonfinite]: All tests passed, 17243 assertions in 5 test cases". |
| FR-004 | pass | drop_curve.h:286-301 blend thirds with we=3s-2; family regions 219-246; elastic knots 370-400, kElasticPeakRatio 0.45 < kElasticDecay 0.5 (static_assert 195). ShapeFamilies and ShapeContinuity passed. |
| FR-005 | pass | drop_curve.h:283 rhoR = 0.25*R*A; line 379 first undershoot rhoR + we*epsA; settle 0.5T (376). DropEnvelope_Rebound passed. |
| FR-006 | pass | drop_curve.h:248-265, 364-365: cubic Hermite pieces, landing fillet last 5% (kLandingFilletFraction, line 46). DropEnvelope_C1, _C1CheckerPositiveControl, _RetriggerContinuation passed. |
| FR-007 | pass | drop_envelope.h:103-121 and drop_curve.h:306-325, 348-359: bridge brake L_br, smoothstep rise over L_min, onset fillet. RetriggerContinuation and EveryCellRetrigger passed. |
| FR-008 | pass | drop_envelope.h:124-148: [[nodiscard]] process, processBlock null/zero no-op, isActive cleared at endSample. BlockInvariance blocks {1,37,512} passed. |
| FR-009 | pass | No new/lock/throw/IO in drop_envelope.h; detail::isFinite and opaqueFloatBits, never std::isnan; TU in -fno-fast-math block. "[drop-nonfinite]" 17243 assertions passed. |
| FR-010 | pass | drop_curve.h:279 N = timeMs*fs/1000 in double, u = n/N. DropEnvelope_RateInvariance (44.1/48/96 kHz) passed. |
| FR-020 | pass | mono_handler.h:74 enum class GlideMode {ConstantTime=0, ConstantRate=1, Quantized=2}; setGlideMode 357, glideMode 374. T026.txt check-append-only: "check 1 (no removed line): PASS; check 2 (public surface): PASS (added public: glideMode, setGlideMode)". |
| FR-021 | pass | ConstantTime path unchanged (guards at 497 etc.); frozen reference mono_handler_pre_phase4_ref.h; seeded 10000-event script at mono_handler_glide_test.cpp:467-502. GlideModeConstantTimeUnchanged passed; [mono_handler] 260 assertions in 51 test cases passed. |
| FR-022 | pass | mono_handler.h:613 gpRate_ = 12000/(T*fs); double position from uint64 counter (554). GlideModeConstantRate passed. |
| FR-023 | pass | mono_handler.h:626-647 step k at ceil(k*N/abs(delta)), integer-note output. GlideModeQuantizedSteps and _QuantizedRetarget passed. |
| FR-024 | pass | mono_handler.h:357-371, 663 enterGlideMode switch table. GlideModeSwitch passed. |
| FR-025 | pass | Diff touches only pitch law; no removed lines per check-append-only. GlideModeUnchangedSurfaces passed. |
| FR-026 | pass | Only new public members setGlideMode/glideMode; ConstantRate reuses portamentoTimeMs_ as ms per octave (613). |
| FR-027 | pass | specs/Profundum-roadmap.md:609-611 (Phase 11 section): "Amended 2026-10-13 (Phase 4 FR-027, SC-027): The single portamento knob is labelled per Glide Mode ...". |
| FR-028 | pass | Exactly three enumerators; setGlideMode rejects values above Quantized (358-360); static_asserts at mono_handler_glide_test.cpp:559-562. GlideModeEnum passed. |
| FR-030 | pass | note_memory.h:76-106 resolve is pure/noexcept; no mutable state beyond config (128-129). |
| FR-031 | pass | note_memory.h:35-41 Transition enum; harness handleEvent classification. HarnessClassification passed in "[note-behaviour]~[long]: 36365 assertions in 22 test cases". |
| FR-032 | pass | note_memory.h:24-32 Mode and Retrigger enums; defaults AlwaysRetrigger/Reset (128-129). |
| FR-033 | pass | note_memory.h:48-56 Actions with seven fields; derivation 94-104. |
| FR-034 | pass | note_memory.h:121-126 kMatrix matches spec table. NoteMemory_ResolverTable passed. |
| FR-035 | pass | note_memory.h:83 static constexpr pure resolve; 80-cell table test passed. |
| FR-036 | pass | note_memory.h:101,103 dropTrigger=env, anchorRearm=env regardless of retrigger. |
| FR-040 | pass | DeclickPositiveControl (spectral_test.cpp:2453) passed, exit 0, 22 cases. |
| FR-041 | pass | retrigger_declick.h (Layer 1), kDeclickMs=1.5f, raised cosine from double rotation. "All tests passed (636 assertions in 3 test cases)". |
| FR-042 | pass | Harness handleEvent step 5 defers pending_ to switchPos_. DeclickTransparency passed. |
| FR-043 | pass | arm(): "if (!prepared_ \|\| active_) return;". RearmNoRestart passed. |
| FR-044 | pass | note_memory.h:95 declick = phaseReset && voiceAudible; gain skipped while inactive. DeclickTransparency passed. |
| FR-045 | pass | One g multiplies core+Anchor sums identically; exactly 0 at switch sample. Caveat: gain applies to the sum, mathematically the same. |
| FR-050 | pass | Harness renderSub routing fRes_/dc_/fCore_/fAnchor_; test-only. Routing passed. |
| FR-051 | pass | Same a.phase to core and Anchor at same pos_; PhasePolicy mirror static_asserted at profundum_note_behaviour_test.cpp:377-380. ResetReference and Routing passed. |
| FR-052 | pass | d_c = 0 when idle; GlideLock60s "worst drift 0.145752 deg (bar 0.5)" (T026.txt), exit 0. |
| FR-053 | pass | drop_phase_lock.h:526-590 solve; PostDropLanding_E0 and _E1 re-run: "All tests passed (33410 assertions in 1 test case)", exit 0. |
| FR-054 | pass | drop_phase_lock.h:243-247 bump 16x^2(1-x)^2, bound 48 st. DropPhaseLock_C1 passed; PostDropLanding passed. |
| FR-055 | pass | drop_phase_lock.h: Layer 2, core includes only (53-54), noexcept methods; registered in dsp/CMakeLists.txt. |
| FR-056 | pass | drop_envelope.h:151,155-159 plannedCurve and phaseIntegralSeconds; no f_res state. PhaseIntegral passed. |
| FR-057 | pass | Bounded GL8 and node cache; T028.txt epoch 3: "SC-025 T1_us=6.451 T4000_us=6.825 ratio=1.058" (bar 2x). |
| FR-058 | pass | drop_phase_lock.h:115-124,147-166 bitwise bypass. FollowBypass passed; FollowAfterDrop [long] 9908 assertions, exit 0. Ruling (A) 2026-10-13 applies to the Anchor level feed; AnchorUntouched passed. |
| FR-059 | pass | drop_phase_lock.h:536-590 SnapReset zeroes accumulator, bridged Hermite carry. TriggerKinds and Mirror passed; PostDropLanding (d) cases passed. |
| FR-060 | pass | Audibility (spectral_test.cpp:2597) and PitchEstimatorSelfTest in the 36365-assertion run. |
| FR-061 | pass | Same g and e on L and R; no stereo state in Phase 4 headers. MonoIdentity passed. |
| FR-062 | pass | TrajectoryNyquist [long]: "All tests passed (4440 assertions in 1 test case)", 191 s; 24 sc015-*.csv. |
| FR-063 | pass | innexus_tests "All tests passed (1111855 assertions in 565 test cases)"; ruinae_tests (CI filter) "29205 assertions in 732 test cases"; [mono_handler] 51 cases. T026.txt epoch 3: processors exit 0 (3409 cases), seraphis exit 0 (109), vorago exit 0 (119). systems_rest had 1 failure, a SeraphisEngine wall-clock clause under load, which passed alone ("20 assertions in 1 test case"). |
| FR-064 | pass | RetriggerClickFreeSmoke, GlideClickFree, DeclickPositiveControl passed; GlideClickFree_E0 [long] 147096 assertions exit 0; RetriggerClickFree_* [long] green per T026.txt epoch 3. Gate is SC-010(b) per ruling (B); (a) recorded not gated (spectral_test.cpp:1708-1712). |
| FR-065 | pass | No heap/lock/throw in Phase 4 headers; AllocationScope tests passed (DropPhaseLock_NoAllocation, MonoHandler_GlideModeNoAllocation, ProfundumNoteBehaviour_NoAllocation). |
| FR-066 | pass | T028.txt epoch 3 isolated: "PROFUNDUM_DROP_PERF SC-019 pct=0.0286 ns_per_sample=5.954" (bar 0.05%). T023 epoch 7 lanes 0.0376 / 0.0382 / 0.0526%, median 0.0382%. |
| FR-067 | pass | T027.txt: "check-portability: all clear -- 12 compiled." and "lint-apple-globals: OK — 24 files, no macOS-only name collisions"; clang-tidy 0 warnings on Phase 4 TUs. |
| FR-068 | pass | 8 WAVs plus CSVs in evidence/ (usecase-{808-attack,cinematic-impact,techno-pluck,kick-bass-hybrid}-{E1,C2}). UseCaseRenders passed. |
| FR-069 | pass | dsp/tests/CMakeLists.txt registrations in the right layer targets; CPU cases [.perf], multi-minute shards [long]. |
| FR-070 | pass | harmonic_oscillator_bank.h:701 opt-in setContinuousFrequencyUpdates, reconcileContinuity 1276; profundum_core.h:140. "worst partial -0.0001 dB (bar 0.05); default path worst -5.82 dB (control > 1)"; "162 cells, worst partial -0.2460 dB", "worst energy 0.0007 dB (bar 0.05)". |
| SC-001 | pass | dsp_processors_tests "All tests passed (10802834 assertions in 3409 test cases)"; bars asserted at drop_envelope_test.cpp:209-359. Tests print no figures. |
| SC-002 | pass | DropEnvelope_AmountTime passed; grid at drop_envelope_test.cpp:366-428. |
| SC-003 | pass | DropEnvelope_Rebound passed; rho 0 / 1.551 / 2.881 / 5.984 st for R 0 / .25 / .5 / 1. |
| SC-004 | pass | DropEnvelope_C1, C1CheckerPositiveControl, RetriggerContinuation passed; C1CheckerCrossCheck "1946 assertions in 1 test case". |
| SC-005 | pass | BlockInvariance, RateInvariance, Latching passed in the 3409-case run. |
| SC-006 | pass | GlideModeConstantTimeUnchanged: mismatches==0 (:603), >100000 comparisons; mono_handler_test.cpp unmodified. |
| SC-007 | pass | GlideModeConstantRate passed; duration within 1 sample (:754,:776), slope within 1e-6 relative. |
| SC-008 | pass | GlideModeQuantizedSteps, GlideModeSwitch, GlideModeEnum passed. |
| SC-009 | pass | "SC-009(b): 17 Reset (worst 0.000000 deg vs fresh), 6 FreeRunning same-note (bitwise), 25 FreeRunning note changes (worst advance error 0.000270 deg), worst Anchor step error 6.491e-11 cycles". PolyVoiceReuse 157 assertions passed. |
| SC-010 | pass | Per ruling (B), (a) recorded not gated. RetriggerClickFree_E0/E1/E2/C3: "SC-010(b) 4608 windows, 0 over the bound", worst ratio 0.7133/0.8243/0.8536/0.8518. GlideClickFree: "12888/12840/12840/12840 step/switch windows, 0 over the bound", worst 0.6892/0.7759/0.8200/0.8254. Positive controls ratio 1.99, 1.99, 2.11. |
| SC-011 | pass | RetriggerDeclick_Timing passed; DeclickTransparency 256 assertions; "onset latency 71 samples (bar 73)". |
| SC-012 | pass | ResetReference and Routing passed: worst theta 0.441078 deg (bar 1.0); Anchor vs f_res 0.000117 cents (bar 1); accumulated phase 3.731e-07 cycles (bar 1e-6); drift -0.010388 deg (bar 0.5); GlideLock60s 0.151454 deg. (No test named PhaseReference exists.) |
| SC-013 | pass | PostDropLanding_E0/E1/E2/C3 (33410 assertions each) and PunchGhostE1 (66818) pass. E0: "384 cells, worst theta 0.354723 deg (bar 1.0), worst drift 0.010450 deg (bar 0.5), worst bypass theta 179.545 deg". Other shards worst theta 0.448655 / 0.534086 / 0.500401. |
| SC-014 | pass | Audibility "1299 assertions": Amount 48.0000 st; Time 97x; Shape 0.74; Elastic 3.67705 st; Rebound 5.98441 st; Retrigger gaps 57.75/38.95/72.85 dB (bar 20); Note Memory 31.57 dB (bar 3); Glide at T/2 CT 11.994, CR 6.000, Q 12.000 st. |
| SC-015 | pass | "SC-015 grid: 384 cells, worst aliased -123.65 dBFS ... bar -96"; smoke "32 cells, worst aliased -130.79 dBFS". |
| SC-016 | pass | MonoIdentity "128 renders, SC-016 L != R in 0 (bar 0)"; also 0 in every SC-010 shard. |
| SC-017 | pass | All five non-finite cases passed in the 3409-case run; TU built with -fno-fast-math -fno-finite-math-only. |
| SC-018 | pass | ProfundumNoteBehaviour_NoAllocation "7 assertions in 1 test case"; "0 allocation(s) over 5 waveforms x 4 modes". |
| SC-019 | pass | Three isolated run-cpu-tests lanes: pct=0.0365 / 0.0334 / 0.0357, median 0.0357% (bar 0.05%). |
| SC-020 | **partial** | See gap 1. All other per-push lanes exit 0 (processors 3409 cases, systems 36365/22 plus 10134537/1577, ruinae 29205/732, seraphis 444660/109, vorago 4060395/119, innexus 1111856/565, membrum 119419/630, core 1588248/565, primitives 4562191/1501); all 16 Phase 4 [long] shards pass; diff is 308 insertions, no deletions. Still red: VoragoMacro_SweepAxes rho=0.8667 vs 0.9 (inherited). |
| SC-021 | **partial** | See gap 2. UseCaseRenders passed (36365 assertions); worst (b) ratio 0.7495; pairs distinct (e.g. "cinematic-impact vs kick-bass-hybrid: d start 12.000 st, t_s ratio 14.881"). No user listening verdict recorded. |
| SC-022 | pass | check-portability: "all clear -- 12 compiled." rc=0; lint-apple-globals "OK — 24 files". |
| SC-023 | pass | profundum_note_behaviour_test.cpp:373-380 static_asserts; "All tests passed (1 assertion in 1 test case)". |
| SC-024 | pass | PhaseIntegral (rel<=1e-6, cells==7020) and PredictedExcessPhase passed. Tests print no per-cell figures. |
| SC-025 | pass | Isolated lanes: ratio=0.968, 1.172, 1.078; median 1.078 (bar 2x). |
| SC-026 | pass | Per ruling (A): a1-scaled diff -139.68/-142.24/-139.68 dB (bar -120) in AnchorUntouched; shards -138.19 to -142.12 dB; raw diff recorded, worst -8.63 dB. FollowAfterDrop "worst dTheta 0.010176 deg, drift 0.080788 deg". |
| SC-027 | pass | specs/Profundum-roadmap.md:610-612 carries the amended per-mode portamento labelling clause. |
| SC-028 | pass | "SC-028 spectrum across Drop: 162 cells, worst partial -0.2460 dB (bar 0.1 within 40 dB of h1, 0.5 dB below) ..., worst energy 0.0007 dB (bar 0.05)". |
| CC-rt | pass | No allocation/lock/throw/IO in the five new headers or glide additions; bounded solve work. Caveat: solve() is a lazy burst after a trigger, bounded; its cost is covered by SC-019/SC-025. |
| CC-layers | pass | Every include points downward; each header registered in its layer list in dsp/CMakeLists.txt. |
| CC-naming | pass | PascalCase classes, trailing-underscore members, kPascalCase constants; ODR sweep finds one definition each. Nits not counted: k00/k01/k11 table constants, local `lim`. |
| CC-warnings | pass | Rebuild: grep for compiler warnings returned 0; clang-tidy on 12 Phase 4 TUs 0 warnings. Full `-Target dsp` sweep not re-run (hung on a non-Phase-4 TU per T027.txt:6-8). |
| CC-portability | pass | `node tools/check-portability.js`: "check-portability: all clear -- 14 compiled." exit 0. |

## Implementation notes (deviations reported by task agents)

- T002/T003: mono_handler_pre_phase4_ref.h generated from 3d8ca19f by extract-mono-ref.js; check-append-only.js enforces no removed lines and public surface {setGlideMode, glideMode}.
- T004 skipped per O-1 ruling (2026-10-12): no gamma_cap, no sc013-infeasible.json; no upward-rate floor (kMaxUpwardGlideSemitonesPerSample omitted; ConstantRate and Quantized have none).
- T007-T009: private helper state added (gpRate_, qNextStep_); Quantized holds only the next step index rather than a 128-entry array; entering ConstantTime is verified as sample-for-sample equal to a snapped-and-retargeted reference LinearRamp, not gated at T+-1 sample (LinearRamp drift 23832-24136 vs 24000 samples at T=500 ms).
- T011: Shape anchors passed as the largest float at or below the anchor (float(2/3) would land in the elastic segment); parameter defaults chosen A=0, T=100, s=0, R=0; plan S3.2 v2 rule gives a K2 slope jump as s crosses 2/3 when R>0 (not gated, SC-001 continuity runs at R=0).
- T012: float allowance added to the C1 checker bound; FR-007 round() for L_br allows up to ~0.54 st brake excursion with L_br>1 (spec contradiction, recorded only); E-4 every-sample retrigger stalls d at ~2 st.
- T015/T016: test-side staircase window [0, ceil(D_s)+16); gamma gate clamps at 48 st where analytic gamma is unbounded; accumulator checks use 1e-14 relative tolerance under fast-math.
- T017: DropPhaseLock::process(NaN) in bypass/unprepared returns NaN (bitwise passthrough per FR-058); MonoHandler::prepare(NaN) has no guard (shared header, out of scope).
- T020/T021/T022: SC-010 sample-delta bound uses Anchor mode-gain Gm/Sm read from the control trace (not purely analytic); SC-014 f_zc agreement uses 10 cents; R-2 staircase-model gap diagnostics (up to 0.4495 deg) printed, not gated.
- T023 epoch 7: rulings A-D applied (a1-scaled SC-026 gate, SC-010(a) recorded not gated, SC-014 Retrigger reference, SC-019 median of three isolated lanes). Three CPU levers kept (per-piece span processing, three-loop run(), out-of-line Quantized step); three reverted for no gain. Each isolated lane had one machine-dependent red case (TriggerBurst 1.705/1.392/1.075 vs 1.5; passes alone at 0.698).
- T024: roadmap Phase 9 and Phase 11 handoff clauses present; the "voice tracks the core's pitch-grid phase for DropPhaseLock::trigger" requirement was dropped by the ruling's collapse and may need a follow-up edit.
- T001: lock2all corrected worst case measured 0.7116 deg, strictly above the plan's "<= 0.71".
- T026/T027/T028 executors returned blocked/deferred; later epoch-3 runs and main-loop runs supply the evidence cited above.

## Remaining gates for the human loop

- Resolve SC-020 (inherited VoragoMacro_SweepAxes rho=0.8667 vs 0.9: accept as inherited by ruling, or fix in a Vorago task).
- SC-021: user listening verdict on evidence/usecase-*.wav.
- clang-tidy: full `./tools/run-clang-tidy.ps1 -Target dsp` sweep (run detached) if wanted beyond the 12 Phase 4 TUs.
- Commit on `synth/profundum` (no push without permission).
- pluginval: not required (no plugin code changed; phases 11+).
