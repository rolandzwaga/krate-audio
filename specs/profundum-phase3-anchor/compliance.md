# Compliance Report: Profundum Phase 3 (profundum-phase3-anchor)

**Overall status: INCOMPLETE** (comply, before the main-loop close) → **main-loop close 2026-10-12: 87 pass, 1 partial (SC-003: every bar green on every ISA measurable here; macOS / ARM NEON wait for the nightly `[long]` lane after push), 0 fail.** Gaps 2 (T025 CPU lane: run alone and pinned, 0.1406 % worst, see SC-018) and the full clang-tidy sweep (390 files, 0 errors, 0 warnings after a stale compile-database fix — tasks.md Run log "Close gates") are closed; gaps 3 and 4 stand as recorded (non-Profundum `[long]` sweeps and a macOS compile are not Phase 3 gates: shared-header diff zero).

## Honest gap list (first)

1. **SC-003 is PARTIAL.** Every bar passes on every target measured (x86 Highway targets AVX2/SSE2 under MSVC; AVX2/SSE4/SSSE3/SSE2 under WSL GCC 13.3). ARM NEON/SVE, the macOS clang toolchain and AVX3 were not measured here, and the nightly CI [long] lane has not run this unpushed code. No bar is failing.
2. **T025 (isolated CPU lane) was deferred to the main loop.** The SC-018 / FR-064 evidence is the earlier pinned, isolated T017 run plus a later pinned run (see rows). `node tools/run-cpu-tests.js dsp_systems_tests` has not been re-run as the closing lane.
3. **Not run:** the non-Profundum [long] cases of dsp_systems_tests (multi-hour Vorago sweeps). A full unsharded run was stopped after 1 h 29 min of CPU time to shard it.
4. **ARM/macOS toolchain:** only check-portability.js (g++ via WSL) and lint-apple-globals.js ran, not a macOS compile.
5. T010 (FR-032 fallback) was not triggered because no SC-003 arm was red; shared-header diff is zero.

## Compliance table

