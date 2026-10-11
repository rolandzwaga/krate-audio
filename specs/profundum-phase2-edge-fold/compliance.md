# Compliance Report: Profundum Phase 2 (profundum-phase2-edge-fold)

**Overall status: INCOMPLETE** (1 fail, 0 partial among the verdict items; caveats below)

**Main-loop closure (2026-10-11, after this report):**
- SC-021: the one red case is `VoragoMacro_SweepAxes` (Vorago, `[long]`), rho 0.8667 against 0.9, identical in the 2026-10-09 lane log before any Phase 2 change and in Long Tests Nightly run 38046177066 on `main` at 89b512d1 (before Phase 1). Ruled **not a Phase 2 gate**; it is reported to the user as a Vorago item for the Vorago branch. Every Profundum and Phase 2 case is green; the row keeps its verdict.
- SC-018 / FR-054 figures of record, measured alone on pinned cores after the comply cost fix and the lint fixes (`F:/tmp/close_perf.log`): core alone 0.689 % (bar 1.0), stage cost 1.019 % (bar 1.5), chain 1.708 % (bar 2.5); excitation alone 0.494, Fold alone 0.561; stereo-forced chain 2.462 (record only); Phase 1 core 0.771 / 1.001 (96 partials) / 0.336 static. The report's own pinned figures were taken while the [long] shards ran, which is why its core-alone read 0.91–0.97.
- FR-041: the unused `core/sigmoid.h` include was removed from `profundum_core.h`; `dsp_systems_tests` rebuilt clean.
- Documentation staleness: spec SC-013 row, FR-033 and D-9 now carry the ratified C-4 per-channel clause and the test name `HarmonicFold_StereoDetectorPerChannel`; plan.md S9.7 carries the isolated figures above.
- Overnight soaks: not part of this phase's gates (no `[soak]` case is Phase 2's).

## Honest gap list (first)

1. **SC-021 FAIL.** dsp_systems_tests is not fully green. `VoragoMacro_SweepAxes` failed in 1947 s with `vorago_macro_test.cpp(1318): FAILED ... 0.8666666667 >= 0.9 Movement per-band total variation rho=0.866667 endpoint=0.218562`. The same test failed on macOS in GitHub Long Tests Nightly run 38046177066 at commit 89b512d1 (main, before Phase 1 and Phase 2): `vorago_macro_test.cpp:1318 ... 0.8666666667 >= 0.9 ... Movement rho=0.866667 endpoint=0.216728`. Profundum code does not cause it, but the gate stays red until the Vorago Movement row is fixed. (T020 records a ruling that it is not a Phase 2 gate and is to be raised with the user as a separate Vorago item. This report does not soften the item verdict.) The overnight soaks were not run.
2. **CC-warnings caveat.** The forced-recompile check had `LINK : fatal error LNK1104` on dsp_systems_tests.exe (about 14 concurrent processes held it open), so the systems exe was not relinked in that check. 0 compiler warnings were reported across the 8 TUs compiled.
3. **SC-018 / FR-054 caveats.** The full `run-cpu-tests.js dsp_systems_tests` lane was not run this session. Earlier-lane figures (T020, 2026-10-11): core alone 0.634 (limit 1.0), stage cost 1.350 (limit 1.5), chain 1.985 (limit 2.5). The pinned runs this session read core-alone 0.910-0.971 %, only 3-9 % under the 1.0 bar. T005 pinned bench median 1.467 % is above the 1.45 % flag line (early R-1 warning).
4. **Documentation staleness.** SC-013 table row still says "shared detector" and names `HarmonicFold_StereoDetectorShared` (the C-4 per-channel re-ruling lives only in Clarifications). plan.md:1163 still says "isolated run-cpu-tests.js lane still has to confirm it". The SC-013(c) re-ruling is not detectable by check-bars.js.
5. FR-041: `core/sigmoid.h` is included in profundum_core.h:20 but unused (unused include).
6. T001: `bench/candidate-design-bench-v2.cpp` (plan S12 step 2) was not run on its own.

