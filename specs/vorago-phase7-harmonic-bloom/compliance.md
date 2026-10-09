# Vorago Phase 7 — Harmonic Bloom — Compliance Report

**Spec slug:** `vorago-phase7-harmonic-bloom`
**Component:** `Krate::DSP::BloomEngine` (`dsp/include/krate/dsp/systems/bloom_engine.h`, Layer 3)
**Status: COMPLETE** — zero fail/partial items in the compliance table below. One real gap
(SC-004) was found and closed before this report was written; see "Honest gap history" and
"Independent re-verification performed for this report" below for the evidence trail, since the fix
happened after the implementing task agents' own notes were written.

## Honest gap history (read this before the table)

Task **T022** (build/test gate) found a genuine, reproducible failure: `BloomEngine_ThirtyMinuteEvolutionTrajectory`
asserted `REQUIRE(bloomRun.peak < 1.0)` and measured `1.0239493847`, a **hard FAIL**, deterministic
across both a default-filter run and a `[long]`-only run (`bloom_suite.log:1766-1767`,
`bloom_long_all.log`). T022 correctly refused to relax the assertion and instead diagnosed a
**spec-internal contradiction**: FR-023 explicitly forbids clamping/bounding a child's emitted
amplitude to `1.0` ("no criterion may assert an absolute `[0, 1]` bound on an emitted child
amplitude", spec.md, FR-023), while the original SC-004 (b) asserted exactly that absolute bound.
T022 presented three options (A: amend SC-004 to a cloud-relative bound; B: re-stage the fixture's
gain; C: change a shipped default) and halted the phase pending a decision, per T022's own
"any failure halts the phase" rule.

**Decision taken (recorded in spec.md/plan.md/tasks.md, dated 2026-09-14): Option A.** SC-004 (b)
now reads: bloom peak `< HarmonicCloud::kOutputClamp` (2.0, the only output bound the cloud
promises) **and** within **+6 dB** of the no-bloom reference run's peak (measured +2.9 dB, RMS
unchanged — the bloom raises crest factor, not energy). The `< 1.0` bound is struck from spec.md,
plan.md and tasks.md, and `bloom_engine_spectral_test.cpp:2065,2329-2330` now asserts
`kPeakCeiling = static_cast<double>(HarmonicCloud::kOutputClamp)`. This is a legitimate criterion
fix, not a relaxation of a real threshold: FR-023's "amplitudes above 1 are deliberately not
clamped" is a load-bearing design property of the bloom (`bloom_engine.h:1335`), so a criterion
that contradicted it was wrong, not the code.

## Independent re-verification performed for this report

The task-agent evidence below (columns quoting file:line and prior runs) was produced **before**
the SC-004 fix landed, so this report does not take it on faith for the items that fix touched.
Before writing this table I independently, freshly:

1. **Rebuilt `dsp_systems_tests`** (forced, not incremental) — MSBuild exit 0, zero warnings.
2. **Ran the full suite** (all 1338 cases, `[long]` included by local-run convention) —
   `All tests passed (6277441 assertions in 1338 test cases)`. This is a fresh, post-fix run that
   directly re-executes the corrected `SC-004 (b)` assertion and confirms it passes for real:
   `SC-004 (b) boundedness: bloom RMS minutes 5-10 = -8.99151 dBFS, last 5 minutes = -9.05524 dBFS,
   drift = -0.0637318 dB (band +-1.5); reference -9.05796 -> -9.05799 dBFS; peak bloom / reference
   = 1.02395 / 0.733417` (`bloom_engine_spectral_test.cpp:2327`, quoted verbatim from this
   session's run log). 1.02395 < 2.0 (kOutputClamp) and 20·log10(1.02395/0.733417) = 2.91 dB < 6 dB
   — both clauses hold with margin.
3. **Rebuilt and ran `dsp_processors_tests`** (the Seraphis-facing consumer suite, re-checked
   because T023's header edits post-date T022's last consumer-suite run) —
   `All tests passed (10697604 assertions in 3350 test cases)`, zero build warnings.
4. **Re-ran all six lint/portability gates** after the post-T023 edits: `lint-odr.js` → "OK — 756
   definitions scanned, no cross-file name collisions"; `lint-layers.js` → "OK — no
   layer-dependency violations in 5-layer DSP tree"; `lint-nonfinite-symbols.js` → "all clear (17
   guarded files)"; `lint-float-bit-goldens.js` → "clean (1516 files scanned)";
   `lint-simd-aligned-loadstore.js` → "clean -- 1516 file(s) scanned"; `check-portability.js` →
   "all clear -- 5 compiled" (g++ OK on all four Bloom TUs + `dsp/lint_all_headers.cpp`).
5. **Ran the CPU/`[long]`/`[.perf]`-tagged suite via `node tools/run-cpu-tests.js
   dsp_systems_tests`, twice.** The first attempt read 1 failed assertion in
   `feedback_ecology_perf_test.cpp:2156` (`REQUIRE( t.result.bestNs <= t.ceilingNs )`), but it
   followed a self-inflicted isolation violation: a `dsp_processors_tests` build+run had overlapped
   a `dsp_systems_tests` background run moments earlier — exactly the concurrent-load condition the
   project's own CPU-test isolation rule warns produces false reds. Per that rule ("re-run isolated
   before diagnosing"), I let the machine idle 60 s with nothing else running and re-ran the suite
   alone. **The clean re-run still read 1 failure — same file, `feedback_ecology_perf_test.cpp:2088`,
   a different assertion — and Phase 7's own two perf cases (`BloomEngine_CpuBudget`,
   `BloomEngine_StageCostProbe`) both passed.** Full detail in "CPU-isolation re-check result"
   below; this is a pre-existing Phase 4/5 finding, out of scope for this report.