| ID | Verdict | Evidence |
|---|---|---|
| FR-001 | pass | anchor_oscillator.h:1-8 Layer 3 banner; :51 `namespace Krate::DSP`; :55 `class AnchorOscillator`; header-only; test.cpp:105 static_asserts copy-constructible and nothrow-move. check-footprint 4(a) PASS (no duplicate class). |
| FR-002 | pass | :99-106 `struct Controls {waveform, mode, level=kDefaultLevel, punchBoostDb=6, punchTimeMs=40, ghostTimeMs=15}`; designated initialisers at :228-238 and test.cpp:159-164. |
| FR-003 | pass | :57 `enum class Waveform : std::uint8_t {Sine, Triangle, RoundedSine, AsymmetricSine, OctaveDownSine}`; :58 `Mode {Lock, Follow, Punch, Ghost}`; test.cpp:88-96 pins values 0..4 and 0..3. |
| FR-004 | pass | :123-124 prepare clamps to [22050, 192000], no allocation, calls reset() :162; setters guard `if (!prepared_) return;`; :289-293 zeros before prepare. LifecycleNoOps (test.cpp:151-243) incl. prepare(10000) bit-equal to prepare(22050) passed. |
| FR-005 | pass | :276 noteOn(f0, rearm=true); :248 setFrequency; :255 setFollowRatio; :285-286 processBlock(...); :411-415 sanitizeF0; AnchorOscillator_MirrorTracksCore (test.cpp:247) bit-for-bit passed. |
| FR-006 | pass | :59 RetriggerPhase {Reset, FreeRunning}; test.cpp:99-101 equals ProfundumCore::RetriggerPhase; :483-516 Reset and FreeRunning branches. RetriggerPolicies passed. |
| FR-007 | pass | :287-288 null/zero-length no-op; :341-343 aliasing handled; LifecycleNoOps (iii) test.cpp:198-223 bit-identical state, sentinels untouched. |
| FR-008 | pass | :357 `double modeGain()`, :358 phase(), :362 stateFinite(), :383 latencySamples()==0; float->double per ruling C-4 (spec.md:821), test.cpp:129 static_assert. |
| FR-010 | pass | :747-763 shapeUnit per waveform. 'Goertzel phase anchor - core h1 = +0.000128 deg (bar 1)'. |
| FR-011 | pass | scales at :391, :393, :148, gain :392. 'SC-002 sine ... a1 0.355234: RMS -12.0000 dB expected -12.0000 dB', 'waveform spread 0.0000 dB (bar 0.1)'. |
| FR-012 | pass | :687-688 taperWeight 0 at/above capHz_; :646-647 delta cap. 'SC-010 triangle midi 16 fs 44100: aliased -148.48 dBFS (+12 -> -136.48, bar -80)'; FOLLOW r 4 worst -174.58 dB re w1 (bar -90); naive triangle control -62.56 dBFS fails as expected. |
| FR-013 | pass | no DC blocker in header. 'SC-011 asymmetric midi 16: DC -115.18 dBFS', 'octave-down midi 16: DC -111.86 dBFS' (bar -80). |
| FR-014 | pass | :76 kWaveformCrossfadeMs=5; :781-785 equal-gain crossfade; :458-461 queue. 'SC-006(b): 124 renders, 0 clicks in total (bar 0)'. |
| FR-015 | pass | :757-759 sin(pi*phi2_), :824-825; 'SC-003(d) static octave-down midi 16 fs 48000: dTheta(60 s) +0.000157 deg'. |
| FR-016 | pass | :498-503 one-period onset ramp (:81); 'SC-017 onset waveform 0 midi 16: ratio 0.9999 (bar 1.1)'. OnsetClickFree passed. |
| FR-017 | pass | :91-92 a3=a2=0.25f; 'SC-001 rounded midi 28: h3 -12.042 dB', 'asymmetric: h2 -12.041 dB', 'rounded h3 - triangle h3 = 7.044 dB (bar 7.0)'. |
| FR-020 | pass | :234 level clamp; :75 kLevelSmoothMs=5; :159, :338, :493. LevelAndA1Smoothing passed; SC-006(b) 0 clicks. |
| FR-021 | pass | smoother.h:199 snap. 'SC-002 zero arm sine ... a1 -> 0: first exact-zero sample 361 (8.186 ms)'; 'SC-012(b) level sweep span 40.0000011 dB'. |
| FR-022 | pass | public API :120-383 exposes only Controls::level as gain; setCoreH1Amplitude (:262) is the FR-023 calibration input. |
| FR-023 | pass | :262-267 setCoreH1Amplitude clamp [0, kMaxCoreH1Amplitude]; :71 equals HarmonicFold::kCalibrationPeak (test.cpp:73); :1164-1165 default; :340 total gain. |
| FR-024 | pass | :70 kAnchorToH1=1.0f, :72 kDefaultLevel=0.5f. 'SC-024 kSineAnchor midi 28: a1 0.355234; core h1 vs ref -0.0000 dB, anchor RMS/(a1/sqrt2) +0.0000 dB, w1 rise level 1 6.0206 dB'. |
| FR-025 | pass | no core-audio input (:285); ProfundumAnchor_NoCompensation (chain_test.cpp:623) passed; roadmap Phase 9 gains the plain-add sentence. |
| FR-030 | pass | mirror :583-632, frequencyUpdate :644. PhaseLock* (5 cases, 26.6 s wall) all passed: 'SC-003(a) static sine midi 40 fs 44100: dTheta(60 s) +0.125033 deg'; E0 48 kHz drift +0.000078 deg; 'SC-003(c) Reset same f0 midi 28 drift -0.000463 deg'. |
| FR-031 | pass | :316 U=16 grid (kPitchUpdateInterval :63); :411 sanitising; :524-552 bankEps; no ProfundumCore include (check-footprint 4(a) PASS). |
| FR-032 | pass | Fallback not needed: SC-003 passes; `git diff --stat 9b1a4cf5 -- dsp/include` empty; check-footprint OVERALL: PASS. |
| FR-033 | pass | :383 latency 0 (test.cpp:106 static_assert); :341 same-index write; 'SC-026(b) +0.000128 deg'. |
| FR-034 | pass | :336-340; check-footprint 4(b): 'public const float* parameters only in processBlock (2)'; no audio input. |
| FR-035 | pass | phi2_=0 only at :484 and :177. 'SC-026(a) FreeRunning +5 rearm: worst |dphi - f_grid/fs| 4.661e-11 cycles (bar 1e-9)'. |
| FR-036 | pass | :645 r_eff only in FOLLOW. 'SC-026(c): ... |diff| 4.475e-07 (bar 1e-6)'. |
| FR-040 | pass | :1013-1016 snapArm Lock/Follow holds 1.0; :1033-1036 bridge to rest. |
| FR-041 | pass | :874-894 cosine and Hermite segments; :976-1008 C-3 bridge; 'SC-006(a) PUNCH snap arm: worst |g - program| 6.75e-14 ... inRange 1; pass 1'; C1CheckerPositiveControl passed. |
| FR-042 | pass | :1017-1021, :1037-1045. 'SC-004 midi 28 boost 3.0 dB time 5.0 ms: peak +0.00000 dB ... return at 288 (expected 288)'; 'SC-027(b) ... return at +1968 (expected +1968)'. |
| FR-043 | pass | :1023-1024, :1047-1055. 'SC-005 P1 midi 28 ghost 1 ms: worst g(t <= 0.1T) -35.37 dB, monotone 1'; 'SC-027(c) GHOST bridged arm: g == 0 at +96 (bar 97)'. |
| FR-044 | pass | :445-446, :1062-1070 (kModeBridgeMs=5 <= 10). 'SC-006(c): worst ratio 0.9091 over 124 transitions (bar 1)'. |
| FR-045 | pass | :255-259 clamp [0.25, 4]; :644-663 16-sample glide. 'SC-012(f) E1 FOLLOW r 0.5: fundamental 20.601724 Hz, r*f0 20.601723 Hz, +0.00007 cents'. FollowEqualsLockAtUnity, FollowLatency passed. |
| FR-046 | pass | Withdrawn id. No detector include, no analysis input, latency 0, no allocation in prepare. |
| FR-047 | pass | Roadmap Phase 5 gains the r-law sentence; ProfundumAnchor_FollowReferenceOnFoldedVoice (chain_test.cpp:827) passed (3.63 s). |
| FR-048 | pass | :276, :481, :504-505, :513-514. 'SC-017 rearm=false Reset (Punch): worst phase-step error 1.761e-16 cycles'; RearmEvent (test.cpp:2835) passed. |
| FR-049 | pass | Single trajectory input (:285-286). 'SC-025(3) midi 28: LOCK worst 0.0001 cents during the Drop (bar 1); FOLLOW worst 0.0003 cents re 1.5*f0 (bar 5)'. |
| FR-050 | pass | :340-343 one float to both channels. 'SC-007 matrix: 200 renders ..., 0 L != R, 0 mono-sum failures'; 'SC-022(3) ... L == R bitwise feed 0 yes, feed 1 yes'. |
| FR-051 | pass | per-instance members only. 'SC-013 source scan: 55406 bytes scanned, 0 mutable static(s)'. |
| FR-052 | pass | no voice-audio input; ProfundumAnchor_SubPreservedThroughMaxFold (SC-009, chain_test.cpp:866) passed under the 2026-10-12 re-ruling. |
| FR-053 | pass | noexcept static_asserts test.cpp:110-126; grep for new/malloc/vector/throw/mutex/printf empty (comments only); 'SC-014 matrix: 0 allocation(s) over 5 waveforms x 4 modes with transitions active'. |
| FR-054 | pass | :406-408 sanitizeField; :229-233 enum guard; :411/:258/:265 keep previous; detail::isFinite is bit-pattern (db_utils.h:118-128); nonfinite TU in -fno-fast-math block. NonFiniteInputs passed. |
| FR-055 | pass | :95-96 kOutputCeiling 1.75f; 'SC-015 bound arm: fs 22050 Hz C2 max|y| 1.709954 analytic worst 1.710100 ceiling 1.75'. |
| FR-056 | pass | no random source; 'SC-016 determinism: 0 of 20 cells differ (bar 0)'; 'partition 64 == partition 512'. |
| FR-057 | pass | :53-54 precondition note; AnchorOscillator_FtzPrecondition (test.cpp:139-145) passed. |
| FR-058 | pass | no SIMD; designated inits. check-portability: '7 translation unit(s) with g++ ... all clear -- 7 compiled'; lint-apple-globals: 'OK — 973 files, no macOS-only name collisions'. |
| FR-060 | pass | AudibilityGate (spectral_test.cpp:814) passed: 'SC-012(b) C2 level sweep span 40.0000011 dB', 'SC-012(c) C2 punch boost span 8.846 dB (bar 6)', 'SC-012(f) ... +0.00006 cents'. |
| FR-061 | pass | NoAliasing passed at E0-E2, 44.1/48/96 kHz, +12 dB; 'aliased -148.48 dBFS (+12 -> -136.48, bar -80)'; positive control -62.56 dBFS detected. |
| FR-062 | pass | 'SC-007 matrix: 200 renders (5 waveforms x 4 modes x 2 policies x 5 blocks), 0 L != R, 0 mono-sum failures, 0 silent'. |
| FR-063 | pass | check-footprint: `git diff --stat 9b1a4cf5 -- dsp/include (empty)`, 18 Phase 1/2 test files '+0 -0', OVERALL: PASS; only dsp/CMakeLists.txt (+2) and dsp/tests/CMakeLists.txt changed. |
| FR-064 | pass | perf_test.cpp:45 kBudgetFraction=0.003, :198 REQUIRE. Isolated pinned T017 log F:/tmp/p3_t017_cpu.log: 'ANCHOR_PERF worst cell: sine PUNCH 494Hz = 0.1377 % (budget 0.3 %)'. Header unchanged since. Non-isolated incidental run 0.00145197 <= 0.003. |
| FR-065 | pass | check-bars.js check: all SAME except SC-003, SC-009, SC-022 CHANGED+RULED, no CHANGED-UNRULED, PASS. Re-rulings at spec.md:833 and tasks.md T018/T023. |
| FR-066 | pass | check-handoff.js pass: 14/14, 'RESULT: PASS'; roadmap diff covers Phases 4, 5, 6, 8, 9, 11. |
| FR-067 | pass | chain_test.cpp:944-976 renderFoldFeed for g in {0,1}; SC-022(2) '+4.4144 dB (floor -1, ceiling +6.27)', SC-022(3) L == R bitwise; roadmap amendments written. |
| SC-001 | pass | WaveformThd 'All tests passed (1197 assertions)': worst sine THD -127.548 dB (bar -90); triangle THD -18.334..-18.336 dB, odd-ratio error 0.0021 dB, even -126.776 dB; rounded/asym h3/h2 -12.040..-12.042 dB, other harmonics -129.807 / -145.068 dB; 'rounded h3 - triangle h3 = 7.044 dB (bar 7.0)'; octave-down THD -111.68 dB, f0 power -111.747 dB. ThdToolFloor (57 assertions): float sine THD -127.563 dB (bar -100). |
| SC-002 | pass | RmsCalibration 'All tests passed (1037 assertions)': 60 rows, 'RMS -12.0000 dB expected -12.0000 dB', 'a1 0.088809: RMS -24.0412 dB expected -24.0412 dB', max dev 0.0000 dB (bar 0.1), worst waveform spread 0.0001 dB, zero arm 'first exact-zero sample 785 ... bound 785.83 samples'. |
| SC-003 | **partial** | All bars pass on every measured target; unmeasured: ARM NEON/SVE, macOS clang, AVX3, nightly CI [long] lane. WINDOWS/MSVC (6 shards, all 'All tests passed', e.g. sysP_4 '3786492 assertions in 30 test cases'): 'SC-003 ISA AVX2: core render hash 685823b4cb9e67ea', 'SC-003 ISA SSE2: core render hash 861536d7eb003071'; worst 'SC-003(a) static sine midi 40 fs 44100 block 37: dTheta(1 s) +0.002435 deg, dTheta(60 s) +0.125214 deg, drift +0.122779 deg (bars 1.0 / 0.5)'; (b) worst drift 0.113714 deg; (c) worst drift 0.041314 deg, worst +1 s offset 0.188111 deg; (e)(i) 0.030343 deg; (e)(ii) -0.593484 deg recorded, not gated per the 2026-10-12 re-ruling (spec.md:853-863). LINUX/GCC 13.3 (WSL, dsp_systems_tests rebuilt 'exit=0 wall=401s'): AVX2 hash 254ee6e3cb37edbb, SSE4/SSSE3/SSE2 (accbe22e48e6f557); EveryIsa60s 'All tests passed (7917 assertions in 1 test case)' 154 s; worst drift 0.004673 deg; (b) 0.002412 deg; (e)(i) 0.068792 deg; (e)(ii) -0.373469 deg recorded. EveryIsaSmoke 'All tests passed (2125 assertions in 1 test case)'. ISA arm: chain_test.cpp:452-524 via hwy::SetSupportedTargetsForTest. |
| SC-004 | pass | PunchBoostAndDuration (86131 assertions): 18 rows, 'peak +0.00000 dB', 'return at 288 (expected 288)', '1968 (expected 1968)', '12048 (expected 12048)', 'after 0.00e+00 dB', trace vs g worst 1.00e-06 dB (bar 0.05). |
| SC-005 | pass | GhostEmergence (61017 assertions): worst g(t<=0.1T) -32.23 dB (bar -30), 'worst |g| after T 0.00e+00 dB', 'monotone 1', 'P2 dip reaches 0 at 96 samples after the arm (bar 97)'; (d) worst 9.86e-08 dB (bar 0.25); P3 memcmp bit-identical. |
| SC-006 | pass | EnvelopesAreC1 (280320 assertions): 98 trajectories 'pass 1', 'worstRatio 1.0000' (bar 1.05); control 'linear ramp ... worstBoundaryJump 1.241667e-02 (bound 2.553075e-04) ... pass 0'; 'SC-006(b): 124 renders, 0 clicks in total (bar 0)'; control 'spliced level step 0 -> 1: 1 click(s)'; 'SC-006(c): worst ratio 0.9091 over 124 transitions (bar 1)'. |
| SC-007 | pass | LrBitIdentical (204 assertions): 'SC-007 matrix: 200 renders ..., 0 L != R, 0 mono-sum failures, 0 silent (bars 0 / 0 / 0 / 0)'. |
| SC-008 | pass | FollowReferenceOnFoldedVoice worst '+0.0013 cent (bar 1)'; FollowRatioAccuracy (57739) 'static r: worst 0.00010 cents (bar 1)', 'glide: worst 0.00010 cents (bar 5)'; FollowLatency 'worst latency 15 samples (bar 32)', 'worst latency 30 samples ... (bar 32)'; '16 FOLLOW r = 1 renders bit-identical to LOCK'. |
| SC-009 | pass | SubPreservedThroughMaxFold (576 assertions): worst coherence residual -0.0392 dB (bar 0.1); worst |E_sub(S)-E_sub(R)| 2.3112 dB (bar 4.1); recorded core-only w1 changes -3.5758/-4.6555/-5.3130 dB (mid grid), -5.0735/+2.5355/+2.5274 dB (kSawAnchor). Bars per 2026-10-12 re-ruling (check-bars 'CHANGED+RULED'). |
| SC-010 | pass | NoAliasing (946 assertions): worst 'aliased -148.44 dBFS' -> -136.44 (bar -80); FOLLOW r=4 '-149.34 dBFS', above-cap worst -169.86 dB re w1 (bar -90); control 'naive triangle C4 44.1k aliased -74.56 dBFS (+12 -> -62.56), bar -80' fails as required. +12 dB added analytically (plan.md:868-872). |
| SC-011 | pass | NoDc (90 assertions): worst 'SC-011 asymmetric midi 28: DC -111.56 dBFS over 53 periods (bar -80)'; 15 rows between -111.56 and -123.18 dBFS. |
| SC-012 | pass | AudibilityGate (17105 assertions): 9 pairs separated; 'rounded vs triangle at h3: 7.044 dB'; asym h2 77.958 dB; octave-down '+0.00006 cents', f0 power -139.84 dB; (b) 'span 40.0000011 dB' / '40.0000005 dB'; (c) 8.846 / 8.892 dB; (d) 3.599 / 3.654 dB; (e) 40.734 / 38.931 dB; (f) +0.00006/+0.00007 cents; (g) GHOST/LOCK -6.322/-4.110/-4.127 dB, PUNCH/LOCK 3.901/3.870/3.764, PUNCH/GHOST 5.166/5.024/4.873. |
| SC-013 | pass | InstancesIndependent (48 assertions): interleaved == alone at blocks 1/37/512; 'source scan: 55406 bytes scanned, 0 mutable static(s) (bar 0)'; StaticScannerControls (16) passes. |
| SC-014 | pass | RealTimeSafe (11 assertions): 'SC-014 matrix: 0 allocation(s) ...'; 'FR-007(i): 0 of 20 ... cells differ'; 'FR-007(ii): 0 trace mismatch(es), 0 sentinel write(s)'; LifecycleNoOps (36). |
| SC-015 | pass | NonFiniteInputs (16063 assertions), TU in -fno-fast-math block (dsp/tests/CMakeLists.txt:1135-1136). 'fs 48000 Hz C2 max|y| 1.710079 analytic worst 1.710100 ceiling 1.75'. |
| SC-016 | pass | Deterministic '0 of 20 cells differ (bar 0)'; BlockSizeIndependent '0 mismatch(es) over 4 modes x 3 partitions (bar 0)'; SampleRateChange (465): THD -161.226/-127.548/-159.339/-161.058/-111.681 dB (bar -90), SC-003(a) 10 s worst drift +0.010882 deg (22.05k). |
| SC-017 | pass | RetriggerPolicies (50800 assertions); 'rearm=false Reset ... worst phase-step error 1.761e-16 cycles'; OnsetClickFree worst ratio 1.0000 (bar 1.1). |
| SC-018 | pass | Alone, pinned (tools/pin-perf-cores.ps1, mask 0xFFFF): 'ANCHOR_PERF worst cell: triangle PUNCH 494Hz = 0.1547 % (budget 0.3 %)', worst E1 cell 0.1307 %, 'All tests passed (42 assertions)'. Earlier T017: 0.1377 %. **T025 (main-loop close, 2026-10-12 13:48, alone, pinned, after all static gates):** 'ANCHOR_PERF worst cell: triangle PUNCH 494Hz = 0.1406 % (budget 0.3 %)', 'All tests passed (72 assertions in 3 test cases)'; Phase 1 / 2 budgets in the same run: core 0.584 / 0.721 / 0.356 %, chain 1.455 % (bar 2.5), stage cost 0.791 % (bar 1.5) — tasks.md Run log T025. |
| SC-019 | pass | check-footprint.js 9b1a4cf5: '0 changed path(s) outside the new header', 18 test files '+0 -0', '11 include(s), all L0-L2 or std', dsp/include diff empty, 'OVERALL: PASS'. |
| SC-020 | pass | check-bars.js: SC-003, SC-009, SC-022 'CHANGED+RULED', all others SAME, 'PASS'. |
| SC-021 | pass | check-handoff.js pass: 14 PASS rows, 'RESULT: PASS'. |
| SC-022 | pass | FoldFeedAudible: THD delta -4.474/-8.405/-4.467/-8.471 dB (bar 3), centroid delta -0.3794/-0.8579/-0.3835/-0.8598 oct (bar 0.25), 'SC-022 VERDICT: PASS'; FoldFeedSubPreserved +3.9509/+4.4141/+3.9560/+4.4144 dB (floor -1, ceiling +6.27); FoldFeedMono 'L == R bitwise feed 0 yes, feed 1 yes'. |
| SC-023 | pass | check-portability on header + 6 files: '5 translation unit(s) with g++ ... all clear -- 5 compiled.' rc=0; lint-apple-globals 'OK — 7 files, no macOS-only name collisions' rc=0. |
| SC-024 | pass | LevelCalibration (416 assertions): anchor RMS/(a1/sqrt2) +0.0000 dB, spread 0.0000 dB (bar 0.1); 'w1 rise level 1 6.0207 dB (expected 6.0206)'; RMS rise worst '+3.5219 dB (bar <= 3.6 dB)'; NoCompensation memcmp 0 mismatches. |
| SC-025 | pass | TrajectoryContract (1305786 assertions): 'fifth up within 1 cent after 9 samples (bar 32)'; glide worst 0.0002 cents (bar 5); Drop LOCK 0.0001 cents (bar 1), dTheta +0.000411 deg, FOLLOW 0.0003 cents (bar 5). |
| SC-026 | pass | PhaseContinuity (480059 assertions): 'glide +-7: worst |dphi - f_grid/fs| 3.283e-10 cycles (bar 1e-9)'; LOCK<->FOLLOW 6.992e-11; PhaseConvention '+0.000191 deg (bar 1)'; PhaseReference '|diff| 4.475e-07 (bar 1e-6)'. |
| SC-027 | pass | RearmEvent (43965 assertions): 'PUNCH bridged arm ... return at +1968 (expected +1968)' / '+1965 (expected +1965)'; 'GHOST bridged arm: g == 0 at +96 (bar 97)', emergence worst -32.23 dB, monotone 1. |
| CC-rt | pass | Read of the 1168-line header; grep for allocation/lock/IO/throw primitives only hit comments (lines 13, 29); processBlock noexcept; fixed-size std::array state; log2/exp2/pow at grid/arm time only. |
| CC-layers | pass | Layer 3 header registered in KRATE_DSP_SYSTEMS_HEADERS; includes :38 core/db_utils.h, :39 core/math_constants.h, :40 primitives/smoother.h, :41 processors/spectral_shape_recipe.h plus std headers; only nonfinite TU in -fno-fast-math block. |
| CC-naming | pass | PascalCase types, camelCase functions, kPascalCase constants, trailing-underscore privates; ODR greps found only anchor_oscillator.h:55; none of the roadmap hazard names used. |
| CC-warnings | pass | MSVC rebuild of all 5 DSP exes exited 0 in 13 s, `grep -ci ': warning'` = 0, link completed. `cl /W4` probe 0 warnings; g++ 13.3 and clang++ 18.1.3 with -Wall -Wextra -Wpedantic -Wshadow -Wconversion 0 warnings; WSL Ninja '[118/118] Linking CXX executable bin/Release/dsp_systems_tests exit=0'. Suites: core 'All tests passed (1588248 assertions in 565 test cases)'; primitives '(4561560 assertions in 1499 test cases)'; processors '(10826781 assertions in 3381 test cases)'; effects '(122746 assertions in 499 test cases)'; systems per-push 12 shards (1577 cases) and [profundum] incl. [long] 6 shards all green. Not run: non-Profundum [long] cases (gap 3). |
| CC-portability | pass | `node tools/check-portability.js` on 6 files, rc=0 in 16 s: '5 translation unit(s) with g++ ... all clear -- 5 compiled.' Header checked via the tests that include it. No isnan/isinf/isfinite hits; designated initialisers used. |