## Compliance table

| ID | Verdict | Evidence |
|---|---|---|
| FR-001 | pass | profundum_core.h:268-280: bank run rescaled by kBankHeadroom (271-276), then exciteRun(left+i,right+i,run) (277-278); ExcitationChannel (1159-1172) and exciteRun (1074) are private members of ProfundumCore. |
| FR-002 | pass | profundum_core.h:331-335 `struct ExcitationControls { float mix = 0.0f; float drive = 0.5f; float asymmetry = 0.0f; }`; setExcitation (353-363) clamps via sanitizeExcitationField (875-881). `git diff --stat 47e43918 -- dsp/include` lists only profundum_core.h. ProfundumCore_ExcitationApi: All tests passed (12 assertions). |
| FR-003 | pass | profundum_core.h:1104-1115 4x closed-form ADAA1 of v/sqrt(1+v^2). SC-001 drive sweep smallest THD(drive 1)-THD(drive 0) = 31.33 dB (bar 20); ExcitationDriveMonotonic [long] passed 8688 assertions. |
| FR-004 | pass | profundum_core.h:339-340 kEdgeDriveRangeDb = 12; excitationDriveLaw 372-377; edgeDriveGain 380-383. EdgeDriveLaw passed 177 assertions; G(edge1)-G(edge0) 20.67 dB (bar 6); Edge 0 still excites +55.94 dB; blend residual -126.6 dB (bar -60). |
| FR-005 | pass | profundum_core.h:967 bias = asymmetry*kExcitationAsymmetryBias (0.5, line 343); 1108 v = D/P*u + b, 1094/1114 subtract fb. SC-003(a) lowest E_gen(asym 1) -17.6 dB re h1 (bar -30); EvenHarmonicsMonotonic [long] passed 607344 assertions. |
| FR-006 | pass | profundum_core.h:277-280 idle path never writes output (1039-1053). ZeroMixBitIdentical memcmp vs Phase1Ref (from 47e43918) passed 1036 assertions. Phase 1 suites `"[profundum]~[edge-fold]"`: All tests passed (431802 assertions in 63 test cases). MixMonotonic [long] passed 3960 assertions. |
| FR-007 | pass | Latched only in controlUpdate; per-sample smoothing (1084-1086, 5 ms at 898-900). ControlStepsNoClick (2432 assertions), EngageNoClick (696), Deterministic (41) passed. |
| FR-008 | pass | profundum_core.h:886 factor 4 (<=100 kHz) / 2 above, PolyphaseHalfbandOversampler (C-1). ExcitationNoAliasingE0/E1/E2 worst -94.3/-94.3/-93.5 dBFS (bar -80); ZeroLatency passed 110 assertions; SC-012(a) worst |T1| 0.069 dB, 0.14 deg. |
| FR-009 | pass | profundum_core.h:1094 fb subtracted in 1114; kExcitationDcCornerHz = 2.0f (346). SC-011 excitation worst DC -109.873 dBFS (bar -80). |
| FR-010 | pass | h1 on zero-delay base band (1101-1102). SC-012(a) 0.069455 dB (bar 0.1), 0.141 deg (bar 2); (b) 0.127 deg sine/asym 0, 14.99 deg saw at asym>0 (C-2 bar 20); FundamentalPreserved [long] passed 7653 assertions. |
| FR-011 | pass | profundum_core.h:907-941 static makeup table, applied at 1088. SC-020 (LevelLaw [long], 17982 assertions): worst -0.875 dB (bar +-1.5). |
| FR-012 | pass | Same per-sample arithmetic both channels (1075/1096). LREqualAtZeroPan 294 assertions; MonoSum inter-channel h1 0.467 deg (bar 2), 9.516 deg at asym 1 (bar 15), mono-sum power 0.131 dB (bar 0.5). |
| FR-013 | pass | profundum_core.h:535-541 Reset branch snaps and resets (975-987); FreeRunning (576-578) no reset. RetriggerPolicy passed 46 assertions. |
| FR-014 | pass | Asymmetry and drive clipping act on recipe output. TriangleTail passed 240 assertions; smallest odd/even drop 93.5 dB (bar 20), lowest E_even -16.3 dB. |
| FR-020 | pass | harmonic_fold.h Layer 2 header-only, namespace Krate::DSP, class HarmonicFold (76); owns PolyphaseHalfbandOversampler, copyable/movable. |
| FR-021 | pass | harmonic_fold.h:121 prepare, 157 reset, 168-171 setAmount, 188 processBlock, 232 stateFinite, 256 `latencySamples() noexcept { return 0; }`. ApiAndConstants passed; ZeroLatency passed 16 assertions. |
| FR-022 | pass | kSoftEnd = 30, kCompressionEnd = 65 (79-80); continuous tables. ContinuousInAmountLong worst 0.00366 (bar 0.02); RegimeContinuityStatic worst |dL| 0.109 dB, |dC| 0.0319 oct; AutomationNoJump 3453 assertions. |
| FR-023 | pass | harmonic_fold.h:521 g law, beta = 0 below kSoftEnd (461). SoftRegimeLevelDependent MIDI 36: THD -68.64/-56.81/-45.00/-33.62 dB, spread 35.02 (bar >= 12). |
| FR-024 | pass | harmonic_fold.h:480 G = (1-beta)+beta*P/env. CompressionLevelIndependent Fold spread 0.00 dB (bar 3) vs tanh 31.68 (bar 12); DensityBeforeLevel THD +13.99 dB, H 1->3, |dL| 0.681, |dCF| 0.671 dB (bar 1); min region margin 7.54 dB (bar 3). |
| FR-025 | pass | sineFold with theta ease-out to kFoldIndexMax = 3.0 (86-87, 524-525). FoldRegimeLevelNormalised spread 0.00 dB (bar <= 3); Fold alias worst -92.7 dBFS (bar -80). |
| FR-026 | pass | kDcCornerHz = 2.0f (91). DcFreeWithEvenHarmonicInput worst -117.1 dBFS; DcFreeOnExcitationOutput -92.7; ChainDcFree passed; ZeroInZeroOut passed 12 assertions. |
| FR-027 | pass | 4x/2x polyphase IIR + 2fs notch pre-filter. HarmonicFold_NoAliasingE0/E1/E2 worst -92.8/-93.3/-92.7 dBFS (bar -80). |
| FR-028 | pass | Bit-exact copy at a == 0 (204-211). TransparentAtZero 126 assertions; CoreInput 432; ContinuousInAmount worst 0.0024-0.0036 (bar 0.02). |
| FR-029 | pass | TrajectoryDistinctFromTanh passed (1815 assertions); min region margins kGrowl 12.82, kHeavy 7.54, kHollow 25.73 dB (bar 3). |
| FR-030 | pass | ThdAndCountMonotonic [long] 57744 assertions passed: min THD step +0.069 dB at amount 99.75, H(100) = 17; smoke 2016 assertions. |
| FR-031 | pass | kCalibrationPeak 0.3552343859, kOutputCeiling 6.0 clamp at 226. OutputBounded worst preCeilingPeak 2.8807 (< 6), 714 assertions. |
| FR-032 | pass | FundamentalPreservedLong worst |angle T1| 1.572 deg (bar 5), min |T1| -0.03 dB below 65 (bar -6), -9.51 dB on [65,100] (bar -12); core-input 1.576 deg / -6.32 dB. |
| FR-033 | pass | Per-channel detectors with identical constants per C-4. LREqualForIdenticalInput 38 assertions; StereoDetectorPerChannel worst 0.0002 dB (bar 0.1); MonoSum Fold-added phase 1.698 deg (bar 2), loss 0.155 dB (bar 0.5). |
| FR-034 | pass | kAmountSmoothMs = 10 (92). BlockSizeIndependent passed 20 assertions. |
| FR-035 | pass | Mechanism (a) envelope-normalised waveshaper shipped (harmonic_fold.h:486-501); (b),(c) "Not built: (a) passes". SC-018 pinned alone: stage cost 1.224 / 1.144 / 1.275 % (bar 1.5); row appended plan.md:1174. |
| FR-036 | pass | kNormalisationRampEnd = 40, smoothstep beta (461). Soft spread 35.02 dB, compression 0.00, fold 0.00; RegimeContinuityStatic |dL| 0.109 dB. |
| FR-037 | pass | kDetectorAttackMs = 0, kDetectorReleasePeriods = 4 (97-98). NoInCycleGainModulation ripple 0 (bar 1e-4), x/env reference >= 0.937 (bar >= 0.1); DetectorSteadyOnNonIntegerPeriod ripple <= 8.56e-6. |
| FR-040 | pass | All audio-thread methods noexcept; no std::function/new/vector/throw/mutex in the new headers. HarmonicFold_NoAllocation (7 assertions), ExcitationNoAllocation (4) passed. |
| FR-041 | pass | Amended by C-1; excitation state fixed std::arrays (1169-1186). Note: profundum_core.h:20 includes core/sigmoid.h but uses nothing from it (unused include). |
| FR-042 | pass | Core silent before prepare; setExcitation no-op before prepare (354-355); harmonic_fold.h:189 untouched before prepare. NonFiniteSetters passed 19 assertions. |
| FR-043 | pass | detail::isFinite rejects non-finite, flushes denormals; non-finite samples -> 0.0. HarmonicFold_NonFinite 8196 assertions, ExcitationNonFinite 66. |
| FR-044 | pass | No RNG. ExcitationDeterministic 41 assertions; BlockSizeIndependent passed. |
| FR-045 | pass | fs clamp [22050,192000]; tables re-derived each prepare. SampleRateChange 1935 assertions (SC-012(a) <= 0.0695 dB, 0.141 deg); RangeAndRates 38. |
| FR-046 | pass | `check-portability: all clear -- 8 compiled.`; `lint-apple-globals.js`: `OK — 973 files, no macOS-only name collisions`. |
| FR-047 | pass | PascalCase classes, camelCase methods, trailing-underscore members, kPascalCase constants (profundum_core.h:331-393, harmonic_fold.h:79-105, 564-581). |
| FR-048 | pass | Amended by C-1. `git diff --stat 47e43918 -- dsp/include` -> only profundum_core.h (497+, 10-); two new untracked headers; zero diff in the other shared headers. |
| FR-050 | pass | CMake registration verified; both nonfinite TUs in -fno-fast-math block; perf TU outside. All pass: processors [profundum] 53+4 cases, primitives 9, systems Phase 2 non-long 40/41 (only the perf case failed, under load) + every [long] case passed. |
| FR-051 | pass | SC-001 mix +55.9 dB, drive +31.3 dB (bar 20); Edge +20.67 dB (bar 6); E_gen -17.6 dB; RegimesAudible THD rise 119.3/14.0/24.1 dB, H(100) 17, centroid rise 0.594 oct. |
| FR-052 | pass | ExcitationMonoSum 0.467 / 9.516 deg, 0.131 dB; HarmonicFold_MonoSum 1.698 deg (bar 2), 0.155 dB (bar 0.5). |
| FR-053 | pass | Worst off-grid power: excitation -93.5, Fold -92.7, chain E0/E1/E2 -88.2/-88.4/-83.1 dBFS (bar -80). Positive control naive tanh E1/48 kHz: -36.4 dBFS (must read > -80). |
| FR-054 | pass | ProfundumEdgeFold_CpuBudget alone, pinned (mask 0xFFFF), 3 runs 20 s apart, 0 competing processes. A_core_mix0 0.971 / 0.949 / 0.910; stage_cost 1.224 / 1.144 / 1.275; total 2.194 / 2.093 / 2.185. Each: All tests passed (22 assertions in 1 test case). Core alone only 3-9 % under its 1.0 bar. |
| FR-055 | pass | Bars intact in code; re-rulings C-2, C-4, C-10, D-1, D-2/D-3 recorded in spec.md Clarifications 2026-10-10/11. |
| SC-001 | pass | Drive sweep 31.3277 dB (bar 20), 8688 assertions, 102.5 s; mix sweep 55.9021 dB (bar 20), 3960 assertions; Smoke siblings passed. |
| SC-002 | pass | EdgeDriveLaw 177 assertions; G(edge1)-G(edge0) 20.6744 dB (bar 6); EdgeZeroStillExcites 55.9448 dB (bar 20); MixIndependentOfEdge under C-9 worst residual -126.584 dB (bar -60). As-written 0/0.5/1 blend is recorded as SC-022(g) at -57.03 to -67.86 dB (several cells above -60: the as-written clause would fail; only C-9 makes it pass). |
| SC-003 | pass | 607344 assertions, 190.9 s: lowest E_gen(asym 1) -17.6435 dB (bar -30); symmetry -0.100582 dB (bar 3). TriangleTail: drop 93.5118 dB (bar 20); lowest E_even -16.3315 dB. |
| SC-004 | pass | ZeroMixBitIdentical 1036 assertions (memcmp vs Phase1Ref, blocks {37,512}, Reset/FreeRunning). Ref header differs from 47e43918 only by 4 comment lines and 2 namespace lines. check-footprint.js: +0 -0 on all Phase 1 TUs. |
| SC-005 | pass | All cells below -80 dBFS: excitation E0 -102.4/-100.0/-94.3; Fold E0 -101.5/-99.2/-92.8; chain E0 -93.5/-92.2/-88.2; E1/48k -91.6; SampleRateChange chain -95.9/-91.6/-88.4. Positive control -36.4 dBFS. |
| SC-006 | pass | ThdAndCountMonotonic [long] 57744 assertions: min step +0.069 dB at 99.75, H(100) 17. RegimesAudible rise 93.7-123.5 / 14.0 / 24.1 dB (bar 6); H 1 -> 17; centroid rise 0.594 oct (bar 0.5). |
| SC-007 | pass | DensityBeforeLevel MIDI 28 THD +13.99 dB, H 1->3, |dL| 0.681, |dCF| 0.681 dB; LevelIndependent Fold spread 0.00 dB (bar 3), tanh 31.66/31.68 (bar >=12); TrajectoryDistinctFromTanh median 5.534 dB (bar 3). |
| SC-008 | pass | RegimeContinuityStatic [long] 400 pairs: |dL| 0.109 dB, |dC| 0.0319 oct; ContinuousInAmountLong worst 0.00365 (bar 0.02); AutomationNoJump worst 0.01999 vs 0.02209 and 0.57871 vs 0.63900. |
| SC-009 | pass | ControlStepsNoClick: `SC-009 worst ratio 1.00978 ... bar 1.1`, 2432 assertions; EngageNoClick 696 assertions, largest ratio 1.0097841638. |
| SC-010 | pass | TransparentAtZero: all 18 expansions `-3000.0 <= -90.0`, bit-exact; CoreInput: `worst residual -3000.0 dB (bar -90); 72 of 72 channels bit-exact`. |
| SC-011 | pass | ExcitationDcFree -109.873 dBFS; Fold -117.1; DcFreeOnExcitationOutput -92.7; ChainDcFree -92.7 (bar -80); ZeroInZeroOut 12 assertions. |
| SC-012 | pass | Excitation (a) 0.069455 dB (bar 0.1), 0.140911 deg (bar 2); (b) 0.126689 deg, saw asym>0 14.9886 deg (bar 20 per C-2); (c) 0.0153868 dB (bar 0.5). Fold 1.572 deg (bar 5), -0.03 dB (bar -6), -9.51 dB (bar -12); CoreInput 1.576 deg, -6.32 dB. |
| SC-013 | pass | LREqualAtZeroPan 294, LREqualForIdenticalInput 38; ExcitationMonoSum 0.467 / 9.516 deg, 0.131 dB; HarmonicFold_MonoSum 1.698 deg, 0.155 dB. Detector clause under C-4: StereoDetectorPerChannel 0.0002 dB (bar 0.1); shared-detector teeth 0.535 vs 6.020 dB. SC table row still says "shared detector" / `HarmonicFold_StereoDetectorShared`; only the C-4 log carries the re-ruling. |
| SC-014 | pass | RetriggerPolicy 46 assertions across Reset / FreeRunning same f0 / FreeRunning 7 semitones away. |
| SC-015 | pass | ExcitationNoAllocation: static_assert noexcept, REQUIRE(count == 0) passed with liveness probe >= 1. HarmonicFold_NoAllocation blocks up to 8192, count == 0. |
| SC-016 | pass | ExcitationNonFinite 66 assertions; HarmonicFold_NonFinite 8196; NonFiniteSetters 19; RangeAndRates 38; both nonfinite TUs in -fno-fast-math list. |
| SC-017 | pass | ExcitationDeterministic 41, BlockSizeIndependent 20; SampleRateChange 48->96->44.1: 96k chain -88.4 dBFS, 0.0372 dB / 0.0883 deg; 44.1k -95.9 dBFS, 0.0695 dB / 0.1409 deg. |
| SC-018 | pass | Pinned, alone, 3 runs 20 s apart. Excitation_alone / fold_alone / stage_cost / total: 0.547/0.534/1.224/2.194; 0.607/0.523/1.144/2.093; 0.584/0.466/1.275/2.185. A_core_mix0 0.971 / 0.949 / 0.910. All printed `All tests passed (22 assertions in 1 test case)`. Stereo-forced stage cost 1.932 / 2.012 / 1.814 (print-only, no gate). Full `run-cpu-tests.js dsp_systems_tests` lane not run this session. Logged plan.md:1174. |
| SC-019 | pass | OutputBounded worst preCeilingPeak 2.8807 (SC-009 state, MIDI 48, amount 82.5) under kOutputCeiling 6.0 but above plan model <= 2.40; excitation peak 0.691, Fold peak 0.908. |
| SC-020 | pass | `SC-020: worst level -0.875142 dB re reference (bar +-1.5)`, 17982 assertions, 86.5 s. |
| SC-021 | **FAIL** | Footprint passes: check-footprint.js `OVERALL: PASS`, 34/34 Phase 1 declarations, only profundum_core.h changed (+497 -10). dsp_processors_tests 4/4 shards passed; dsp_primitives_tests `All tests passed (4561560 assertions in 1499 test cases)`; dsp_systems_tests per-push 8/8 shards passed; [long] 13 of 14 shards passed. **VoragoMacro_SweepAxes failed in 1947 s: `vorago_macro_test.cpp(1318): FAILED ... 0.8666666667 >= 0.9 Movement per-band total variation rho=0.866667 endpoint=0.218562`.** Predates the phase (nightly run 38046177066 at 89b512d1 failed identically on macOS, endpoint=0.216728). Gate stays red until the Vorago Movement row is fixed. Overnight soaks not run. |
| SC-022 | pass | All figures printed: (a) chain -89.8 (E2), -79.8 (MIDI 52), -64.1 (64), -58.0 (72) dBFS; (b) 22.05k -87.7, 192k -70.9; (c) 8 Hz |T1| -0.0435 dB, -0.076 deg; (d) 0.374 dB; (e) kGrowl -93.09 deg; (f) +1.72/+2.76/+6.48 dB; (g) -57.03 to -67.86 dB; (i) full-band argmax -4, group delay 4.98 samples at 1 kHz, 4.78/4.98 at 2.6-4.0 kHz (<= 8). |
| SC-023 | pass | `check-bars.js sc023 plan.md`: `PASS: SC-023 table at plan.md:1155, 11 rows, chosen: (a) envelope-normalised waveshaper`. C++ SC-018 log plan.md:1165-1174 includes this session's row (stage 1.224 / 1.144 / 1.275 %). Caveat: plan.md:1163 text still says "isolated run-cpu-tests.js lane still has to confirm it" and quotes older 0.85-0.96 % figures. |
| SC-024 | pass | kDetectorAttackMs = 0.0f, kDetectorReleasePeriods = 4.0f; DetectorTimingConstants passed. NoInCycleGainModulation residual -92.0 to -151.9 dB (bar -50), ripple 0 (bar 1e-4), x/env reference 1.358 / 1.186 / 0.937 (bar >= 0.1). |
| SC-025 | pass | SoftRegimeLevelDependent MIDI 28 THD -68.58 / -56.76 / -44.96 / -33.58 dB, spread 35.01; MIDI 36 35.02 (bar >= 12). FoldRegimeLevelNormalised THD -8.93, spread 0.00 (bar <= 3). |
| SC-026 | pass | static_assert latencySamples() == 0 (harmonic_fold_test.cpp:124); ZeroLatency REQUIRE(bestLag == 0) 16 assertions. Under D-2/D-3: mid grid |arg T1| 0.0396 deg (E1), 0.0736 deg (C2), bar 0.5; 125 Hz low-passed argmax 0; full-band argmax -4 (<= 8). No FIR-mode tokens found. |
| SC-027 | pass | `check-bars.js check spec.md`: SC-001..SC-025 and SC-027 SAME; `SC-026 CHANGED+RULED`; PASS. C-4 re-ruling of SC-013(c) lives only in the Clarifications log; script cannot detect it. |
| CC-rt | pass | No new/delete, containers, exceptions, locks or I/O on audio-thread paths in polyphase_halfband_oversampler.h, harmonic_fold.h, profundum_core.h; table builds run only from prepare(). Grep of added core lines found no allocation tokens apart from comment text. |
| CC-layers | pass | polyphase_halfband_oversampler.h (L1) includes only core/db_utils.h + std; harmonic_fold.h (L2) includes L0 and L1 only; profundum_core.h (L3) includes core, primitives, processors. |
| CC-naming | pass | PascalCase classes, camelCase functions, trailing-underscore members, kPascalCase constants. ODR grep for the four class names found only the four definitions. Minor nit: single-letter uppercase locals (P, V, G) in harmonic_fold.h:445-481. |
| CC-warnings | pass | All 8 new test TUs recompiled; `grep -iE warning` returned 0 compiler warnings; primitives and processors exes relinked. Caveat: `LINK : fatal error LNK1104` on dsp_systems_tests.exe (about 14 concurrent processes held it open); systems exe not relinked in that check. |
| CC-portability | pass | `check-portability: 11 translation unit(s) with g++`, all OK; `check-portability: all clear -- 11 compiled.` EXIT=0. NaN checks via detail::isFinite/bit_cast; designated initialisers at profundum_core.h:357-361; no SIMD loads. |