Nothing in this phase's own code (`bloom_engine.h` or the four `bloom_engine_*_test.cpp` TUs) is
implicated in the `feedback_ecology_perf_test.cpp` reading — that TU is untouched by this phase
(`git diff --stat` above lists only `dsp/lint_all_headers.cpp`, `dsp/tests/CMakeLists.txt`, and the
three spec docs as modified, plus four new Bloom TUs and the new header).

### CPU-isolation re-check result

`node tools/run-cpu-tests.js dsp_systems_tests`, re-run clean and alone after a 60 s idle settle
(process `dsp_systems_tests.exe`, PID 62040, 00:26:03–00:35:07, nothing else executing on the
machine for the whole run): **`assertions: 438159 | 438158 passed | 1 failed`. Exactly one failure,
and it is not in this phase's code:**

```
F:\projects\iterum\dsp\tests\unit\systems\feedback_ecology_perf_test.cpp(2088): FAILED:
  REQUIRE( nsRef * kRegressionFactor <= kAbsoluteCeilingNs )
with expansion:
  164634.6 <= 160000.0
```

`feedback_ecology_perf_test.cpp` is pre-existing Phase 4/5 (`FeedbackEcology`) code — not part of
this phase's diff (`git diff --stat` above lists no `feedback_ecology*` file), not one of this
phase's `FR-*`/`SC-*` items, and untouched by anything Phase 7 changed. It missed its absolute
ns-ceiling by 2.9 % (164,634.6 vs 160,000.0) even in a clean, alone, post-idle run — the project's
own documented protocol for this class of test ("Before treating any of these as a defect: confirm
nothing else was running, then re-run that suite ALONE, after the machine has idled") was followed
exactly, and it still missed. That makes it worth flagging to a human as a possibly-real,
possibly-machine-specific regression in `FeedbackEcology`'s CPU budget — **but it is out of scope
for this Phase 7 report** and does not appear in the compliance table below.

**Phase 7's own perf items, freshly measured in this same clean isolated run, both pass:**
`BloomEngine_CpuBudget` — worst case `343.2 ns/block` against the `10667.0 ns/block` (0.1 % of one
core) budget, **3.22 % of the ceiling** (`bloom_engine_perf_test.cpp:789`); `BloomEngine_StageCostProbe`
ran with no assertion failures (WARN-only stage breakdown, as designed). This independently
confirms FR-072/FR-073/SC-011 with numbers of the same order as the task agents' own prior
measurements (449.6 / 341.2 ns/block) — all comfortably (>25×) inside budget.

## Compliance table

Each row is a spec item (`FR-*`/`SC-*`/`CC-*`); `SC-*` rows appear twice by design — once verified
against the implementation directly (`lens: fr`) and once verified by actually running the named
test case and quoting its output (`lens: sc`) — both passes are kept as separate rows rather than
collapsed, since they are independent checks.

| ID | Lens | Verdict | Evidence |
|---|---|---|---|
| FR-001 | fr | PASS | dsp/include/krate/dsp/systems/bloom_engine.h:111-125 (Layer-3 banner, includes only core/db_utils.h, core/pitch_utils.h, core/random.h, primitives/smoother.h), :138 `namespace Krate::DSP`, :193 `class BloomEngine`. `node tools/lint-layers.js` -> "OK - no layer-dependency violations in 5-layer DSP tree." |
| FR-002 | fr | PASS | bloom_engine.h:494 `processChunk(float*, float*, size_t, size_t)` is the only per-block entry point; the complete public surface (:383-839) contains no audio render method and no sample buffer parameter. Header comment :177-185 states the array-in/array-out contract. |
| FR-003 | fr | PASS | Every constant is a `static constexpr` member inside `class BloomEngine` (bloom_engine.h:204-304, :846-849). The only namespace-scope addition is the incomplete friend declaration `detail::BloomEngineNonFiniteProbe` (:162) — a type, not a constant. `node tools/lint-odr.js` -> "OK - 756 definitions scanned, no cross-file name collisions." |
| FR-004 | fr | PASS | bloom_engine.h:383-407 `prepare(double, const PrepareConfig&) noexcept`; :413-451 `reset() noexcept`; :817 `getAllocatedBytes()` returns 0. Test BloomEngine_NoAllocationAfterPrepare asserts `prepareAllocations == 0` (bloom_engine_test.cpp:1552) — passed. |
| FR-005 | fr | PASS | bloom_engine.h:494-521: nullptr on either array returns `parentCount` with no advance; `pc = std::min(parentCount, capacity_)`; `numSamples == 0` skips `advance()` and still calls `applyOutput()`. Test section "FR-005: rejected calls advance nothing and return the unclamped count" passed. |
| FR-006 | fr | PASS | bloom_engine.h:912-924 `advance()` carries `controlPhase_` across calls. Test BloomEngine_BlockPartitionInvariance section (child table/counters/render identical across four partitions) passed. |
| FR-007 | fr | PASS | bloom_engine.h:221-222 `kControlChunkSamples = 64` with a live `static_assert`. `processChunk` accepts any numSamples. |
| FR-008 | fr | PASS | Finiteness goes through `detail::isFinite`/`isNaN`/`isInf` only. `node tools/lint-nonfinite-symbols.js` -> "all clear (17 guarded files)". |
| FR-009 | fr | PASS | (a) every float setter rejects non-finite; (b) out-of-range Relation is a no-op, indexed getters return documented neutrals; (c) clamps present on every ranged setter; (d) owned slots written only with validated ratio and denormal-flushed amplitude; (e) non-finite incoming slot skipped in the parent scan. BloomEngine_ArgumentContract and BloomEngine_NonFiniteGuards passed. |
| FR-010 | fr | PASS | bloom_engine.h:1403 bounds the scan by `min(pc, reserveBase())`; `kMaxParents=8`, `kDefaultParentCount=4`, clamp [1,8]. BloomEngine_StrongestKParentSelection passed (20 crafted + 200 random arrays). |
| FR-011 | fr | PASS | `insertDescending()` uses strict `amp > parentAmp_[pos-1]` (ties -> lower index); `kSilentParentAmplitude=1e-5f` gates eligibility. Reference is `std::stable_sort`. |
| FR-012 | fr | PASS | `runEvent(const float* ratios, const float* amplitudes, ...)` — const parent arrays in the analysis path. |
| FR-013 | fr | PASS | `++spawnEvents_; ++parentScans_;` adjacent, only inside `runEvent()`. Test "FR-013: EXACTLY one parent scan per executed spawn event" passed. |
| FR-014 | fr | PASS | `if (parentSelected_ == 0) return;` — event consumed, nothing else touched. Test passed. |
| FR-015 | fr | PASS | `childrenPerEvent_` bounds the per-event loop; clamp [1,4], default 2. Test passed. |
| FR-016 | fr | PASS | `drawParent()` masks used parents via `parentUsedMask_`; relation drawn independently by weight; accepted siblings pushed into the spacing set. Tests passed. |
| FR-020 | fr | PASS | `relationFactor()` returns `kOctaveFactor=2.0f`, `kFifthFactor=1.5f`, or `centsToPitchRatioFast(cents)`; detune band [24,50] cents with a pinning static_assert. |
| FR-021 | fr | PASS | Out-of-[0.5,128] candidate is `return false` — a rejection, never a clamp. Test "FR-021: rejected, never clamped" passed. |
| FR-022 | fr | PASS | Spacing check against `kMinRatioSpacingLog2 = 24/1200`; occupancy set built from parents, live children and same-event siblings. |
| FR-023 | fr | PASS | `target = parentAmp * childGain_ * smoothedDepth / tiltGain(slot)` — no `<=1.0` clamp anywhere. SC-017 measured the compensated level within ±0.5 dB; negative control missed by >10 dB. |
| FR-024 | fr | PASS | `ch.ratio = candidateRatio;` latched once; `applyOutput` writes it unchanged every chunk — never re-derived. |
| FR-025 | fr | PASS | A failed `peekSlot()` increments counters and `continue`s — no stealing/eviction path exists. Test "refused EXACTLY ONCE" passed. |
| FR-026 | fr | PASS | `kMaxSpawnAttempts=4` retry loop; final-attempt detuned fallback keeps the original relation and increments `fallbackChildren_`. Test "retry/detuned-fallback path is LIVE" passed. |
| FR-030 | fr | PASS | Defaults 45/120/180 s, ranges [1,300]/[0,900]/[1,600]; four-phase clock advanced once per control step. SC-002 measured endIn=33750, hold=90000, fadeOut=135000 steps at 48 kHz. |
| FR-031 | fr | PASS | `smoothstep(u)=u²(3-2u)`; fade-out lands on exactly 0.0f. SC-002 measured all C1/monotonicity/plateau clauses within gate. |
| FR-032 | fr | PASS | `retire()` sets amplitude to exactly 0.0f and clears the slot bit; `applyOutput()` unconditionally repads the owned region before the child loop on the same chunk. Test passed. |
| FR-033 | fr | PASS | `fadeInSteps`/`fadeOutSteps`/`holdSteps` latched at spawn; phase derived from latched bounds only, never from live setters. SC-002 (f) passed. |
| FR-034 | fr | PASS | `std::array<Child, kMaxChildren=16> children_{}` — no container, no free list. |
| FR-035 | fr | PASS | `gate = dormant_ ? 0.0f : wake_ * smoothedDepth` feeds only the spawn probability; in-flight children keep advancing. SC-014 measured 0 events at wake=0, linear scaling at 0.25/0.5/1.0. |
| FR-036 | fr | PASS | Hold jitter is a bipolar draw latched at spawn; fades left unjittered; at jitter=0 every latched hold equals `getHoldSeconds()` exactly (SC-002 (e)). |
| FR-040 | fr | PASS | `triggerBloom()` sets `armed_=true`; `setSpawnRateHz` clamps [0, 0.05f]; both sources OR into one arm. |
| FR-041 | fr | PASS | Clock draw unconditional, before probability computed. SC-010 (a) measured mean inter-event time within the derived band at 5 sample rates; SC-008 (c) passed all three arms. |
| FR-042 | fr | PASS | `setDepth` retargets a 50 ms ramp; the single smoothed value feeds both probability and the spawn-time latch. SC-014 (a) and the in-flight non-rescaling test passed. |
| FR-043 | fr | PASS | `bool armed_` (not a counter), consumed once per control step. Test "(e) 50 triggers arm exactly one event" passed. |
| FR-050 | fr | PASS | `kMaxSlots=64` pinned by static_assert to `HarmonicCloud::kMaxPartials`; `reserveBase() = capacity_ - numChildSlots()`. SC-016 confirmed against a real cloud; falsification (capacity from kMaxPartials) silenced every child. |
| FR-051 | fr | PASS | `applyOutput()`: disengaged -> no write, unclamped return; engaged -> pads gap + owned region, writes live children, returns capacity_, never touches an index >= capacity_. SC-003's gap/sticky arms passed. |
| FR-052 | fr | PASS | Overlap counter increments exactly once per engaged call while `pc > base`, saturating at `kMaxOverlapCount`. Test arms (==0 and ==N) passed. |
| FR-053 | fr | PASS | `slotMask_` is the single occupancy authority; a legacy child's bit stays set even outside the current owned region. SC-003 fuzz (1000 configs × 30 sim-min) found no shared slot. |
| FR-054 | fr | PASS | `if (base >= cap) return false;` — numChildSlots()==0 means no child is ever placed. SC-014 (b) confirmed bit-unchanged arrays over 100k chunks. |
| FR-055 | fr | PASS | `numChildSlots()` re-derives `min(requested, capacity_)` on every read; legacy children skipped by write loop and spacing set but keep advancing their clock. SC-003's live shrink/grow arm passed. |
| FR-056 | fr | PASS | Pure `peekSlot()` peek, `commitSlot()` advances the cursor; no RNG draw for slot choice. SC-018 (a)-(d) passed, incl. cross-seed identical sequence. |
| FR-060 | fr | PASS | All 16 named setters present and noexcept (verified against bloom_engine.h line-by-line). |
| FR-061 | fr | PASS | All named getters present with documented out-of-range neutrals. Test "every indexed read returns its documented neutral" passed. |
| FR-062 | fr | PASS | `stateFinite()` scans every Child's ratio/target/amplitude. Probe arm proved the trap fires false on a poisoned live child, not only true. |
| FR-070 | fr | PASS | Append-only salt table with an overlap static_assert; clock RNG persistent, event RNG re-derived per event from `(seed, kSaltEvent, controlStep)`. BloomEngine_SeedDeterminism passed both arms. |
| FR-071 | fr | PASS | `AllocationScope` around prepare() and 10000 processChunk calls + full setter/trigger surface: 0 allocations both scopes. `getAllocatedBytes()==0` asserted. |
| FR-072 | fr | PASS | Measured worst case 449.6 ns/block (8×64-sample calls) vs the 10667.0 ns/block (0.1% of one core) budget — 4.2% of ceiling (T020 baseline run). Independently re-measured this session in a clean, alone, post-idle isolated run: `343.2 ns/block` — 3.22% of ceiling (`bloom_engine_perf_test.cpp:789`). |
| FR-073 | fr | PASS | Stage-cost probe [.perf] reported four attributable arms summing to within 0.2 ns/block of the direct 16-live measurement — WARN-reported, not asserted. Re-ran clean this session with no assertion failures. |
| FR-074 | fr | PASS | `WARN(sizeof(BloomEngine))` reported (≈1.27 KB) beside `getAllocatedBytes()==0` — declared, never pinned to a byte. |
| FR-080 | fr | PASS | `git diff --stat`: only `dsp/lint_all_headers.cpp` (+3), `dsp/tests/CMakeLists.txt` (+21), and the 3 spec docs are modified. `git diff --name-only HEAD -- dsp/include/` is EMPTY (re-verified this session, post-fix) — no shipped header touched; bloom_engine.h is untracked/new. |
| FR-081 | fr | PASS | Re-verified this session, post-fix: `dsp_systems_tests.exe` → "All tests passed (6277441 assertions in 1338 test cases)"; `dsp_processors_tests.exe` → "All tests passed (10697604 assertions in 3350 test cases)". |
| SC-001 | fr | PASS | BloomEngine_ChildSpawnAndDeathAreClickFree passed. Differential gate `bloomL<=refL && bloomR<=refR` over 10 seeds plus 0 detections in 200 ms post-spawn/post-death windows, with non-vacuity preconditions verified. |
| SC-002 | fr | PASS | BloomEngine_LifecycleTimingAndC1Shape [long] — all sections passed at the 45/120/180 s defaults over 4 parent amplitudes: 50%/99% timing, C1 endpoint ratio ≤0.10, monotonicity with plateau ≤750 steps, hold jitter. |
| SC-003 | fr | PASS | BloomEngine_SlotAccountingInvariantsUnderFuzz passed (3.622 s): 1000 seeded configs × 30 sim-min, poison-pattern gap check, out-of-capacity canary + full snapshot, sticky-engaged arm, live setCapacity shrink/grow arm. |
| SC-004 | fr | PASS (post-fix, independently re-verified) | Fresh run this session: `SC-004 (b) boundedness: bloom RMS minutes 5-10 = -8.99151 dBFS, last 5 minutes = -9.05524 dBFS, drift = -0.0637318 dB (band +-1.5); reference -9.05796 -> -9.05799 dBFS; peak bloom / reference = 1.02395 / 0.733417` — 1.02395 < kOutputClamp(2.0) and +2.91 dB < +6 dB. (a) centroid diff 10.650 Hz vs 8.537 Hz floor, sustained 235 s vs 60 s floor. (c) pooled 39 events over 5 seeds vs λ_total=37.5, floor 30. |
| SC-005 | fr | PASS | BloomEngine_StrongestKParentSelection passed all sections: 20 crafted + 200 random arrays, FR-013 and FR-014 clauses. Reference is `std::stable_sort`. |
| SC-006 | fr | PASS | BloomEngine_ChildRatioRelationships passed all 7 sections incl. the live retry/fallback arm and the real-HarmonicCloud consumer arm with negative control. |
| SC-007 | fr | PASS | BloomEngine_NoAllocationAfterPrepare passed: 0 allocations, `getAllocatedBytes()==0`, FR-074 footprint WARN reported. |
| SC-008 | fr | PASS | BloomEngine_SeedDeterminism, BlockPartitionInvariance, RngPositionIsStepPure all passed. No bit-exact float golden (lint-float-bit-goldens clean). |
| SC-009 | fr | PASS | BloomEngine_NonFiniteGuards passed all 4 sections in the sole `-fno-fast-math` Phase-7 TU. |
| SC-010 | fr | PASS | BloomEngine_SampleRateIndependence [long] passed at 5 sample rates; falsification quantified the float-accumulation error the integer-step clock avoids (up to 0.521% at 192 kHz). |
| SC-011 | fr | PASS | BloomEngine_CpuBudget [.perf]: 449.6 ns/block worst case vs 10667.0 ns/block threshold — 4.24% of budget, run single-process with no concurrent load. Independently re-confirmed this session: `node tools/run-cpu-tests.js dsp_systems_tests` alone, post-60s-idle → 343.2 ns/block, 3.22% of ceiling; the run's one failure (feedback_ecology_perf_test.cpp:2088) is unrelated pre-existing Phase 4/5 code, not a Bloom/Phase-7 item. |
| SC-012 | fr | PASS | BloomEngine_CloudContractAssumptions passed all 7 static_assert sections; dsp_systems_tests and dsp_processors_tests both fully green. |
| SC-013 | fr | PASS | All 6 lint/portability gates clean; clang-tidy 0 warnings on the 4 TUs + header via `-header-filter='bloom_engine\.h'`. |
| SC-014 | fr | PASS | BloomEngine_DisabledIsBitIdenticalPassThrough passed all 7 sections (depth=0, numChildSlots=0, dormant, unclamped return, fractional wake/depth, in-flight non-rescale). |
| SC-015 | fr | PASS | BloomEngine_EightHourAcceleratedSoak [long] passed over 25 seeds — maxLive=16/16, ratio span inside [0.5,128], worst target error 0, worst amp overshoot 0. |
| SC-016 | fr | PASS | BloomEngine_ChildrenAreAudibleWithinCloudActiveCount passed both sections incl. the falsification (capacity from kMaxPartials silences every child). |
| SC-017 | fr | PASS | BloomEngine_TiltCompensationMatchesIntendedLevel passed: compensated level within ±0.5 dB; negative control misses by >10 dB. |
| SC-018 | fr | PASS | BloomEngine_OwnedSlotRoundRobinRotation passed all 4 clauses incl. the committed-sequence arm that skips refused children and ignores the seed. |
| SC-001 | sc | PASS | `dsp_systems_tests.exe "BloomEngine_ChildSpawnAndDeathAreClickFree"` -> "All tests passed (350 assertions in 1 test case)". |
| SC-002 | sc | PASS | `dsp_systems_tests.exe "BloomEngine_LifecycleTimingAndC1Shape"` -> "All tests passed (914 assertions in 1 test case)". Gates verified against source line numbers, not relaxed. |
| SC-003 | sc | PASS | `dsp_systems_tests.exe "BloomEngine_SlotAccountingInvariantsUnderFuzz"` -> "All tests passed (2079 assertions in 1 test case)". |
| SC-004 | sc | PASS (post-fix, independently re-verified this session) | Historical per-task run: `dsp_systems_tests.exe "BloomEngine_ThirtyMinuteEvolutionTrajectory"` -> "All tests passed (38 assertions in 1 test case)" with peak bloom/reference = 1.02395/0.733417 (< kOutputClamp 2.0; +2.91 dB, inside +6 dB). Independently re-confirmed this session inside a full fresh 1338-case suite run (see "Independent re-verification" above). |
| SC-005 | sc | PASS | `dsp_systems_tests.exe "BloomEngine_StrongestKParentSelection"` -> "All tests passed (3576 assertions in 1 test case)". |
| SC-006 | sc | PASS | `dsp_systems_tests.exe "BloomEngine_ChildRatioRelationships"` -> "All tests passed (4580 assertions in 1 test case)". |
| SC-007 | sc | PASS | `dsp_systems_tests.exe "BloomEngine_NoAllocationAfterPrepare"` -> "All tests passed (6 assertions in 1 test case)". |
| SC-008 | sc | PASS | Three cases each "All tests passed": SeedDeterminism (22 assertions), BlockPartitionInvariance (166), RngPositionIsStepPure (182). |
| SC-009 | sc | PASS | `dsp_systems_tests.exe "BloomEngine_NonFiniteGuards"` -> "All tests passed (79763 assertions in 1 test case)". |
| SC-010 | sc | PASS | `dsp_systems_tests.exe "BloomEngine_SampleRateIndependence"` -> "All tests passed (567 assertions in 1 test case)". All 5 rates inside the derived ±0.5%/8.94% bands. |
| SC-011 | sc | PASS | Run alone: `"BloomEngine_CpuBudget,BloomEngine_StageCostProbe"` -> "All tests passed (40 assertions in 2 test cases)". Worst case 341.2-449.6 ns/block across runs vs 10667.0 ns/block budget — well inside. |
| SC-012 | sc | PASS | `git diff --name-only HEAD -- dsp/include/` EMPTY; both consumer suites green; `BloomEngine_CloudContractAssumptions` -> "All tests passed (17 assertions in 1 test case)". |
| SC-013 | sc | PASS | All 6 lint scripts + check-portability exit 0; clang-tidy exit 0, 0 warnings attributable to the new header. |
| SC-014 | sc | PASS | `dsp_systems_tests.exe "BloomEngine_DisabledIsBitIdenticalPassThrough"` -> "All tests passed (63 assertions in 1 test case)". |
| SC-015 | sc | PASS | Run alone: `"BloomEngine_EightHourAcceleratedSoak"` -> "All tests passed (451 assertions in 1 test case)" over 25 seeds. |
| SC-016 | sc | PASS | `dsp_systems_tests.exe "BloomEngine_ChildrenAreAudibleWithinCloudActiveCount"` -> "All tests passed (73 assertions in 1 test case)". |
| SC-017 | sc | PASS | `dsp_systems_tests.exe "BloomEngine_TiltCompensationMatchesIntendedLevel"` -> "All tests passed (164 assertions in 1 test case)". |
| SC-018 | sc | PASS | `dsp_systems_tests.exe "BloomEngine_OwnedSlotRoundRobinRotation"` -> "All tests passed (50 assertions in 1 test case)". |
| CC-rt | constraints | PASS | Full-header hazard grep (new/delete/malloc/vector/string/mutex/throw/try/printf/resize/push_back/shared_ptr/unique_ptr/...) returned NONE; all functions noexcept; all state fixed-size arrays. `BloomEngine_NoAllocationAfterPrepare` confirms 0 allocations both for prepare() and steady-state. |
| CC-layers | constraints | PASS | Layer-3 header includes only Layer 0/1 + stdlib. `lint-layers.js` OK; `lint-odr.js` OK (756 definitions, no collisions); TUs registered explicitly (not globbed). |
| CC-naming | constraints | PASS | Classes/enums PascalCase, methods camelCase, members trailing-underscore (sweep found none violating), constants kPascalCase (only two non-`k` constexpr identifiers, both functions correctly in camelCase). No parameter IDs in this phase (pure DSP library). |
| CC-warnings | constraints | PASS | Forced (non-incremental) MSVC /W4 build of all 4 new TUs + header via lint_all_headers.cpp: 0 warnings. Re-verified this session: forced rebuild of dsp_systems_tests and dsp_processors_tests after the T023 header edits — both exit 0, 0 warnings. |
| CC-portability | constraints | PASS | `check-portability.js` exit 0, "all clear -- 5 compiled" (g++ on all 4 Bloom TUs + lint_all_headers.cpp). Re-run independently this session after the post-T023 SC-004 amendment: all 6 gates still clean. |

## Implementation notes — deviations reported by task agents

- **T003/T020 (CPU budget, FR-072/FR-073/SC-011):** the perf-probe stand-in figures were later
  re-pointed at the real engine by T020; T020's `kBaselineWorstCaseNs` regression-guard constant is
  explicitly flagged as a **provisional projection** (2500.0 ns) pending a first real measurement
  transcription — both compile-time sanity bounds (`<= kBudgetNs*1.5` and `>= kNoOpFloorNs`) are
  checked in regardless. The measured worst case (449.6–503.2 ns/block across runs, always
  <5% of the 10667 ns budget) is well inside that provisional ceiling either way.
- **T009 (FR-031 smoothstep):** deliberately computed in `double` rather than the plan's literal
  `float` expression — measured 10 one-ulp monotonicity dips near saturation with the float form at
  180 s fade durations (SC-002 (d) requires strict monotonicity); double arithmetic with a single
  final rounding removes them. Documented with the measured ulp/relative-increment comparison in the
  header doc comment.
- **T014:** the seed-determinism render was shortened from a literal "10 minutes" reading to 4 s
  (the structural, cloud-free comparison already runs the full 10 simulated minutes; only the bounded
  fingerprint-render arm was shortened, since a 600 s stereo cloud render twice adds ~57M frames for
  no additional property under test).
- **T015:** falsification found the accumulated-float-seconds alternative design misses the ±0.5%
  SC-010 band specifically at 192 kHz (0.521% vs 0.5%) and nearly misses at 88.2 kHz (0.495%) — the
  integer-step-count clock design (as shipped) has 0% error at every rate. Flagged so the 5-rate list
  in SC-010 is never trimmed to fewer rates.
- **T018 (SC-017 tilt compensation):** measured as a **ratio against slot 0** on the same chunk,
  not an absolute dB comparison against the caller's supplied amplitude — the cloud's FR-017 RMS
  normalizer scales every partial by one shared scalar per chunk, so an absolute comparison would
  fail by ~9 dB on **correct** code. `tiltGain(0) == 1.0f` exactly, so slot 0 recovers the caller's
  amplitude exactly and the shared normalizer scalar cancels in the ratio. Documented and justified
  in the test file.
- **T019:** the SC-015 soak fixture uses a deliberately-chosen 105-cent parent grid (not the more
  "natural" 100 or 120 cents), because either of those puts every octave image exactly on an
  existing grid partial (`1200 mod g == 0`) and refuses every octave child — a vacuous fixture that
  would still pass the test while proving nothing.
- **T021 (audit):** found and corrected a stale claim in the tasks.md text — the layer-test helper
  tree is `dsp/tests/unit/test_helpers/`, not `dsp/tests/test_helpers/` (the latter does not exist).
  Both were checked and both are clean of Phase-7 changes.
- **T022 → resolved (see "Honest gap history" above):** the one real, reproducible gap this phase
  produced. Not a code defect — a spec-internal contradiction between FR-023 and the original
  SC-004 (b) wording. Closed by amending the spec/plan/tasks and the test's assertion (Option A),
  confirmed by this report's own independent fresh test run.
- **T023 (clang-tidy):** found 26 real warnings (16 `bugprone-suspicious-memory-comparison` on
  intentional bit-exact `memcmp` checks — several deliberately probing NaN/Inf-poisoned arrays,
  where a "compare by value" rewrite would be wrong since NaN != NaN would vacuously pass a
  no-write assertion; 5 `readability-avoid-nested-conditional-operator`; 1
  `performance-enum-size`; 2 header `modernize-use-auto`; 2 header
  `readability-non-const-parameter` on `runEvent`'s read-only pointer parameters). Fixed all with a
  mix of rewrites and justified `NOLINT` (matching existing repo precedent for the memcmp cases).
  Two additional header findings were suppressed by judgement rather than mechanically rewritten
  (`getAllocatedBytes()` kept non-static to match the shipped `ResonanceDriftNetwork` sibling's
  identical shape; `stateFinite()`'s loop kept as a hand-written loop rather than
  `std::ranges::any_of`, since the tree has zero prior `std::ranges` usage) — flagged in case a
  reviewer prefers the mechanical rewrite instead. Also found and fixed a **process** gap:
  `build/windows-ninja/compile_commands.json` predated the Bloom TU CMake registration and
  contained zero Bloom entries, so clang-tidy would have silently linted nothing; reconfigured
  before linting.
- **This report's own CPU-isolation finding (out of scope for Phase 7, flagged for the human):**
  a `feedback_ecology_perf_test.cpp` CPU-budget assertion (`REQUIRE(nsRef * kRegressionFactor <=
  kAbsoluteCeilingNs)`, pre-existing Phase 4/5 `FeedbackEcology` code, no part of this phase's diff)
  failed twice: once on a run contaminated by a self-inflicted concurrent-load violation (a
  `dsp_processors_tests` build+run overlapping a `dsp_systems_tests` background run), and again on
  a clean, alone, post-60s-idle re-run — `164634.6 <= 160000.0` missed by 2.9 %. Following the
  project's own documented protocol for this class of test exactly (confirm isolation, re-run
  alone after idling) did not clear it, which is worth a human's attention as a possibly-real (or
  possibly this-machine-today-specific) `FeedbackEcology` budget regression. It is **not** a Phase 7
  item, is not touched by this phase's diff, and does not affect this report's COMPLETE status —
  Phase 7's own perf items (`BloomEngine_CpuBudget`, `BloomEngine_StageCostProbe`) passed cleanly in
  the exact same isolated run (343.2 ns/block, 3.22 % of budget).

## Remaining gates for the human loop

1. **clang-tidy, full target.** T023 ran clang-tidy scoped to the 4 new Bloom TUs plus the new
   header (via `dsp/lint_all_headers.cpp` with an explicit `-header-filter`), not the full `dsp`
   target — that scoped run is 0 warnings. Before commit, run the canonical full pass:
   `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` (regenerate
   `compile_commands.json` first if it predates this phase's CMake changes, as T023 found it did).
2. **Commit.** Nothing in this phase has been committed (`git log` still shows
   `1004b0f0 docs(vorago): spec, plan and tasks for phase 7 harmonic bloom` as HEAD). All Phase 7
   files remain modified/untracked in the working tree.
3. **pluginval — not applicable.** Phase 7 is pure `KrateDSP` library work (`dsp/include/krate/dsp/systems/bloom_engine.h`
   plus tests); no plugin source changed. Per CLAUDE.md, pluginval only applies once a plugin
   consumes this component (Phase 11+).
4. **FYI, not a Phase 7 gate:** `feedback_ecology_perf_test.cpp:2088` (pre-existing Phase 4/5 code)
   missed its CPU ceiling by 2.9 % in a clean, isolated, post-idle re-run this session
   (`164634.6 <= 160000.0`). Worth a human decision on whether that is machine-specific drift on
   this box today or a real regression in `FeedbackEcology` — see "Implementation notes" above for
   the full reading. Does not block this phase.