## Implementation notes (deviations reported by task agents)

- T001: no ODR collision; `AnchorMode` exists nested in resonance_drift_network.h:293 but the plan uses nested `AnchorOscillator::Mode`.
- T002: tasks.md lists 17 Untouchable test files but names 18; the script checks 18. Plan S5.4's fallback `const float*` input would trip check 4(b) if ever used.
- T003/T004: check-handoff P5-BCS prints one combined line; sections also end at `#`/`##` headings. check-bars.js copied from Phase 2 minus the sc023 mode.
- T007: reset() keeps heldRatio_ (plan S4.2 self-contradictory); fold barrier on the mirror multiplier; extra phaseIncrement assertion.
- T008: plan S8.2 SHS estimator misses its own 0.1-cent bar (about -0.19 cent on three pitches); a golden-section refinement stage was added, no bar loosened.
- T009: deviation 5 applied (FreeRunning noteOn calls frequencyUpdate at offset 0); SC-003(c) render length about 12.1 s; Drop read as a 200 ms raised-cosine dip.
- T011: plan S4.5 Clenshaw closing term has a sign error (implemented u*(b0+b1)); taper leaves h1 at weight 1; cents-domain weight cache via two 31-entry tables; octave-down phase arms add +90 deg for the cosine-phase convention.
- T012: ClickDetector verdict on a smoothed level step depends on step position (about 24-28 % of positions flag in a Node model); step placed at sample 48000, not tuned.
- T013: no per-sample gSlope_; Hold(1) when g=1, g'=0; segments cut short are passed to anchorCheckC1 as stand-ins.
- T014: glide length is samples to next grid point when started off-grid; retarget instead of cut when a glide is running at a grid sample.
- T015: noteOn reference uses held f0; bound arm uses the full 22.05-192 kHz range.
- T016: waveform switch alternates between the cell's waveform and the next.
- T018 (epoch 2): SC-009 and SC-022(2) gates changed to the 2026-10-12 re-ruled bars (coherence residual <= 0.1 dB and |dDb| <= 4.1; floor -1, ceiling +6.27).
- T023 (epoch 2): SC-003(e)(ii) fold-path drift is recorded, not gated, per the re-ruling.
- T010 skipped (condition not met); T022 audit found 60 TEST_CASE names matching the spec, no duplicates; T024 clang-tidy on the 5 TUs: zero user-code warnings (full dsp sweep not repeated).
- T025 deferred to the main loop.

## Remaining gates for the human loop

- Run `node tools/run-cpu-tests.js dsp_systems_tests` alone, detached, on an idle machine and record the SC-018 figure (T025).
- clang-tidy: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` (full sweep not repeated after the last edits; per-TU runs were clean).
- Optionally run SC-003 on macOS arm64 / nightly CI [long] lane to close the partial.
- Commit (nothing committed). Pluginval not needed: no plugin code changed.