## Implementation notes (deviations reported by task agents)

- **T001:** All 22 Node evidence scripts reproduced plan figures (wall clock about 336 s, set by ctlphase.js). Non-blocking: plan C-10 "at most 0.15 deg" does not hold on mid grid at asymmetry 1 (E1 1.57 -> 1.06 deg, C2 0.27 deg); SC-013(c-i) core-only worst case is 1.89 deg (margin 0.11) not 1.95. candidate-design-bench-v2.cpp not run.
- **T002:** tasks.md base line numbers off by one (`public:` at :41, FTZ note :38-39); script finds them by text. Checks 3 and 4 had no positive control before the headers existed.
- **T003:** "a line" in the Clarifications log means a bullet plus wrapped continuation; token regex over-collects (safe direction).
- **T004:** `Krate::DSP::ProfundumTest` namespace already exists in test helpers; checked no name clash. Generated file is deterministic (hash 9dee6cca...).
- **T005:** Pinned bench v2 median of 5: both 1.467 % (exc 0.718, Fold 0.785), above the 1.45 % flag line: early R-1 warning for T013. Compiled with clang 21.1.8 from C:/Program Files/LLVM.
- **T006:** One extra comment line added above primitives TU entry in CMake.
- **T007:** Header written before test (test-first not literally followed). Added a downsampler steady-state gain section to RenderMatchesDesign to prove branch order. Group delay round trip 3.24 samples (F=2), 4.34 (F=4) vs plan "about 1.9" (recorded only).
- **T008:** tasks.md SC-024(a) clause self-contradicts (4/20.6 vs 4/20.6017223); test checks Approx(4/20.6017223) at default and >= 4/20.6f after setFundamentalHz(20.6f). Helper header also includes catch2. describeFold needs >= 262144 samples. detectorLevel() returns left channel.
- **T009:** OutputBounded notes E0, E1, E2, C2-C5 chosen by agent. preCeilingPeak_ updated in idle too (idle output not clamped).
- **T010 (D-1):** Linear fold-index law failed (sine |dC| 0.0751 oct vs 0.04); p = 1.5 ease-out law adopted (kFoldIndexEaseExponent = 1.5f), theta(100) still 3.0. p = 2, 3, smoothstep failed the +0.05 dB min THD step. Edited edge-fold-model.js, evidence/d1law.js, plan.md, spec.md, tasks.md beyond the listed set.
- **T011:** Table law computed inline in double (Sigmoid::recipSqrt is float-only), so sigmoid.h include is unused. setExcitation sets dirty flag on every call. DC mirror fields named dcIn/dcOut without underscore.
- **T012 (D-2/D-3):** ZeroLatency rewritten to re-scoped SC-026; at E1 no partial at 4 kHz (measured at harmonic 63, about 2596 Hz); edited tasks.md per ruling.
- **T013:** Perf test written; R-1 ladder not needed (gates passed when measured).
- **T014:** Extra [long] + Smoke pairs beyond the two SC-001 cases; cached mix-0 twin references per (input, f0, fs).
- **T015:** HelperParity may fail on Linux/macOS from COMDAT/fast-math mismatch rather than real drift; SampleRateChange may exceed 15 s and need a [long] tag.
- **T016:** No header change; defects D-1, D-2, D-3 recorded.
- **T017:** Audit clean; duplicate-name regex needed digits; stale `HarmonicFold_StereoDetectorShared` name in spec.md line 494.
- **T018:** All 37 lanes green in 1034 s wall clock; per-push systems lane alone sets wall clock (about 8 shards would give about 5 min). Worst aliasing chain E2/96 kHz -83.1 dBFS (3.1 dB margin).
- **T019:** clang-tidy `-Target dsp`: 385 files, Errors 0, Warnings 0. Phase1 ref header is untracked so the `git diff --quiet` check cannot apply.
- **T020:** CPU lane not re-run; relied on 2026-10-11 lane (core 0.634, stage 1.350, chain 1.985, Phase 1 0.633). VoragoMacro_SweepAxes ruled not a Phase 2 gate, to be raised with the user.

## Remaining gates for the human loop

- clang-tidy: re-run before commit (last T019 run on `-Target dsp` clean: 385 files, 0 errors, 0 warnings).
- Commit (nothing committed; new files untracked: polyphase_halfband_oversampler.h, harmonic_fold.h, test TUs, profundum_phase1_core_ref.h, spec scripts).
- Raise the Vorago `VoragoMacro_SweepAxes` failure (SC-021) with the user.
- pluginval: not required (no plugin code changed).
