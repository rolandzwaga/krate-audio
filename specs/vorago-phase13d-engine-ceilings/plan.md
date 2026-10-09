# Implementation Plan: Vorago Phase 13d — Engine Ceilings

**Spec:** `specs/vorago-phase13d-engine-ceilings/spec.md` (reviewed; Clarifications Q1–Q8 and OQ-1 of 2026-10-07)
**Roadmap:** `specs/Vorago-roadmap.md` Phase 13d (lines 663–707); Cross-Cutting Constraints (765–789)
**Branch / base:** `feat/vorago-phase1-events-modulation` at `502e5243`
**Status:** PLAN. This document writes no production code.

Every `file:line` below was read in the planning session on the `502e5243` tree. Where this plan's line number
differs from the spec's, the plan's is the one read this session (for example `readPilotLever()` is at
`preset_pilot_test.cpp:881`, its doc block at `:875-880`). §10 sweeps every name this plan introduces (FR-038).

---

## 0. What this plan decides (read first)

1. **No new class, no new TU, no CMake change.** Production edits stay in `vorago_voice.h`, `vorago_engine.h` and
   `vorago_macro_matrix.h` (FR-011). `bloom_engine.h`, `feedback_ecology.h`, `resonance_drift_network.h` and
   `ecosystem_engine.h` are **not edited by any planned rung**: every lever reaches them through setters that already
   exist (`BloomEngine::setParentCount` `bloom_engine.h:556`, `setChildrenPerEvent` `:560`,
   `FeedbackEcology::setLoopWake` `feedback_ecology.h:1296`, `processBlockTapped` `:979`). One append-only edit is held
   in reserve for E6.hi / E7.hi only, and its instrument decides whether it is needed (§3.3). No Seraphis-consumed
   header is touched.
2. **The instruments come first** (FR-010). Four hidden read-outs are added before any rung is built (§2):
   - a loop-bus meter for E4's loops-alive conjunct (FR-004, FR-021b);
   - a per-`Kind` eco-lane read-out for E6.hi / E7.hi (FR-015);
   - a bloom read-out for E1;
   - a Life-row read-back for M10 (FR-019).

   Two existing instruments are reused rather than rebuilt:
   - the Phase 12 kRows **row-emulation probe** (`dsp/tests/unit/systems/vorago_macro_retune_probe_test.cpp:1-55`). It
     emulates a candidate row through `setTargetBase` on the SC-008 fixture, and it serves as the macro-test
     pre-screen for M2, M4, M5 and M10;
   - the pilot's `VORAGO_PILOT_OVERRIDE`, for every preset-level premise.
3. **Seams vs rebuild (FR-012).**
   - **Seams, option (a).** The E1 and E4 route constants become voice members. `prepare()` installs each one from
     its unchanged compiled constant. Each gets a `VoragoEngine` fan-out, and there are seven new
     `VORAGO_PILOT_LEVER` keys (§4). These two ladders are multi-dimensional and need many FR-010c read-set runs, so a
     rebuild per rung is not affordable.
   - **Rebuild, option (b).** Everything else is rebuild-laddered:
     - every macro-row rung (M2, M4, M5, M10);
     - every E6.hi / E7.hi constant rung;
     - the rate-comp exponent;
     - any E1 / E4 rung that changes a *compiled* split or a combine gain.

     Macro rows are `static constexpr` data (`vorago_macro_matrix.h:315`) that `dsp_systems_tests`' SweepAxes /
     NoZipper also consume, so a plugin-side seam could not reach the tests that gate them.
   - **The probe's secondary read-out learns to carry the tweak** (§4.4). This lifts the T067 restriction
     (`preset_pilot_test.cpp:1013-1019`), because FR-010c read sets include secondaries.
4. **Three structural findings shape the ladders.** All three are code-read this session, and each is confirmed by an
   instrument before a rung is chosen.
   - **E1 is clamp-limited on the stored surface.**
     - The Partial lane is a *sum* onto the bloom depth, clamped at 1 (`vorago_voice.h:2233`). Bloom Colony stores
       bloom depth 0.50 (`tools/vorago_preset_defs.h:1049`). So the route can move the depth by at most 0.5.
     - R_k0 still spawns children at depth 0.5. The spawn gate is `wake_ * smoothedDepth` (`bloom_engine.h:960`), and
       the latched child target is `parentAmp * childGain_ * smoothedDepth / tiltGain(slot)` (`:1340`).
     - 13c's gain rungs 1.5, 2.0 and 3.0 all read 1.1989 (13c `compliance.md:50`), which is what saturation
       predicts.
     - So the E1 lever pairs the route gain with a **companion** that hands the bloom to the route: the stored bloom
       depth is lowered (FR-025b). The attachment half is the bloom's parent count K and its children per event.
   - **E4's lever must keep the raw lane in the combine.**
     - `checkWakes` (`vorago_ecosystem_lever_test.cpp:374-386`) asserts
       `getLoopWakeAmount(l) == combineWake(loopWakeBase(v, l), eco, sched)` with the *raw* lane.
     - It is called from `VoragoVoice_EcosystemLeverMapping` (`:989`, call at `:1021`), which FR-032 names as
       unedited. A gain on the eco term breaks it.
     - Lowering the loop-wake **base** widens the same route's audible span and keeps the test green, because the test
       reads the base through `Probe::loopWakeBase`.
     - Mechanism W1 (the base) is laddered first. Mechanism W2 (an eco gain) is rebuild-only and adoptable only by a
       user ruling (§3.2, §11).
   - **A new `VoragoMacroTarget` enumerator is a spec amendment.**
     - `static_assert(... VoragoMacroTarget::Count == 41 ...)` (`vorago_macro_matrix.h:1239-1240`) says so in its own
       message.
     - So every macro rung re-aims or widens a row onto one of the 41 existing targets, and carries that target's
       existing base (`everyRowSharesOneBasePerTarget`, `:1233-1234`).
     - A new `kRows` row is allowed (FR-016). `kNumRows` is a hand-written literal, `kNumRows = 51` (`:276`), under a
       per-macro tally comment (`:272-275`: "3 Darkness + 3 Age + … = 51"). Every added row must bump both by hand.
       Production code pins the count: `static_assert(VoragoMacroMatrix::kRows.size() == VoragoMacroMatrix::kNumRows,
       "FR-060: kNumRows must match the table")` (`:1223-1224`) fails the build on a forgotten bump. No Vorago *test*
       pins it (grep this session: the only test use is the loop in `VoragoMacro_NeutralIsIdentity`,
       `vorago_macro_test.cpp:1428`). The bump is a production edit inside `vorago_macro_matrix.h` (FR-011), not an
       FR-034 test edit, and it is logged with the rung (§3.4, §3.5, §8 L4 / L5, SC-012 in §6.3).
5. **Order of work is FR-010b's:**
   1. before-record;
   2. instruments;
   3. E1 → E4 → E6.hi / E7.hi → M2 → M5 → M4 + rho → M10;
   4. final-tree re-measure;
   5. regression gates;
   6. one fingerprint re-harvest;
   7. confirming pass.

   The build adopts each lever under FR-010d's rule. The user is stopped only for the items in §11.

---

## 1. Verified facts this plan builds on

### 1.1 `VoragoVoice` — `dsp/include/krate/dsp/systems/vorago_voice.h` (Layer 3)

- **Route writes.** All are inside `void applyIdentityLanes(const IdentityLanes& lanes) noexcept` (`:2203`):
  - loop wake: `ecology_.setLoopWake(l, combineWake(loopWakeBase_[l], lanes.eco[kFeedback][l],
    lanes.sched[kFeedback][l]))` (`:2224-2228`);
  - the Partial pair: `cloud_.setMutation(std::clamp(mutationBase_ + kPartialLaneGain * partialEco, 0.0f, 1.0f))`
    and `bloom_.setDepth(std::clamp(bloomDepthBase_ + kPartialLaneGain * partialEco, 0.0f, 1.0f))` (`:2232-2233`);
  - the Feedback lever (`:2272-2284`):
    - `x = shapeLeverInput(lanes.eco[kFeedback][l], kLeverInputGain[kFeedback])`;
    - `loopGainOffset_[l] = kLoopGainLeverSpan * x`, then `writeLoopGain(l)`;
    - the ring coupling `std::clamp(kRingCouplingBase + kCouplingLeverSpan * x, 0.0f,
      FeedbackEcology::kMaxCouplingPerPair)`, written only when it changed.
- **`combineWake`.** `[[nodiscard]] static constexpr float combineWake(float base, float eco, float sched) noexcept`
  returns `std::max(base, std::max(eco, sched))` (`:1091-1094`). **Not changed** (Non-goals).
- **Constants:**
  - `kPeakWakeBase = kLoopWakeBase = 0.45f` (`:430-431`, public);
  - `kLoopGainLeverSpan = 0.18f` (`:1681`);
  - `kPartialLaneGain = 1.0f`, `kGhostLaneGain = 1.0f` (`:1685-1686`);
  - `kRingCouplingBase = 0.06f`, `kCouplingLeverSpan = 0.30f` (`:1694-1695`);
  - `kFreqWanderLeverSpanSemis = 3.0f` (`:1701`);
  - `kWanderLeverRateCompExponent = 0.75f` and `wanderLeverRateComp(float)` (`:1725-1731`);
  - `kLeverInputGain{1, 2, 2, 2, 1}`, with the static_assert that Partial and Ghost stay 1.0 (`:1742-1747`);
  - `kMinRetunedWakeBase = 0.05f`, and its static_assert over both wake bases (`:1750-1755`);
  - `kCloudRichnessFloor = 0.6346f` (`:379`), `kBloomChildGain = 1.5f` (`:381`);
  - `kEcologyWetMakeupDb = 30.0f` (`:385`), `kBreathGravityLaneGain = 3.0f` (`:390`), `kTidalRate = 0.8f` (`:394`).
- **`prepare()` installs:**
  - `loopWakeBase_.fill(kLoopWakeBase)` (`:654`);
  - `setBloomDepth(0.60f)` (`:657`);
  - `bloom_.setChildGain(kBloomChildGain)` (`:658`);
  - `ecology_.setWetGain(kEcologyWetMakeupDb)` (`:644`);
  - the ring coupling at `kRingCouplingBase` (`:650-652`).

  It does **not** call `bloom_.setParentCount` or `setChildrenPerEvent`. So the component defaults stand:
  `kDefaultParentCount = 4` and `kDefaultChildrenPerEvent = 2` (`bloom_engine.h:211-213`).
- **`void writeLoopGain(std::size_t l) noexcept`** clamps `loopGainBase_ + loopGainOffset_[l]` to
  `[kMinLoopGain, kMaxLoopGain]` (`:1774-1780`).
- **Ecology is processed in place, without taps:** `ecology_.processBlock(excL_.data(), excR_.data(), excL_.data(),
  excR_.data(), n);` (`:2610`). This is inside `renderOneChunk()` (`:2547`), which always renders exactly
  `kControlChunkSamples` into the carry buffer that `processStereoBlock` serves (`:902-929`).
- **Setters:**
  - `setEcosystemDepth` (`:1501-1506`): clamp [0, 1], non-finite rejected;
  - `setEcosystemSyncRate` (`:1547`) and `setEcosystemSelfAffinity` (`:1556-1561`): pass-throughs to
    `EcosystemEngine`;
  - `setEventRateScale` (`:1566-1571`): clamp [0.1, 10];
  - `setBloomDepth` (`:1576-1590`): writes `bloomDepthBase_`;
  - `setBloomSpawnRateHz` (`:1593`);
  - the 13c seam forwards `setBloomChildGain` / `setEcologyWetMakeupDb` (`:1596-1603`).
- **Const accessors the probes use:** `cloud()`, `resonance()`, `ecology()`, `bloom()`, `ecosystem()`
  (`:1637-1642`).
- **No lane survives `publishIdentity()`.** It builds a **local** `IdentityLanes` (`:2369-2380`; the struct at
  `:2049-2052` holds eco and sched arrays per `Kind`). So a lane read-out needs a member (§2.3).
- **The parent spectrum handed to the bloom is the voice's nominal law:**
  `amplitudes_[i] = (i < userCount) ? std::exp2(-p * kHarmonicCloudLog2N[i]) : 0.0f` (`:2478-2485`), passed with
  `parentCount_ = bloom_.reserveBase()` (`:2465`). Parents above the user's count are exactly 0 and are never scanned
  (`bloom_engine.h:1418`). By code-read the strongest K parents are therefore always the lowest harmonics. That is
  why the "attachment" half below is about *how many* parents the children share (K) and *how many* children an event
  makes, not about a silent-parent fix.

### 1.2 `VoragoEngine` — `systems/vorago_engine.h` (Layer 3)

- `kMaxVoices = 6` (`:231`), `kMaxBlockSamples = 2048` (`:238`).
- The render loop slices at the control grid, `slice = std::min(n - done, kControlChunkSamples - phase)` (`:1194`).
  It calls `voices_[v].processStereoBlock(vL_.data(), vR_.data(), slice)` on rendering slots only (`:1205-1225`), so
  no voice call is longer than 64 samples.
- The 13c seam pattern:
  - `setBloomChildGain` / `getBloomChildGain` fan out over `kMaxVoices` and read slot 0 (`:1040-1045`);
  - `setGhostDensity` rejects non-finite input itself (`:1056-1066`).
- `[[nodiscard]] const VoragoVoice& getVoice(std::size_t index) const noexcept` (`:1371`).

### 1.3 `VoragoMacroMatrix` — `systems/vorago_macro_matrix.h` (Layer 3)

- `struct VoragoMacroRow { macro; owner; target; float base; float amount; ModCurve curve; }` (`:233-257`).
- `kNumRows = 51` (`:276`); `kRows` (`:315`). The rows this phase may touch, read this session:

  | Line | Macro → target | base | amount | curve |
  |---|---|---|---|---|
  | 342 | Age → BodyDamping | 0.25 | 0.55 | Linear |
  | 348 | Age → CavernDecaySeconds | 20 | −14 | Linear |
  | 354 | Age → CloudSpectralTiltDb | −4 | −4 | Linear |
  | 402 | Movement → CloudDriftDepthCents | 8 | 42 | Linear |
  | 408 | Movement → ResonanceWanderRate | 0.03 | 0.97 | Exponential |
  | 414 | Movement → NoiseWanderRate | 0.03 | 0.97 | Exponential |
  | 420 | Movement → BreathingDepth | 0.30 | 0.70 | Linear |
  | 426 | Movement → CavernDamperDepth | 0.35 | 0.45 | Linear |
  | 439 | Gravity → ResonanceGravity | 0 | 1 | Linear |
  | 450 | Gravity → ResonanceOctaveLock | 0 | 1 | Linear |
  | 638 | Life → EcosystemDepth | 0.85 | 0.15 | Linear |
  | 650 | Life → EventRateScale | 1 | 9 | Linear |
  | 656 | Life → BloomSpawnRateHz | `BloomEngine::kDefaultSpawnRateHz` | 0.0208 | Exponential |

- **Targets with an existing base that a re-aim may reuse:**
  - `CloudMutation` (0.15; Entropy `:460`);
  - `CloudSpectralTiltDb` (−4; four rows);
  - `SubToneLevelOffsetDb` (0; Pressure `:530`, Weight `:541`, Mass `:759`);
  - `CavernDarkness` (0.80; `:325`, `:597`);
  - `BreathingIrregularity` (0.30; Entropy `:478`);
  - `TidalDepth` (0.40; Fog `:616`).
- **`contributionOf`** (`:1168-1176`) is **private**: the class turns private at `:1150`. Gravity uses
  `g = (m − 0.5)·2` and contributes `amount * applyModCurve(curve, |g|) * sign(g)`.
- **`evaluateAll()`** (`:1185-1197`) seeds each target once, from the override or the literal, and adds every row's
  contribution. `setTargetBase` (`:1014-1021`) does not clamp.
- **The compile-time guards** (`:1223-1243`):
  - size: `kRows.size() == kNumRows` (`:1223-1224`). `kNumRows` is the literal 51 (`:276`) with a per-macro tally
    comment (`:272-275`), so an added row must bump both by hand; this assert is what catches a forgotten bump;
  - owner validity;
  - cavern POD fields;
  - no Stepped curve;
  - every target claimed;
  - one base per target;
  - `Count == 41`;
  - `kNumMacros == 12`;
  - cavern block last.

### 1.4 Read-only components

- **`BloomEngine`** (`bloom_engine.h:193`):
  - `setDepth` (`:537-543`);
  - `setSpawnRateHz` (`:548-553`), clamp `[0, kMaxSpawnRateHz = 0.05]` (`:285`);
  - `setParentCount(std::size_t)` (`:556-558`), clamp `[1, kMaxParents = 8]`;
  - `setChildrenPerEvent(std::size_t)` (`:560-563`), clamp `[1, kMaxChildrenPerEvent = 4]`;
  - `setChildGain` (`:566-570`), clamp `[0, kMaxChildGain = 2]`.

  Per control step:
  - `smoothedDepth = depthRamp_.process()` (`:949`);
  - `gate = dormant_ ? 0 : wake_ * smoothedDepth` (`:960`);
  - spawn probability `spawnRateHz_ * gate * 64 / fs` (`:961-962`).

  The strongest-K scan skips any parent with `a <= kSilentParentAmplitude` (`:1418`).
- **`FeedbackEcology`** (`feedback_ecology.h:185`):
  - `processBlockTapped(inL, inR, outL, outR, float* const* loopTaps, numSamples)` (`:979-998`);
  - the tap is written as `loopTaps[i][tapOffset + s] = prevOut_[i]` after the output stage (`:2278-2283`), where
    `prevOut_[i] = flushDenormal(y * gate[i])` (`:2248-2251`). That is the per-loop wet signal after its wake gate,
    before the wet trim and the mix;
  - `processBlock` forwards with `loopTaps = nullptr` (`:949-952`);
  - `setLoopWake` clamps to [0, 1] and rejects non-finite input (`:1296-1301`);
  - `gateSteady` snaps `wakeAmount <= kWakeSilenceEpsilon` to 0 (`:2304-2309`). That is the only route to the FR-063
    sleep edge from the wake surface;
  - read-outs: `getLoopWakeAmount` (`:1428`), `isLoopDormant` (`:1432`), `getLoopGate` (`:1520`).
- **`EcosystemEngine`** (`ecosystem_engine.h:143`): `setSyncRate` clamps to [0, 0.5] (`:595-600`);
  `getAgentEnergy(i)` (`:890`).

### 1.5 Harness and probe — `plugins/vorago/tests/`

- **`struct RenderSpec`** (`preset_test_support.h:284-299`) carries `engineTweak`. **`struct SweepCapture`**
  (`:301-310`) carries `blockPowerL/R` (one sum of squares per 512-sample block) and `tweakHeld`.
- **`renderPreset`** (`:333-470`) applies the tweak after `loadState`. It compares `detail::readTweakLevers` (a
  `std::array<float, 4>`, `:318-321`) before and after block 0.
- **Windows:**
  - `tenSecondWindows(a, b, sr)` (`:1872`);
  - `windowDb(cap, w)` over block power (`:1822`);
  - `kArmWindowSeconds = 10.0` (`:1768`), `kSilenceDb = -60.0` (`:1770`);
  - `evaluateArms` (`:2012`).
- **Route arms.** `routeOverrides(kept, depthZero)` (`:2300-2318`):
  - R_k ablates the sections of the *other four* route destinations;
  - R_0 ablates all five;
  - `depthZero` adds `900 → 0`;
  - `kRouteDestinations` pairs E1 with S6 (`:2172-2180`);
  - `attribBase = d(R_0, R_00)` (`:2749`).

  So, by code-read, **every route write that survives with all five sections ablated lands in `attribBase`**. The
  Partial lane's mutation write (`vorago_voice.h:2232`) is one of them, because the cloud is not a route section.
- **`computeVerificationVector(...)`** (`:2580`) renders internally. So `runPrimaryProbe` FAILs a lever run that
  asks for secondaries (`preset_pilot_test.cpp:1013-1019`).
- **The pilot probe:**
  - `PilotLever readPilotLever()` (`preset_pilot_test.cpp:881-934`): four keys; an unknown key FAILs (`:917-918`);
  - `computeLeverTakes` (`:942-1002`) REQUIREs `held` per take;
  - print lines: `route arms:` (`:1116`), `arm1@44.1k:` (`:1172`), `levels:` (`:1196`), `secondary <cell>:`
    (`:1240`);
  - `Vorago_PresetPilot_PrimaryProbe` (`:1347`), `VORAGO_PILOT_OVERRIDE` (`:1389-1420`), `VORAGO_PILOT_MASTER_TRIM`
    (`:1422-1427`), and `VORAGO_PILOT_ITERATE`, which asserts no override and no trim (`:1352-1353`).
- **Sweep and 13b probe:**
  - the sweep reads `VORAGO_SWEEP_SHARD`, `VORAGO_SWEEP_IN`, `VORAGO_SWEEP_OUT` and `VORAGO_SWEEP_THREADS`
    (`integration/preset_sweep_test.cpp`);
  - `Vorago_FactoryPresets_TreeMatchesGenerator` (`unit/preset/factory_preset_test.cpp:1807`);
  - the 13b probe options are `VORAGO_PROBE_SURFACE`, `VORAGO_PROBE_KNOBS`, `VORAGO_PROBE_SEEDS`,
    `VORAGO_PROBE_WAKEBASE`, `VORAGO_PROBE_REF` and `VORAGO_PROBE_GR` (`integration/ecosystem_rule_probe_test.cpp`).

### 1.6 Tests that encode route constants

These were read this session. They decide which mechanisms are free.

| Test (file:line) | What it asserts | Mechanism that breaks it |
|---|---|---|
| `VoragoVoice_EcosystemLeverMapping` (`vorago_ecosystem_lever_test.cpp:989`), via `checkWakes` (`:374-386`, call `:1021`) | loop wake `== combineWake(loopWakeBase(v,l), raw eco, sched)` | an eco-term gain on the loop wake (W2). **Base changes do not**: the test reads the base through `Probe::loopWakeBase` |
| `VoragoVoice_EcosystemLaneShapingFidelity` (`:1276`) | mutation and bloom depth `== clamp(base + raw)`, hard-coded (`:1334-1337`); also `checkWakes` (`:1332`) | **any** `kPartialLaneGain ≠ 1`, a split, or W2 |
| `VoragoVoice_RouteLeverZeroAtZeroLane` (`:1360`) | mutation and bloom depth `== clamp(base + Probe::partialLaneGain() * raw)` (`:1386-1389`), where `partialLaneGain()` returns `VoragoVoice::kPartialLaneGain` (`:137`) | a **compiled** split (two gains). A single gain change does not, and nor do seams installed from one constant |
| `expectedLevers` (`:296-319`), used by Mapping and LaneShapingFidelity | loop gain and coupling from `Probe::loopGainLeverSpan()` / `couplingLeverSpan()` / `ringCouplingBase()` (`:103`, `:116`), i.e. by name | nothing, as long as the constants keep their names and the members equal them |
| `vorago_voice_longrun_test.cpp:181` | `kShippedLoopWakeBase = VoragoVoice::kLoopWakeBase`, by name | nothing |

### 1.7 Showcase and host preset facts (`tools/vorago_preset_defs.h`)

- **Bloom Colony** (`:1040-1061`): 900 = 1.0; bloom depth 0.50 ("room above for the colony"); spawn 0.742386 →
  0.01 Hz; mutation 0.85 ("near its clamp"); events 0.5×.
- **Feeding Loops** (`:1139-1162`): 900 = 1.0; ecology mix 0.90; loop gain 0.60 → 0.54; bloom depth 0.10; events
  0.389076 → 0.6×.
- **Colony Pulse** (`:562-582`): secondaries D13.2, E6.hi and E7.hi; 901 = 1.0, 902 = 1.0; events 4.0×; Life 0.45.
  E6.hi's twin resets 901 → 0.0, and E7.hi's resets 902 → 0.25 (`:187-190`).
- **The macro showcases:** Erosion (`:711-735`); Drifting Strata (`:771-795`, every Movement member stored at its
  minimum); Stone Gravity (`:800-821`: Gravity 1.0, sub −6 dB, the limiter note); Teeming (`:944-962`).
- **What 13c already measured** (13c `compliance.md:49-60`; `artifacts/rulings.md:6`, `:12`):
  - E1: Partial gain 1.5 / 2.0 / 3.0 → 1.1989 each. The spawn / child-gain extension (`kPartialSpawnSpanHz` /
    `kPartialChildGainSpan`) read E1 0.93–1.14 and pulled S6 down to 1.27–2.50, so it was declined (B-11);
  - E4: loop-gain span 0.24 / 0.30 → 1.77; coupling 0.38 / 0.44 → 1.77;
  - M2: tilt −7 → 2.79; damping 0.70 → 2.79; Age → mutation → 2.83;
  - M4: three rungs at 0.85–0.87, each with 12 macro-test reds;
  - M5: r1 3.14, r2 3.19;
  - M10: r1 0.91, r2 0.82.

  **No 13d rung repeats one of these.** FR-016 states this rule for M2; the plan applies it everywhere, so no ladder
  re-measures a known number.

---

## 2. Instruments (FR-003, FR-004, FR-010, FR-019)

Every instrument is hidden (`[.probe]`, or an env option of a hidden case). None is a per-push gate. Each one is
written, built and run on the **unmodified** lever constants before the first rung (§8, step I).

### 2.1 Loop-bus meter (FR-004, FR-021b, SC-004): voice → engine → harness → probe

**Signal.** For each sample:

  loopBus(t) = Σ over voices v, Σ over loops l, of tap_{v,l}(t), where tap_{v,l}(t) = `prevOut_[l]` of voice v's
  `FeedbackEcology` (`feedback_ecology.h:2251`).

The gated figure is the RMS of this signal over every 10 s window in `[A, H]`, read against −60 dBFS. These are arm
2's windows, built with `tenSecondWindows(tl.A, tl.H, sr)` exactly as `evaluateArms` builds them.

**Voice** (`vorago_voice.h`). The meter is test-only, never on by default, and adds no allocation:

```cpp
// public
void setLoopBusMeterEnabled(bool on) noexcept { loopBusMeter_ = on; }
[[nodiscard]] bool isLoopBusMeterEnabled() const noexcept { return loopBusMeter_; }
/// Overload: identical to processStereoBlock(outL, outR, n), and additionally
/// serves the per-sample loop-bus sum into loopBusOut (nullable) on the SAME
/// carry clock as the audio. With the meter off, writes zeros.
void processStereoBlock(float* outL, float* outR, float* loopBusOut, std::size_t n) noexcept;

// private state (fixed size; sized by the existing chunk constant)
bool loopBusMeter_ = false;
std::array<std::array<float, kControlChunkSamples>, FeedbackEcology::kMaxLoops> loopTap_{};
std::array<float, kControlChunkSamples> carryLoopBus_{};
```

- **`renderOneChunk()` step 7.**
  - With the meter on, it calls `ecology_.processBlockTapped(excL_.data(), excR_.data(), excL_.data(),
    excR_.data(), tapPtrs, n)`, where `tapPtrs[l] = loopTap_[l].data()` for `l < ecology_.getNumLoops()` and
    `nullptr` above it.
  - It then fills `carryLoopBus_[s] = Σ_l loopTap_[l][s]`.
  - With the meter off it calls `processBlock` as today.
  - The tap is write-only (FR-073, `feedback_ecology.h:976-978`), and `processBlock` *is*
    `processBlockTapped(..., nullptr, ...)` (`:949-952`). So the audio path is the same function in both states (§6.1,
    T-M2).
- **Serving.** The existing three-argument `processStereoBlock` becomes a forward with `loopBusOut = nullptr`. The
  four-argument form copies `carryLoopBus_` alongside `carryL_` / `carryR_` with the same `carryRead_` / `take`
  arithmetic (`:915-928`).
- **Reset.** `reset()` and the clearing path zero `carryLoopBus_`. The meter flag survives `reset()`, but `prepare()`
  sets it to false.

**Engine** (`vorago_engine.h`):

```cpp
void setLoopBusMeterEnabled(bool on) noexcept;          // fans out to all kMaxVoices
[[nodiscard]] bool isLoopBusMeterEnabled() const noexcept;  // slot 0
/// Sum over every sample processed since the last call of loopBus(t)^2; resets to 0.
[[nodiscard]] double takeLoopBusSumSq() noexcept;
// private
std::array<float, kControlChunkSamples> vLoopBus_{};    // one voice's served slice
std::array<float, kControlChunkSamples> loopBusAcc_{};  // the voices' sum
double loopBusSumSq_ = 0.0;
```

- In the voice loop (`:1205-1226`), when the meter is on:
  - `loopBusAcc_` is zeroed per slice;
  - each rendering voice is called through the four-argument overload with `vLoopBus_.data()`;
  - `loopBusAcc_[s] += vLoopBus_[s]` is added **inside the existing per-sample accumulation loop** (`:1211-1226`),
    after the FR-072 check and beside `busL_[s] += a; busR_[s] += b;` (`:1224-1225`);
  - after the voice loop, `loopBusSumSq_ += Σ_s (double)loopBusAcc_[s]²`.
- **A poisoned slice is mirrored, not zeroed.** The FR-072 guard `break`s mid-slice (`:1220-1223`). Samples before the
  offending index are already in `busL_` / `busR_`; the rest of that voice's slice contributes nothing. Because the
  meter's add sits in the same loop after the same check, the meter covers exactly the samples the audio bus kept:
  the partial prefix, not 0 and not the whole slice.
- With the meter off, the original three-argument call runs unchanged.
- `slice ≤ kControlChunkSamples` holds by construction (`:1194`).

**Harness** (`preset_test_support.h`):

- `RenderSpec` gains `bool loopBusMeter = false;`.
- `SweepCapture` gains `std::vector<double> loopBusPower;`, one sum of squares per 512-sample block, reserved like
  `blockPowerL`.
- `renderPreset`, when `spec.loopBusMeter` is set:
  1. calls `host.engineForTweak()->setLoopBusMeterEnabled(true)` after `loadState`, next to the tweak;
  2. after every `host.process` block, pushes `engine->takeLoopBusSumSq()`;
  3. sets `tweakHeld = false` if `isLoopBusMeterEnabled()` reads false after block 0, the same contract as the lever
     getters.
- New helper `[[nodiscard]] inline double loopBusWindowDb(const SweepCapture& cap, const SweepWindow& w)`. It is
  `spanPowerDb`'s arithmetic over the mono `loopBusPower`, with the same `kPowerFloor`.

**Probe** (`preset_pilot_test.cpp`), env `VORAGO_PILOT_LOOPBUS=1`:

- It sets `loopBusMeter` on every **take** render: the stored take and, under `VORAGO_PILOT_TAKES=4`, all four. Twins
  and route arms do not carry it, because it is not part of their scoring.
- Per take it prints one line:

  `  loopbus take j seed s: worst10s %.2f dBFS [PASS|NO] windows n` — the minimum `loopBusWindowDb` over arm 2's
  windows, against `kSilenceDb`.

- It adds a per-loop life line from the last block of each take, read through `getVoice(0).ecology()`:
  `getLoopWakeAmount`, `getLoopGate`, `isLoopDormant` and `getLoopGain` per loop. This answers FR-014's question —
  whether the sleep edge fires or the loops simply fade — without guessing.
- The verdict line gains `loopsAlive y|n`, the AND over all printed takes. The FR-021b conjunct is that field.

### 2.2 Bloom read-out (E1)

Env `VORAGO_PILOT_BLOOM=1`. It needs no production change: it reads through `getVoice(0).bloom()` in a per-block
observer. Per take, at the end of `[A, H]`, it prints:

- the bloom's spawn, executed and discarded event counts, and its placed / refused child counts (the existing
  counters at `bloom_engine.h:728` and following);
- the time-mean of `getLiveChildCount()` (`:717`) and of `getSmoothedDepth()` (`:688`) over `[A, H]`.

**Observer plumbing.** `RenderSpec` gains `std::function<void(const Krate::DSP::VoragoEngine&, long long
startSample)> blockObserver{};`. `renderPreset` calls it after each block through `host.engineForTest()` (const). It
is empty by default, so it changes nothing. The probe's lambda writes into a per-job struct, never into Catch2 (jobs
run on worker threads, `preset_pilot_test.cpp:941`).

It is printed for R_k and R_k0 of E1, the two renders the route gate compares. The line answers one question: is E1
**spawn-limited** (few events, so children are rare in both arms) or **level-limited** (children exist in both arms
at similar depth)? The E1 ladder (§3.1) starts on whichever dimension it names.

### 2.3 Eco-lane read-out (E6.hi / E7.hi, FR-015)

**Voice.** One read-only member, written once per control step in `publishIdentity()` after the gather. Each entry is
the mean of `lanes.eco[k][slot]` over that kind's addressed slots (`slotCountForKind(k)`):

```cpp
std::array<float, EcosystemEngine::kNumKinds> ecoLaneMean_{};
[[nodiscard]] float getEcoLaneMean(EcosystemEngine::Kind k) const noexcept;  // 0 for out of range
```

It costs at most 5 × 12 float adds per 64-sample chunk.

**Probe.** Env `VORAGO_PILOT_LANES=1` installs a `blockObserver` that samples `getEcoLaneMean` for all five kinds
every block over `[A, H]`. It renders P and the ExtReversion twin of each named secondary (E6.hi: 901 → 0.0; E7.hi:
902 → 0.25) on the stored take. Per kind it prints mean, p10, p90, and the mean absolute block-to-block change.

The **ranking** row orders the kinds by `|Δmean| + |Δ(p90−p10)|` between P and twin. Its first one or two kinds are
"the lanes that carry the difference" (FR-015).

**Rule-probe read-out (13b's Life-max surface).** `VORAGO_PILOT_LANES` is a pilot option. It does not reach
`Vorago_EcosystemRuleProbe` (`integration/ecosystem_rule_probe_test.cpp:544`), which renders through its own
`renderOnce` (`:360`) and reads its own options through the TU's `readEnv` (`:211`). The rule probe gets its own
option:

- **Env:** `VORAGO_PROBE_LANES=1`.
- **Sampling.** `RenderResult` (`:327-338`) gains `std::array<LaneStats, EcosystemEngine::kNumKinds> lanes{}`.
  `LaneStats` is a TU-local struct in the TU's anonymous namespace (`:176`) holding mean, p10, p90 and the mean
  absolute block-to-block change. `renderOnce`'s block loop already reads `colonyEngine` (const, `:417`) after every
  block for FR-005's colony mean. With the option on it also samples `colonyEngine.getVoice(0).getEcoLaneMean(k)` for
  all five kinds over the M1–M3 capture, into vectors reserved before the loop (test code, not the audio path).
- **Print site.** One line per render, `LANES <label> <kind>=mean/p10/p90/dabs …`. It is printed after the default
  render's `DESCRIPTOR` line, and after each knob's table row (`:974-980`) for that knob's `.lo` / `.hi` extremes
  (rendered at `:857-860`). The `syncRate` (`:310`) and `selfAffinity` (`:321`) rows' `LANES` lines are the ones
  FR-015 records.
- With the option off nothing is sampled or printed, so every GATE1 / GATE2 line is unchanged.

### 2.4 Life-row read-back (M10, FR-019)

Env `VORAGO_PILOT_READBACK=1` on Teeming (and on Colony Pulse for E6.hi / E7.hi). It renders block 0 at three Life
values: the stored 0.70, the ablation reset 0.0, and 1.0. After block 0 it prints:

- the destination values: `getEcosystemDepth()`, `getEventRateScale()` and `getBloomSpawnRateHz()` on voice 0;
- the **unclamped row sum**, computed in the probe from `VoragoMacroMatrix::kRows` (public) as base override plus
  `amount * applyModCurve(curve, life)` (`modulation_curves.h`).

  The base override is the stored plain value of each MB route: 900 for `EcosystemDepth`, 800 for `EventRateScale`,
  1301 for `BloomSpawnRateHz` (`param_routes.h:199-200`, `:213`). It is converted by the same helper the processor
  uses, `Vorago::normalizedToPlain…` — the exact function is read from `param_routes.h` when the probe is written.

A row whose destination value equals its setter clamp while the unclamped sum exceeds it is printed `CLAMPED`. That
line is FR-019's "measured value decides".

### 2.5 Macro pre-screen: reuse the Phase 12 row-emulation probe

`vorago_macro_retune_probe_test.cpp` already emulates a candidate row as `setTargetBase(t, base(t) + amount *
applyModCurve(curve, x))` on the SC-008 fixture: seeds {101, 202, 303}, five points, 60 s, the same metrics and
`spearmanRho` (header `:1-55`; the emulation is exact because `evaluateAll` seeds from the override, `:24-33`).

13d adds a third hidden case to that TU:

- **Name:** `TEST_CASE("VoragoMacro_Phase13dRowProbe", "[.probe][vorago]")`.
- **Input:** `VORAGO_ROWPROBE="<macro>:<target>:<amount>:<curve>[;...]"`, a list of delta rows on the existing targets.
  A negative delta equal to a shipped amount **cancels** that member; this is M4's member-isolation instrument.
- **Output:** for every SweepAxes row the candidate touches, rho, endpoint and the per-seed five-point series (the
  shipped SweepAxes prints only the mean, `vorago_macro_test.cpp:1281-1289`), against the unchanged thresholds.
- **Use:** it is a **pre-screen only**. A rung that fails it is not built; a rung that passes is still confirmed by
  the real `VoragoMacro_SweepAxes` / `VoragoMacro_NoZipper` on a rebuilt binary (FR-018).
- **Scope:** it emulates `kRows` deltas only. A rung that changes a compiled voice constant rather than a row (M4 r2,
  `kWanderLeverRateCompExponent`) cannot be represented, so it is exempt; its only admissibility read is the rebuilt
  `VoragoMacro_SweepAxes` + `VoragoMacro_NoZipper` (§3.6).

---

## 3. Lever design, cell by cell (FR-010b order)

Two notes apply to every cell:

- **Per-rung read set (FR-010c).** For every rung, the build runs the read set below and logs it under
  `artifacts/<cell>_r<n>_*.log`:
  - (a) every earlier roster cell, read on **its own gate surface**: the rung in place, plus that cell's recorded gate
    override string from the lever table (FR-025). Reading a companion-gated cell as compiled would read the wrong
    surface (for example Feeding Loops at 0.6× events instead of its 1.0× gate), and then FR-010b's re-open trigger
    could never fire. The gate surfaces are:
    - E1: Bloom Colony with its adopted `1300=…` companion (§3.1);
    - E4: Feeding Loops at `800=0.5` (FR-014), with `VORAGO_PILOT_TAKES=4` and `VORAGO_PILOT_LOOPBUS=1`, so the
      loops-alive conjunct is re-read too;
    - E6.hi / E7.hi: Colony Pulse at `109=<L>`, the L ruled in §3.3 (FR-015), with `VORAGO_PILOT_SECONDARY=1
      VORAGO_PILOT_CELLS=S7`;
    - M10: Teeming with its `900=0.15` companion, once r3 (or an r4 containing it) is adopted;
    - M2 / M4 / M5: their showcases, with no override.

    This set is run by `VORAGO_PILOT_ITERATE=roster` (§4.5), which applies each ruled cell's stored override string;
  - (b) every sweep-5-verified primary and secondary on every preset where the lever is **live**, as compiled (no
    override), run by `VORAGO_PILOT_ITERATE=liveFor:<lever-id>` (§4.5).

  Liveness is decided by the spec's three rules, from the preset's stored values in `vorago_preset_defs.h`. The list
  per lever is generated by a probe option (§4.5) rather than typed by hand.
- **Adoption (FR-010d).** The adopted rung is the smallest-change rung that clears:
  - its gate;
  - the read set;
  - the four arms, with arm 1 also at 44.1 kHz;
  - the −6 dB re-read.

  "Smallest change" is ordered by mechanism count first, then by `|rung − compiled|` normalised by the rung's own
  ladder span.

### 3.1 E1 Partial → bloom (FR-013, FR-021; Bloom Colony)

**Mechanisms.**

| ID | Mechanism | Where | Ladder route |
|---|---|---|---|
| E1-G | bloom-route gain: `bloom_.setDepth(std::clamp(bloomDepthBase_ + partialBloomLaneGain_ * partialEco, 0, 1))` | `vorago_voice.h:2233` | seam `partialBloomGain` |
| E1-M | mutation-route gain (diagnostic first): `cloud_.setMutation(std::clamp(mutationBase_ + partialMutationLaneGain_ * partialEco, 0, 1))` | `:2232` | seam `partialMutationGain` |
| E1-K | attachment: parent count K, `bloom_.setParentCount(kBloomParentCount)` installed at prepare | new prepare line beside `:658` | seam `parentCount` |
| E1-C | attachment: children per event, `bloom_.setChildrenPerEvent(kBloomChildrenPerEvent)` installed at prepare | same | seam `childrenPerEvent` |
| E1-D | **companion** (FR-025b): Bloom Colony's stored bloom depth 1300 | preset defs, via the override | `VORAGO_PILOT_OVERRIDE="1300=…"` |

**How the members are installed.**

- Both gain members are installed from the **one** compiled `kPartialLaneGain` (1.0). So with no tweak the two writes
  are the shipped expressions value for value, and `RouteLeverZeroAtZeroLane` and `LaneShapingFidelity` stay green
  (§1.6).
- `kBloomParentCount = BloomEngine::kDefaultParentCount` and `kBloomChildrenPerEvent =
  BloomEngine::kDefaultChildrenPerEvent` are the component defaults restated. Installing them changes nothing.

**Premise reads (I-E1), before any rung.**

1. The §2.2 bloom line on R_k and R_k0, which shows spawn-limited or level-limited.
2. `attribBase` with `partialMutationGain=0`. This measures how much of E1's 1.10 residue is the mutation half of the
   same lane (E-4). It is a diagnostic, and never adopted on its own.
3. **The companion alone on the base tree** (FR-025b): `1300=0.0` and `1300=0.2`, with no lever. If either clears the
   gate, E1 becomes a stop-and-surface item (FR-027).

**Rungs.** FR-013 measures both ways of the route gain: one gain on both destinations, and a split. So the ladder has
two gain arms, and **both are always run**. Every rung differs from 13c's unsplit, companion-free pg 1.5 / 2.0 / 3.0
(§1.7) by its companion. Lever strings use the probe's comma list (`preset_pilot_test.cpp:875`, parsed at `:892-912`).

*Arm A — single gain, laddered first.* Both seams are set to the same g. A clearing rung ships as a new
`kPartialLaneGain` value, so `RouteLeverZeroAtZeroLane` stays unedited and only `LaneShapingFidelity`'s hard-coded
`+ raw` changes (FR-034 entry 1, §6.4). An arm-A rung is therefore **FR-010d-adoptable** without a user ruling.

| Rung | Lever | Override | Why |
|---|---|---|---|
| a1 | `partialBloomGain=2.0,partialMutationGain=2.0` | `1300=0.2` | the route owns most of the bloom depth; R_k0 sits at 0.2 |
| a2 | `partialBloomGain=2.0,partialMutationGain=2.0` | `1300=0.0` | R_k0 has no bloom at all: the spawn gate is 0 (`bloom_engine.h:960`) |
| a3 | `partialBloomGain=3.0,partialMutationGain=3.0` | `1300=0.2` | — |
| a4 | `partialBloomGain=3.0,partialMutationGain=3.0` | `1300=0.0` | — |
| a5 | `partialBloomGain=4.0,partialMutationGain=4.0` | `1300=0.2` | — |
| a6 | `partialBloomGain=4.0,partialMutationGain=4.0` | `1300=0.0` | — |

*Arm B — split, always measured.* Mutation stays at its installed 1.0 unless the rung names it. A clearing arm-B rung
needs two compiled constants and edits `RouteLeverZeroAtZeroLane`, so it is never FR-010d-adopted (§3.1 Shipping).

| Rung | Lever | Override | Why |
|---|---|---|---|
| b1 | `partialBloomGain=2.0` | `1300=0.2` | bloom half only |
| b2 | `partialBloomGain=3.0` | `1300=0.2` | — |
| b3 | `partialBloomGain=4.0` | `1300=0.2` | — |
| b4 | b-best (with the better of `1300=0.2` / `0.0`) + `partialMutationGain=0.5`, `=0.25`, `=0` | same | three rungs down to the clamp 0; isolates the mutation half when attributability (d ≥ attribBase + 1.5) is the failing clause |

- **Which way is offered.** If any arm-A rung clears E1 under FR-010d, the smallest one is adopted, and arm B is
  logged in `rulings.md` as the measured alternative; nothing from arm B ships. The split is **presented to the user**
  (§11 item 2) only if **no arm-A rung** clears E1 under FR-010d and an arm-B rung does.

*Attachment arm (on top of the best gain rung: arm A's, or arm B's if arm A has none).*

| Rung | Lever | Why |
|---|---|---|
| c1 | + `parentCount=3`, `=2`, `=1` | children concentrate on the loudest parents; three rungs from the default 4 down to the clamp 1 (`bloom_engine.h:556-558`). **User ruling if adopted** (FR-013) |
| c2 | + `childrenPerEvent=3`, `=4` | more children per spawn event. Only two values exist between the default 2 and the clamp `kMaxChildrenPerEvent = 4` (`:560-563`), so FR-010's "every rung up to the clamp" is met with two. **User ruling if adopted** |

- If I-E1 reads spawn-limited, the attachment arm runs before b4. The order is logged with the I-E1 line that decided
  it.
- `childGain` stays at the 13c ruled 1.5 (B-4). It is re-laddered only through an FR-010b re-open.

**Read set (FR-010c b).** E1 has two lever ids in the §4.5 generator, because its mechanisms are live on different
presets:

- **`e1route`** (E1-G / E1-M; every arm-A and arm-B rung): live wherever the stored 900 > 0 and the preset makes the
  bloom (or, for E1-M, the cloud) sound.
- **`e1attach`** (E1-K / E1-C; every c1 / c2 rung): live wherever the bloom sounds, i.e. stored bloom depth
  1300 > 0, **regardless of 900**. `parentCount` and `childrenPerEvent` are compiled bloom constants, not route
  terms. `tools/vorago_preset_defs.h` has presets that store `{kEcosystemDepthId, 0.0}` (`:1320`, `:1352`, `:1383`,
  `:1414`, `:1445`, `:1476`, `:1507`, `:1539`, `:1570`, `:1617`, `:1648`, `:1681`, `:1712`), and some of them store a
  non-zero bloom depth (`kBloomDepthId` 0.35 at `:1375`, 0.25 at `:1467`, 0.10 at `:1569`). Those presets are in
  `e1attach`'s set and not in `e1route`'s.
- **S6 Slow Bloom** is in every E1 read set. 13c's spawn extension sank it (B-11). Under FR-030 a sinking rung may
  still be adopted, and S6 then joins the full re-author set.

**Shipping the adopted rung.**

- A single-gain outcome (E1-M equal to E1-G) is shipped by changing `kPartialLaneGain`.
  - That breaks `LaneShapingFidelity`'s hard-coded `+ raw` (`:1334-1337`). It is pre-listed under FR-034 (§6.4,
    entry 1).
  - `RouteLeverZeroAtZeroLane` stays green unedited.
- A **split** outcome (E1-M different from E1-G) needs two compiled constants, `kPartialMutationLaneGain` and
  `kPartialBloomLaneGain`.
  - `RouteLeverZeroAtZeroLane` asserts both destinations with one gain (`:1386-1389`), and SC-013 names it
    "unedited". So a split is a **user ruling** (§11), never an FR-010d adoption, and it is presented only when no
    arm-A rung clears.
- K and children per event ship as the compiled `kBloomParentCount` / `kBloomChildrenPerEvent` values.
- The companion `1300` is transcribed into Bloom Colony by the confirming pass, with a comment.

### 3.2 E4 Feedback → loop wake (FR-014, FR-021, FR-021b; Feeding Loops at 1.0× events)

**Mechanisms.**

| ID | Mechanism | Where | Ladder route |
|---|---|---|---|
| W1 (primary) | loop-wake route span by its base: `loopWakeBase_[l]` in `combineWake(loopWakeBase_[l], eco, sched)` | prepare `:654`; write `:2224-2228` | seam `loopWakeBase` (clamped `[kMinRetunedWakeBase, 1]`) |
| W2 (reserve) | loop-wake eco gain: `combineWake(loopWakeBase_[l], std::min(1.0f, kLoopWakeLaneGain * eco), sched)` | `:2227` | rebuild only |
| F-G (companion) | `loopGainLeverSpan_` (installed from `kLoopGainLeverSpan`) | `:2275` | seam `loopGainSpan` |
| F-C (companion) | `couplingLeverSpan_` (installed from `kCouplingLeverSpan`) | `:2279` | seam `couplingSpan` |

- **Why W1 is "the loop-wake route gain".** The route's audible span is `1 − base`: at depth 0 the wake reads `base`,
  and at depth > 0 it reads `max(base, eco, sched)` (`:1091-1094`). Lowering the base from 0.45 widens the span the
  colony moves.
- **W1 keeps every 13b test green.** `checkWakes` reads the base through the probe (§1.6), and E-8's static_assert is
  over the *compiled* `kLoopWakeBase`, which only changes if W1 is adopted (and then stays ≥ 0.05).
- **W2 breaks `checkWakes`.** That breaks `VoragoVoice_EcosystemLeverMapping`, which FR-032 names as unedited. So W2 is
  measured only if W1 and its companions fail, and is adoptable only by a user ruling (§11).

**Premise reads (I-E4).** These run at `VORAGO_PILOT_OVERRIDE="800=0.5" VORAGO_PILOT_TAKES=4
VORAGO_PILOT_LOOPBUS=1`, and again at the stored 0.6×:

- the loop-bus worst 10 s per take, the per-loop life line, and arms 2–3 per take;
- the roadmap premise is 3.71, with arm 2 red on every take. A reading outside `max(0.01, 0.005·d)` is
  stop-and-surface (FR-003).
- **The sleep edge, by code-read.** It cannot fire from the identity path: `gateSteady` returns 0 only for
  `wakeAmount ≤ 1e-6`, dormant, or out-of-count (`feedback_ecology.h:2304-2309`), while the combine never writes
  below `kLoopWakeBase = 0.45` and the voice never calls `setLoopDormant` (grep this session: no call in
  `vorago_voice.h`). The per-loop line (`isLoopDormant`, `getLoopGate`) confirms or refutes this. If it refutes it,
  that is a stop-and-surface item before any rung: the plan's model of the loops is wrong.

**Rungs**, all at `800=0.5`:

| Rung | Lever |
|---|---|
| r1 | `loopWakeBase=0.30` |
| r2 | `loopWakeBase=0.15` |
| r3 | `loopWakeBase=0.05` (the floor, E-8) |
| r4 | r-best + `loopGainSpan=0.22`, `=0.26`, `=0.30` (three rungs from the compiled 0.18). Sustain: raises the loop's own feedback where the colony feeds it, which keeps the loop bus up. It differs from 13c's unpaired 0.24 / 0.30 by being paired with a base |
| r5 | r-best (with r4's best if it helped) + `couplingSpan=0.34`, `=0.40`, `=0.44`. 0.44 is the clamp: `kRingCouplingBase` 0.06 + 0.44 · 1 = `kMaxCouplingPerPair` 0.5 (`feedback_ecology.h:269`), so a larger span is inert at full lane. It differs from 13c's unpaired 0.38 / 0.44 by being paired with a base |
| r6 | (only if r1–r5 fail, user-gated) W2 rebuild `kLoopWakeLaneGain ∈ {1.5, 2.0, 2.5}` at the compiled base |

- **The tension.** A lower base widens the contrast but can starve the loop bus when the colony's lane is low. The
  gate's loops-alive conjunct (FR-021b) reads exactly this. So r4 / r5 exist to keep the loops fed, not to add
  distance.
- **Read set.** W1, F-G and F-C are live on every preset with ecology mix > 0, because the wake base applies with the
  route depth at 0 too: S4 Feedback Mire, and every preset whose ecology is audible. FR-010c (b) runs on all of them.
- **Shipping.** W1 ships as a new `kLoopWakeBase` value. `kPeakWakeBase` is unchanged. Both stay public and keep their
  static_assert.

### 3.3 E6.hi / E7.hi (FR-015, FR-022; Colony Pulse with Life raised)

**Premise (I-E67).**

- §2.3 on Colony Pulse with `VORAGO_PILOT_OVERRIDE="109=<L>"` for L ∈ {0.45 (stored), 0.70, 0.90, 1.00}, and on 13b's
  Life-max surface through `Vorago_EcosystemRuleProbe` with `VORAGO_PROBE_LANES=1` (§2.3).
- The ranking row names the carrying kinds. This premise ladder is laddered by override alone: Life is the cell's
  named FR-025 exception.
- Each L is also read for the gate with `VORAGO_PILOT_SECONDARY=1 VORAGO_PILOT_CELLS=S7`, which gives E6.hi, E7.hi,
  D13.2 and S7.

**Candidate surfaces.** They are chosen by the ranking and are all rebuild rungs, because each is a compiled constant
the 13b tests read by name:

| Ranked kind | Rung family | Constraint |
|---|---|---|
| Resonator / Noise / Feedback | `kLeverInputGain[k]` 2 → 2.25 / 2.5 / 2.75. The ladder stops below 3, which 13b measured as a regression (`vorago_voice.h:1736-1741`) | the Partial / Ghost static_assert stays (`:1744-1747`) |
| Resonator | `kPeakLevelLeverSpanDb` (`:1680`) 18 → 19 / 20 / 21 (clamped downstream by `kMaxPeakLevelDb = 12`, `resonance_drift_network.h:237`) | NoZipper per rung (E-7) |
| Noise | `kNoiseLevelLeverSpanDb` (`:1679`) 12 → 13 / 14 / 15 | arm 1 on every noisy preset in the read set |
| Partial | `kPartialLaneGain` (E1's lever, already ruled) | a change re-opens E1 (FR-010b) |
| Ghost | `kGhostLaneGain` 1 → 1.25 / 1.5 / 2.0 | S9 / E5 in the read set |

- **Ladder per surface:** the three magnitude rungs above, in increasing distance from the compiled value.
- **Each rung is read in both FR-015 places:**
  - on Colony Pulse at the best Life value from I-E67 (the gate), with `VORAGO_PILOT_SECONDARY=1
    VORAGO_PILOT_CELLS=S7` → `artifacts/e67_r<n>_gate.log`;
  - on 13b's Life-max surface: `Vorago_EcosystemRuleProbe` with `VORAGO_PROBE_SURFACE` set to the same Life-max
    string step B used for `gate2_table_*_base.log`, and `VORAGO_PROBE_LANES=1` → `artifacts/e67_r<n>_lifemax.log`.
    The `syncRate` and `selfAffinity` table rows and their `LANES` lines are copied into `rulings.md` with the rung.
- **The append-only reserve.** If the ranking shows the knob's difference lives in agent *energy* but no lane moves (a
  flat ranking), the reserve is an append-only `EcosystemEngine` read-out. Its rules stay untouched (FR-011, FR-015).
  This plan does not specify it further: that outcome is a stop-and-surface ruling with the I-E67 table, because no
  voice-side surface would then exist.
- **Shipping.** The Life value goes into Colony Pulse in the confirming pass. S7 and D13.2 must re-verify with it, and
  a failure is FR-027.

### 3.4 M2 Age (FR-016, FR-020; Erosion)

All rungs are rebuild rungs. Each is pre-screened with §2.5 on the Age SweepAxes row, which must keep HF > 4 kHz down
≥ 3 dB, and on the folded `Decay` clauses (`vorago_macro_test.cpp:1324` ff.: decay shortens, damping rises). The 13c
rungs (tilt −7, damping 0.70, an Age → mutation row) are the floor, and none is repeated.

Every candidate is a three-rung magnitude ladder (FR-010), ordered by distance from the compiled amount, so FR-010d's
smallest-change rung can be shown (SC-026).

| Rung | Change (`vorago_macro_matrix.h`) | Kind |
|---|---|---|
| r1 | Age → BodyDamping (`:342-347`) amount 0.55 → 0.75 / 0.85 / 0.95. 13c's 0.70 is not repeated. 0.95 takes Erosion's stored 0.05 to 1.00 at Age 1, which is the damping clamp `[kMinDamping 0, kMaxDamping 1]` (`continuous_body.h:146-147`, applied at `:1454`) | widen |
| r2 | Age → CloudSpectralTiltDb (`:354-359`) amount −4 → −8 / −9 / −10 (13c's −7 is not repeated). **Clamp headroom:** see below | widen |
| r3 | new row Age → CavernDarkness, base 0.80 (the target's existing base, `:325-330`), amount +0.05 / +0.10 / +0.20, Linear. +0.20 reaches the darkness ceiling 1.0 at Age 1 from the base alone | re-aim of the HF-loss half into the space |
| r4 | new row Age → CloudMutation, base 0.15, amount +0.30 / +0.50 / +0.70, Linear. 13c's amount is read from `l7_M2_ladder_summary.txt` first; a rung that equals it is replaced by the midpoint to its neighbour | add mutation |
| r5 | Age → CavernDecaySeconds (`:348-353`) amount −14 → −16 / −18 / −20 (Erosion's 30 s → 14 / 12 / 10 s at Age 1). The cavern's decay clamp is read before the rung is written, and a rung past it is dropped as inert | widen the `Decay` half |
| r6 | the two best singles combined, each at its adopted magnitude | two mechanisms |

**r2's clamp headroom.** `HarmonicCloud::setSpectralTiltDb` clamps to `[kMinTiltDbPerOct −12, kMaxTiltDbPerOct +12]`
(`harmonic_cloud.h:194-195`, applied at `:443`). The target's shared base is −4 (`vorago_macro_matrix.h:322`, `:357`),
and Darkness also writes it with amount −6 (`:319-324`). With Age as the only contributor, the row sums to
`−4 + amount · Age`:

- −8 reaches −12 exactly at Age 1, so the whole Age range is live;
- −9 and −10 reach −12 at Age 0.89 and 0.80; above that the top of the macro range is clamped and inert.

Before each rung the build pre-computes the **unclamped** sum of every row on the target, as §2.4 does for Life rows:
at the SweepAxes fixture's five Age points (with its other macros as the fixture sets them) and at Erosion's stored
macros. A point at or past the clamp is printed `CLAMPED`. The Age SweepAxes row's endpoint and monotonicity are read
against that clamped curve: a flat top counts as it measures, and is never excused. −8 is laddered first.

**Added rows (r3, r4).** A new row bumps `kNumRows` (`:276`) and its per-macro tally comment (`:272-275`) in the
same edit; the `kRows.size() == kNumRows` static_assert (`:1223-1224`) catches a forgotten bump at build time. The
row joins the Age block, so the owner-block and one-base-per-target guards hold (§1.3).

### 3.5 M5 Gravity (FR-017, FR-020; Stone Gravity)

A new target must be bipolar through `contributionOf`. Any row on `VoragoMacro::Gravity` is (`:1171-1175`). Its target
must also have headroom on **both** sides of its base, because the octave-lock row's air half is clamped inert
(`:445-449`). Candidates on existing targets:

Each candidate target is a three-rung magnitude ladder (FR-010):

| Rung | New row (Gravity, Linear) | Air (m = 0) / stone (m = 1) contribution |
|---|---|---|
| r1 | → CloudSpectralTiltDb, base −4, amount −3 / −6 / −8 | +3 / −3, +6 / −6, +8 / −8 dB/oct. −8 takes the stone side to −12, the tilt clamp (`harmonic_cloud.h:194-195`), with Gravity as the only contributor |
| r2 | → SubToneLevelOffsetDb, base 0, amount +2 / +4 / +6 | −2 / +2, −4 / +4, −6 / +6 dB |
| r3 | → CavernDarkness, base 0.80, amount +0.10 / +0.15 / +0.20 | 0.70 / 0.90, 0.65 / 0.95, 0.60 / 1.00. +0.20 reaches the darkness ceiling 1.0 on the stone side |

- **Clamp headroom.** As in §3.4, the unclamped sum of every row on the target is pre-computed per rung (Darkness's
  tilt row `:319-324` and its SCurve darkness row `:325-330` land on the same targets), at Gravity 0 and 1 and at
  Stone Gravity's stored macros. A side that clamps is printed `CLAMPED`, and its arm is read as measured.
- **Added row.** Every rung adds one `kRows` row, so it bumps `kNumRows` (`:276`) and the tally comment
  (`:272-275`); the `:1223-1224` static_assert is the guard (§3.4).

- Arms 1 and 3 are printed on all four takes and at 44.1 kHz for every rung. A rung that re-opens either arm is not
  adopted (FR-017). r2 raises the sub that Stone Gravity already trims for the limiter
  (`tools/vorago_preset_defs.h:818`), so it is laddered last.
- The SweepAxes Gravity row (octave offset, `vorago_macro_test.cpp:952-957`) is unaffected by construction: no
  candidate touches a ratio. It is still read per rung.
- SC-014's Gravity-0 arm covers the air half.

### 3.6 M4 Movement and the Movement rho (FR-018, FR-023; Drifting Strata)

**Premise reads (I-M4)**, before any rung:

1. **Locate the inverting member** (§2.5). The Movement row is run with each member cancelled in turn (five runs), and
   with `kWanderLeverRateCompExponent`'s effect isolated by also cancelling `ResonanceWanderRate`. The run that
   restores rho ≥ 0.9 names the member that breaks monotonicity. A plausible suspect by code-read is the 13c
   `kBreathGravityLaneGain = 3.0` acting on the Movement → BreathingDepth row (0.30 + 0.70, `:420`). 13b measured the
   Movement row monotone at rho 1.000 before 13c (`vorago_voice.h:1715-1722`). This is a suspect, not a diagnosis.
2. **Ask what the descriptor hears.** With `VORAGO_PILOT_OVERRIDE`, Drifting Strata is read with Movement at 0 and one
   member's stored parameter at its Movement-1 value (drift 204, resonance wander 402, noise wander 302, breathing
   1500, damper 1104). This tells which member the C-7.2 descriptor hears. Its `motion` term is
   `log2(perBandTotalVariation)` (`preset_test_support.h:117-118`), the SweepAxes metric itself.

**Rungs** (rebuild). Row rungs (r1, r3, r4) must pass the §2.5 pre-screen, then the real SweepAxes + NoZipper.

| Rung | Change |
|---|---|
| r1 | the inverting member's curve changed, amount unchanged. The admissible curves are Linear, Exponential and SCurve (`core/modulation_types.h:79-84`); Stepped is excluded by the no-Stepped guard (§1.3). So the member has exactly **two** alternative curves, and both are laddered: fewer than three rungs exist, which FR-010's "every rung up to the clamp" allows |
| r2 | `kWanderLeverRateCompExponent` (`vorago_voice.h:1725`) 0.75 → 0.80 / 0.85 / 0.90 (13b measured k = 1 at endpoint +19 %, under the bound, so the ladder stays below 1). **Exempt from the §2.5 pre-screen:** it is a compiled voice constant applied to the colony wander lever (`:1726-1731`, used at `:2251`), not a macro row, and the row-emulation probe cannot represent it. Its only admissibility read is the rebuilt `VoragoMacro_SweepAxes` + `VoragoMacro_NoZipper` |
| r3 | re-aim within the family: the inverting member's amount reduced by a third, and a new row Movement → BreathingIrregularity (base 0.30, the target's existing base, Entropy `:478-482`) at +0.20 / +0.35 / +0.50. Entropy's +0.60 row lands on the same target, so the unclamped sum is pre-computed against the owner's clamp (§3.4) |
| r4 | r-best + the member the descriptor hears (step 2) widened by 1/3, 2/3 and 3/3 of its headroom to its component clamp |

- r3 adds a `kRows` row, so it bumps `kNumRows` and the tally comment (§3.4).

- Admissibility per rung (FR-018): Movement rho ≥ 0.9, endpoint ≥ +20 %, every FR-003 (ii)-passing assertion still
  passing, NoZipper ≤ 1.5×.
- If no rung reaches M4 ≥ 4.0 while monotone, M4 is stop-and-surface with every rung's (d, rho, endpoint, zipper).
- The Movement rho is a separate gate (FR-023). A rung that fixes rho but not M4 is adopted for FR-023 only if it
  clears FR-010d's other clauses. It is never presented as M4's fix.

### 3.7 M10 Life, ruled last (FR-019, FR-020; Teeming)

**Premise (I-M10).** §2.4 on Teeming, which prints the summed values and `CLAMPED` flags at Life 0 / 0.7 / 1.
`EcosystemDepth` is expected `CLAMPED` and `EventRateScale` about 6.5 (Overview). The measured value decides. A row
read `CLAMPED` is excluded from the ladder, except as the FR-025b companion.

| Rung | Change (rebuild) | Companion |
|---|---|---|
| r1 | Life → BloomSpawnRateHz amount 0.0208 → 0.029 / 0.037 / 0.0458. 0.0458 reaches `kMaxSpawnRateHz` 0.05 (`bloom_engine.h:285`) at Life 1 from 1/240 | — |
| r2 | Life → EventRateScale amount 9 → 10 / 11.5 / 13 (13 puts Teeming at 0.7 at 9.3, under the clamp 10; every rung's sum at Life 1 is read against the clamp on I-M10's `CLAMPED` line) | — |
| r3 | Life → EcosystemDepth amount 0.15 → 0.45 / 0.65 / 0.85 (the shipped default 0.85 base clamps at Life ≥ 0.18: inert there) | Teeming `900=0.15` (FR-025b), on every rung |
| r4 | the two best of r1–r3 combined, each at its best magnitude | as needed |

- The companion-alone read (`900=0.15` with no lever, on the base tree) is logged first. If it alone clears 4.0, M10
  is stop-and-surface (FR-025b).
- Life's rows also drive E1's spawn and E4 / E6 / E7's colony. So every M10 rung's read set includes E1, E4, E6.hi,
  E7.hi, S7, D13.1 and D13.2 (FR-010b, FR-010c a). A drop under a bar re-opens that lever once (Q8).

---

## 4. Measurement seams (FR-012), API and plumbing

### 4.1 Voice (`vorago_voice.h`)

These follow the 13c forward pattern (`:1596-1603`). Members are installed in `prepare()` from the compiled
constants:

```cpp
// --- Phase 13d measurement seams (FR-012). prepare() installs the compiled values. ---
void setPartialBloomLaneGain(float g) noexcept;     // reject non-finite; clamp [0, 8]
void setPartialMutationLaneGain(float g) noexcept;  // reject non-finite; clamp [0, 8]
void setLoopWakeBase(float b) noexcept;             // reject non-finite; clamp [kMinRetunedWakeBase, 1]; fills loopWakeBase_
void setLoopGainLeverSpan(float s) noexcept;        // reject non-finite; clamp [0, FeedbackEcology::kMaxLoopGain]
void setCouplingLeverSpan(float s) noexcept;        // reject non-finite; clamp [0, FeedbackEcology::kMaxCouplingPerPair]
void setBloomParentCount(std::size_t k) noexcept { bloom_.setParentCount(k); }         // owner clamps [1, 8]
void setBloomChildrenPerEvent(std::size_t n) noexcept { bloom_.setChildrenPerEvent(n); } // owner clamps [1, 4]
// matching const getters: getPartialBloomLaneGain, getPartialMutationLaneGain, getLoopWakeBase (slot 0),
// getLoopGainLeverSpan, getCouplingLeverSpan, getBloomParentCount, getBloomChildrenPerEvent
static constexpr std::size_t kBloomParentCount = BloomEngine::kDefaultParentCount;          // 4
static constexpr std::size_t kBloomChildrenPerEvent = BloomEngine::kDefaultChildrenPerEvent;  // 2
// private
float partialBloomLaneGain_ = kPartialLaneGain;
float partialMutationLaneGain_ = kPartialLaneGain;
float loopGainLeverSpan_ = kLoopGainLeverSpan;
float couplingLeverSpan_ = kCouplingLeverSpan;
```

- **`applyIdentityLanes` changes.** It reads the four members where it read the constants (`:2232`, `:2233`,
  `:2275`, `:2279`). With the members at their compiled values, every expression is the same float expression as
  today.
- **Getters for the K and children-per-event seams** forward to the component's own read-outs,
  `BloomEngine::getParentCount()` and `getChildrenPerEvent()` (`bloom_engine.h:690-691`). No voice shadow is needed.
- **`setLoopWakeBase` is the only seam that changes an existing per-slot array.** It writes all
  `FeedbackEcology::kMaxLoops` entries, and the next control step's combine picks it up.

### 4.2 Engine (`vorago_engine.h`)

There are seven fan-outs over all `kMaxVoices`. Each has a getter that reads slot 0, after `setBloomChildGain`
(`:1040-1045`). The two `std::size_t` seams (`setBloomParentCount`, `setBloomChildrenPerEvent`) take `float` at the
engine, so the probe parser has one value type. They convert in this order:

1. reject non-finite input with `detail::isFinite` (the previous value stands);
2. clamp **in float** to `[1, BloomEngine::kMaxParents]` (8) or `[1, BloomEngine::kMaxChildrenPerEvent]` (4);
3. `std::lround`, then `static_cast<std::size_t>`.

Clamping before the round is required. `BloomEngine::setParentCount` / `setChildrenPerEvent` clamp a `std::size_t`
(`bloom_engine.h:556-563`). A negative float rounded first would become a negative `long`, which wraps to a huge
`std::size_t` and is clamped to the **maximum**, not the minimum. A float outside `long`'s range (32-bit on MSVC)
would give an unspecified `lround` result. After the float clamp the rounded value is always in `[1, 8]` or `[1, 4]`.

### 4.3 Harness (`preset_test_support.h`) and probe keys

- `detail::readTweakLevers` grows from `std::array<float, 4>` to `std::array<float, 11>` (`:318-321`). The new
  getters are cast to float. The comparison in `renderPreset` (`:451-454`) is unchanged.
- `readPilotLever()` (`preset_pilot_test.cpp:881`) accepts seven new keys:
  - `partialBloomGain`, `partialMutationGain`, `parentCount`, `childrenPerEvent`;
  - `loopWakeBase`, `loopGainSpan`, `couplingSpan`.

  The unknown-key FAIL (`:917-918`) stays. The verdict line still prints `lever: <string>`, so no lever reading can be
  mistaken for a gate.

### 4.4 Secondaries under a lever

- `computeVerificationVector` (`preset_test_support.h:2580`) gains a trailing parameter,
  `const std::function<void(Krate::DSP::VoragoEngine&)>& engineTweak = {}`. It copies the tweak into every
  `RenderSpec` it builds and REQUIREs nothing itself. It returns `tweakHeld` as the AND of its captures in
  `VerificationVector`, as a new `bool tweakHeld = true;` field.
- `runPrimaryProbe` drops the FAIL at `:1013-1019`, passes `lever.tweak`, and REQUIREs `vec.tweakHeld`.
- With no tweak, every render is the same `RenderSpec` as today.

### 4.5 Read-set generator

Two new `VORAGO_PILOT_ITERATE` modes sit next to the existing `verified27` (`:1342-1354`). Today that branch
REQUIREs `*iter == "verified27"` (`:1351`) and forbids an override, a master trim and a lever (`:1352-1354`). It is
widened to accept the two new modes. `verified27` (and `verified36`, §6.3) keep all three prohibitions.

**`liveFor:<lever-id>`: the FR-010c (b) set.** It derives the list in the probe from `vorago_preset_defs.h` and the
liveness predicates below, and runs each listed preset **as compiled** under the current `VORAGO_PILOT_LEVER`. It
REQUIREs that no `VORAGO_PILOT_OVERRIDE` and no master trim is set: the (b) set is read override-free.

**`roster`: the FR-010c (a) set.** It runs every earlier ruled roster cell on its gate surface: the current
`VORAGO_PILOT_LEVER` plus that cell's recorded gate override string.

- The strings live in one probe-side table, `kRosterGateOverrides` (cell → override string, empty when the cell has
  none). The build updates it in the same edit that records a ruling in `rulings.md`, and the FR-025 / FR-050 lever
  table is transcribed from it, so the two cannot disagree.
- Its entries as the ladders proceed: E1 `1300=<ruled>`; E4 `800=0.5`; E6.hi / E7.hi `109=<ruled L>`; M10
  `900=0.15` once r3 (or an r4 containing it) is adopted; M2 / M4 / M5 empty. After the confirming pass transcribes
  a companion into its preset, its entry is emptied.
- The E4 row renders with four takes and the loop-bus meter (§2.1), and the E6.hi / E7.hi row runs the S7 secondary
  path. So each cell's whole gate is re-read, including FR-021b's conjunct and FR-022's secondaries.
- `roster` REQUIREs that no `VORAGO_PILOT_OVERRIDE` is set in the environment: each cell's override comes from the
  table, and a global one would mix surfaces.

Both modes print `cell, host, role, overrides, d, bar, arms, verified` per row (`overrides` is `-` under `liveFor`).
Each lever id maps to its liveness predicate over stored values:

| Lever id | Live when | Used by |
|---|---|---|
| `e1route` | 900 > 0, and bloom depth 1300 > 0 or the cloud sounds | E1 arms A and B (E1-G, E1-M) |
| `e1attach` | bloom depth 1300 > 0, regardless of 900 | E1 attachment arm c1 / c2 (E1-K, E1-C) |
| `e4` | ecology mix 500 > 0 | E4 (W1, F-G, F-C, W2) |
| `e67` | 900 > 0 | E6.hi / E7.hi |
| `m2` / `m4` / `m5` / `m10` | the macro is away from its neutral | M2, M4, M5, M10 |

An undecidable preset counts as live (FR-010c).

---

## 5. State layout, contracts, RT safety

- **Members added per voice:**
  - 4 floats for the gain / span seams;
  - `loopTap_`: 6 × 64 floats = 1536 B;
  - `carryLoopBus_`: 64 floats;
  - `ecoLaneMean_`: 5 floats;
  - 1 bool.

  Per engine: two 64-float arrays and one double. Everything is fixed-size, in-object, and allocated nowhere.
  `getAllocatedBytes()` after `prepare()` is unchanged, which SC-014 asserts.
- **`prepare()` contract.** It installs every seam member from its compiled constant, the two bloom counts, and
  `loopBusMeter_ = false`.
- **`reset()` and the clearing path** keep configuration (the seam members, like `kBloomChildGain` today) and zero
  `carryLoopBus_`.
- **`process` contract.** Seams are control-path writes. They take effect at the next control step, through the same
  setters the lanes already drive, and nothing new ramps. The meter adds one branch per 64-sample chunk when off.
- **RT safety (E-11).** All of the above is noexcept, allocation-free, lock-free and exception-free. Non-finite input
  to every new seam is rejected with `detail::isFinite` (`core/db_utils.h:118`), never `std::isnan` (E-12).
- **Layering.** Every edit is inside Layer-3 Vorago headers, which include `bloom_engine.h`, `feedback_ecology.h` and
  `ecosystem_engine.h` as before. No new include.
- **Surface (FR-039).** No parameter, ID, range, default or state field changes. `kCurrentStateVersion` stays 3
  (`plugins/vorago/src/plugin_ids.h:24`). The seams are not parameters.

---

## 6. Test plan

The two executables:

- **dsp:** `build/windows-x64-release/bin/Release/dsp_systems_tests.exe`;
- **plugin:** `.../vorago_tests.exe`.

"Hidden" means `[.probe]`, run by name. Every gate figure is the probe's printed line in a checked-in log under
`specs/vorago-phase13d-engine-ceilings/artifacts/`.

### 6.1 New per-push unit tests (in existing TUs; no CMake change)

| ID | TU | `TEST_CASE` | Assertions |
|---|---|---|---|
| T-M1 | `dsp/tests/unit/systems/vorago_engine_test.cpp` | `"VoragoEngine_CeilingSeamContract", "[systems][vorago]"` | (1) after `prepare()` each of the 7 getters equals its compiled constant on every `getVoice(i)`; (2) a set value fans out to all `kMaxVoices`; (3) NaN, +Inf and −Inf (bit patterns via `std::bit_cast`, the `reference_fastmath_nan_in_tests` rule) leave the value unchanged; (4) out-of-range values clamp to the documented bounds, including, for both `std::size_t` seams, `-1.0f` → 1 and `1e12f` → the maximum (8 parents, 4 children per event), which pins §4.2's clamp-before-round order; (5) `setLoopWakeBase(0.0f)` reads `kMinRetunedWakeBase` (E-8) |
| T-M2 | same | `"VoragoEngine_LoopBusMeterIsObservationOnly", "[systems][vorago]"` | Two engines with the same seed, one note, 20 s: the meter-on output equals the meter-off output **sample for sample in the same process** (the same function runs; this is an A/B identity, not a stored golden). With the meter off, `takeLoopBusSumSq() == 0.0`. With it on and the ecology mix > 0, the sum is > 0 and finite by bit pattern. Every per-sample `|loopBus| ≤ kMaxLoops * kMaxVoices` (each tap is a `tanh × gate ≤ 1`, `feedback_ecology.h:2248-2251`). `getAllocatedBytes()` is unchanged after enabling |
| T-M3 | `dsp/tests/unit/systems/vorago_ecosystem_lever_test.cpp` | `"VoragoVoice_CeilingLeverNeutral", "[systems][vorago]"` | With the friend `Probe::injectEco`: at lane 0 every new member reads its destination's base exactly (mutation, bloom depth, loop wake = `max(base, sched)`, loop gain = base, coupling = `kRingCouplingBase`); at raw ∈ {0.1, 0.5, 1.0} with `partialBloomLaneGain_ ≠ partialMutationLaneGain_` set through the seams, each destination equals `clamp(base + its own gain · raw)`; `setLoopWakeBase(b)` moves `getLoopWakeAmount` to `combineWake(b, raw, sched)`. This is SC-013's neutrality for the new seams; the two named SC-013 tests stay unedited |
| T-M4 | `dsp/tests/unit/systems/vorago_ecosystem_lever_test.cpp` | `"VoragoVoice_EcoLaneMeanReadout", "[systems][vorago]"` | after `injectEco(uniformLanes(x))` and one control step, `getEcoLaneMean(k) == x` for every kind with ≥ 1 addressed slot; 0 for an out-of-range kind |

### 6.2 Hidden instruments (logged, never gated per push)

| ID | Where | How it is run |
|---|---|---|
| I-E1 | `preset_pilot_test.cpp`, `VORAGO_PILOT_BLOOM=1` | `VORAGO_PILOT_PRESET="Bloom Colony" VORAGO_PILOT_TAKES=4 VORAGO_PILOT_BLOOM=1 vorago_tests.exe "Vorago_PresetPilot_PrimaryProbe"` → `artifacts/ie1_bloom.log` |
| I-E4 | `VORAGO_PILOT_LOOPBUS=1` | Feeding Loops at `800=0.5` and as stored → `artifacts/ie4_loopbus_{1x,stored}.log` |
| I-E67 | `VORAGO_PILOT_LANES=1`; `Vorago_EcosystemRuleProbe` with `VORAGO_PROBE_SURFACE` at Life max and `VORAGO_PROBE_LANES=1` (§2.3) | `artifacts/ie67_lanes_L{045,070,090,100}.log`, `ie67_lanes_lifemax.log` |
| I-M4 | `VoragoMacro_Phase13dRowProbe` (`vorago_macro_retune_probe_test.cpp`) + pilot overrides | `artifacts/im4_member_cancel.log`, `im4_member_override_<id>.log` |
| I-M10 | `VORAGO_PILOT_READBACK=1` | `artifacts/im10_readback.log` (Teeming), `ie67_readback.log` (Colony Pulse) |

### 6.3 FR / SC → test, assertion and threshold

| FR / SC | Test or instrument | Assertion strategy |
|---|---|---|
| FR-003 / SC-001 | the probe as compiled, `VORAGO_PILOT_TAKES=4`, per roster cell → `before_<cell>.log`; E4 also at `800=0.5`; **E4's FR-003 (i) loop-level record** is `ie4_loopbus_{1x,stored}.log` (all four takes, both rates), taken at step I on the instrumented binary once `instr_inert_*.log` shows it equals B (§8); one cell (E1) twice → `before_E1_repeat.log`; `VoragoMacro_SweepAxes` → `base_sweepaxes.log`; `VoragoMacro_NoZipper` → `base_nozipper.log`; 13b Gate 1 / Gate 2 at the default surface and Life max → `gate1_base_*.log`, `gate2_table_*_base.log` | `|d − d_sweep5| ≤ max(0.01, 0.005·d_sweep5)`, also on `attribBase`; SweepAxes Movement rho prints 0.8333. Out of tolerance = stop |
| FR-004 | I-E4 / I-E67 lines | present in the same run as the gate line |
| FR-005 / SC-019 | the 13b probe at the default surface (t0, `t0on`, M1 RMS) and the pilot P0 row, recorded as 13c did (13c `plan.md:957`, `:968`) → `default_{before,after}_{13b,p0}.log` | finite (bit pattern), peak ≤ 0.9661; the values are recorded, not gated |
| FR-010 / FR-010c / FR-010d / SC-026 | ladder logs `artifacts/<cell>_r<n>_{gate,roster,readset,minus6}.log` (`roster` = `VORAGO_PILOT_ITERATE=roster`, `readset` = `liveFor:<id>`) + `rulings.md`; for E6.hi / E7.hi also `e67_r<n>_lifemax.log` | adoption line cites gate d, both read-set tables, arms and `arm1@44.1k`, the −6 dB d, and the smallest-change rung of its ladder |
| FR-010b / SC-027 | `rulings.md` re-open count per lever | ≤ 1 re-ladder; a second drop gets a stop line with both ladders |
| FR-013 / SC-003 | probe on Bloom Colony, final tree → `final_E1.log` | `route arms:` d ≥ 4.0 **and** ≥ attribBase + 1.5; `levels:` four `y` and `arm1@44.1k [y]` |
| FR-014 / FR-021b / SC-004 | probe on Feeding Loops, `VORAGO_PILOT_TAKES=4 VORAGO_PILOT_LOOPBUS=1`, `800=0.5` until the re-author, then as compiled → `final_E4.log` | the route gate as SC-003; arms 2 and 3 `y` on **all four** take lines; every `loopbus take j` line `PASS` (worst 10 s ≥ −60 dBFS); verdict `loopsAlive y` |
| FR-015 / FR-022 / SC-005 | per rung: `e67_r<n>_gate.log` and `e67_r<n>_lifemax.log` (§3.3, syncRate / selfAffinity rows and `LANES` lines recorded); final: probe on Colony Pulse with `109=<L>`, `VORAGO_PILOT_SECONDARY=1 VORAGO_PILOT_CELLS=S7` → `final_E67.log`; 13b Life-max table with `VORAGO_PROBE_LANES=1` → `final_gate2_lifemax.log` | `secondary E6.hi … -> VERIFIED`, `secondary E7.hi … -> VERIFIED` (d ≥ 1.5, state ok, conjunct ok); S7 primary ≥ 4.0 and D13.2 VERIFIED in the same run |
| FR-016 / FR-017 / FR-019 / FR-020 / SC-002 | probe on Erosion / Stone Gravity / Teeming / Drifting Strata → `final_M{2,5,10,4}.log` | verdict d ≥ 4.0, `levels:` four `y`, `arm1@44.1k [y]`; M5 also arms 1 and 3 `y` on four takes |
| FR-018 / FR-023 / SC-006 | `dsp_systems_tests.exe "VoragoMacro_SweepAxes"` → `final_sweepaxes.log`; NoZipper → `final_nozipper.log` | the Movement row's two CHECKs pass (`vorago_macro_test.cpp:1318-1319`) with the thresholds untouched; the case is green; NoZipper green |
| FR-024 / FR-024c / SC-007 | the gate run, and the same with `VORAGO_PILOT_MASTER_TRIM=-6` → `final_minus6_<cell>.log` | d bar (and attrib / state / conjunct) still met; arms printed, not gated |
| FR-025 / FR-025b | the lever table, and the companion-alone log per companion | the companion-alone d is below the gate (else stop) |
| FR-026 | every `final_*.log` from one binary, alone | the compliance cites those lines only |
| FR-030 / SC-008 | `VORAGO_PILOT_ITERATE=verified36` (the `verified27` mechanism, roster = the 6 primaries) → `final_verified36.log` | derived count REQUIRE 36; each row d ≥ bar, arms green; sunk presets re-read after the confirming pass |
| FR-030b / SC-009 | `VORAGO_PILOT_SECONDARY=1` per host → `final_secondaries.log`; `Vorago_PresetSweep_AblationVerifiesClaims` default pseudo-preset | the before/after table from sweep-5 records |
| FR-031 / SC-010 | `final_sweepaxes.log`, `final_nozipper.log`, the `[long]` lanes | every assertion passing in `base_sweepaxes.log` still passes, plus Movement; NoZipper ≤ 1.5× |
| FR-032 / SC-011 | `Vorago_EcosystemRuleProbe` at both surfaces → `final_gate{1,2}_*.log`; the named 13b / 13c tests in the per-push suite | GATE1M ≥ 0.5; every knob counting in the base tables still counts; the named tests green with `git diff` showing them unedited |
| SC-012 | `VoragoMacro_NeutralIsIdentity` (`vorago_macro_test.cpp:1419`); the build | green; static_asserts compile with any added row, including `kRows.size() == kNumRows` (`vorago_macro_matrix.h:1223-1224`) with `kNumRows` (`:276`) and its tally comment (`:272-275`) bumped |
| SC-013 | `VoragoVoice_EcosystemLeverNeutral` (`:1147`), `VoragoVoice_RouteLeverZeroAtZeroLane` (`:1360`), T-M3 | green; the first two unedited (a compiled split would need a ruling, §11) |
| SC-014 | `VoragoEngine_CapabilityLeverBounded` (`vorago_ecosystem_lever_test.cpp:1974`) | unedited: it renders the compiled constants, so the ruled levers are in it by construction, with every macro at 1 (any new rows included), Gravity 0 and 1, eco lanes at 1, 44.1 / 48 / 96 kHz, `|out| ≤ 0.9661`, finite by bit pattern, bytes unchanged |
| FR-033 / SC-015 | `node tools/run-cpu-tests.js dsp_systems_tests`, then `vorago_tests`, alone, idle machine → `cpu_{base,final}_*.log`; clause (i) by alternating pinned A/B against the base binary (13c B-2) | the `VoragoEngine_CpuBudget` clauses pass; P/D ≤ 1.05; worst preset ≤ 1.15; delta in ns and % |
| FR-034 / SC-018 | `node tools/run-close-lanes.js` → `summary.txt` | 100 %; test edits only from §6.4's list |
| FR-035 / SC-017 | `VoragoEngine_GhostExtensionWiring` printer, twice | md5 of the two literal blocks identical; verify run green; new PROVENANCE |
| SC-016 | `VoragoEngine_SlotSeedReproducibility` (`vorago_engine_test.cpp:1840`), `Vorago_PresetSweep_RendersAreReproducible` | green (render_fingerprint tolerances) |
| FR-036 / SC-024 | build log, `node tools/check-portability.js` then `wsl --shutdown`, `./tools/run-clang-tidy.ps1 -Target dsp` and `-Target vorago`, pluginval 5 | 0 warnings; clean; 0 / 0; pass |
| FR-037 / SC-025 | `git diff --name-only 502e5243..HEAD` → `git_diff_names.txt`; `seraphis_tests` | no Seraphis path or Seraphis-consumed header; green |
| FR-039 / SC-020 | `param_table_test`, `state_v2_test`, `state_v3_test` | green unedited |
| FR-040 – FR-044 / SC-021 – SC-023 | §8 step C | see §8 |

### 6.4 FR-034 pre-surfaced list (tests that encode old voicing as data)

Each entry applies **only if** the named mechanism is adopted. Its edit is listed with its reason before any
re-measure, and none is edited silently.

1. **`VoragoVoice_EcosystemLaneShapingFidelity`** (`vorago_ecosystem_lever_test.cpp:1334-1337`).
   - Trigger: `kPartialLaneGain ≠ 1`.
   - Reason: the expectation hard-codes gain 1 (`+ raw`).
   - Edit: the expectation reads `Probe::partialLaneGain() * raw`.
2. **`checkWakes`** (`:374-386`) — `VoragoVoice_EcosystemLeverMapping` and `LaneShapingFidelity`.
   - Trigger: W2 only.
   - FR-032 names `EcosystemLever*` unedited, so this is a **user ruling** (§11), not a FR-034 self-listing.
3. **`VoragoVoice_RouteLeverZeroAtZeroLane`** (`:1386-1389`) and the `Probe::partialLaneGain()` accessor (`:137`).
   - Trigger: a compiled split.
   - SC-013 says unedited, so this is a **user ruling** (§11).
4. **`VoragoEngine_GhostExtensionWiring` clause (a)**: a fingerprint re-harvest (FR-035), not an edit of an
   assertion.
5. **`VoragoMacro_SweepAxes` comment block** (`vorago_macro_test.cpp:1306-1317`) for the Gravity / Pressure history.
   It is untouched. It is listed so that no rung's rewrite of nearby text is mistaken for a threshold change.

Before the first rebuild rung on each cell, the build re-greps `dsp/tests` and `plugins/vorago/tests` for the literal
of every constant or row amount the rung touches. Any new hit joins this list first.

---

## 7. Build integration

- **No CMake list changes.** Every edited test TU is already registered:
  - `dsp/tests/CMakeLists.txt:511`, `:513`, `:515`, `:560` (voice, engine, macro and lever tests in
    `dsp_systems_tests`). The retune-probe TU is in the same list; the build confirms it before adding the case;
  - `plugins/vorago/tests/CMakeLists.txt:52`, `:58`, `:60` (rule probe, sweep and pilot in `vorago_tests`).
  - The plugin TUs are already in the `-fno-fast-math` per-file list (`:148`, `:153`, `:155`), and the new
    finiteness checks use bit patterns anyway.
- **Targets built per rung:**
  - dsp rungs: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target
    dsp_systems_tests vorago_tests`. Both are needed, because the plugin links the headers;
  - seam rungs: no rebuild.
- **Close builds** add `dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_effects_tests seraphis_tests
  shared_tests` and the `Vorago` plugin target for pluginval.
- **Long runs** are captured to log files, never re-run for output (memory rules). The CPU lane runs alone (FR-033).

---

## 8. Order of work and artifacts (FR-003, FR-010b, FR-026, FR-040–FR-044)

| Step | Tree | Work | Artifacts |
|---|---|---|---|
| B | `502e5243`, unmodified binary | FR-003 (i)–(iii); FR-005 before; CPU base (§6.3); copy the sweep-5 records and hash them (FR-042) | `before_*.log`, `base_*.log`, `gate*_base*.log`, `default_before_*.log`, `cpu_base_*.log`, `sweep5-records/`, `sweep5-records.sha256` |
| I | instruments only (§2, §4; T-M1–T-M4); seam members at compiled values | build; per-push suites green; **re-read two roster cells as compiled and confirm they equal B within FR-003's tolerance**, which proves the instruments are inert; then run I-E1, I-E4, I-E67, I-M4, I-M10. I-E4 is also FR-003 (i)'s loop-level before-record (see below) | `instr_inert_*.log`, `ie*.log` (incl. `ie4_loopbus_{1x,stored}.log`), `im*.log` |
| L1 | + E1 | §3.1 ladder | `e1_r*.log`, `rulings.md` lines |
| L2 | + E4 | §3.2 | `e4_r*.log` |
| L3 | + E6 / E7 | §3.3 | `e67_r*.log` |
| L4 | + M2 | §3.4 (pre-screen, rebuild, SweepAxes + NoZipper per adopted rung; an added row bumps `kNumRows` and its tally comment) | `m2_r*.log` |
| L5 | + M5 | §3.5 (every rung adds a row: the same `kNumRows` bump) | `m5_r*.log` |
| L6 | + M4 / rho | §3.6 | `m4_r*.log` |
| L7 | + M10 | §3.7; re-opens per Q8 | `m10_r*.log` |
| F1 | final binary, alone | FR-026: every roster gate, the −6 dB re-reads, `verified36`, the secondaries | `final_*.log` |
| F2 | final binary | SweepAxes, NoZipper, 13b gates, FR-005 after, soaks, close lanes, CPU lane alone, the GhostExtensionWiring re-harvest, check-portability + `wsl --shutdown`, clang-tidy, pluginval, `git diff --name-only 502e5243..HEAD` | `final_*.log`, `summary.txt`, `git_diff_names.txt` |
| C | confirming pass | (1) full re-author by probe override batches of the roster's showcase / host presets (with `800=0.5` into Feeding Loops and Colony Pulse's Life), Hull Resonance, Smeared Horizon, Haunted Colony and every sunk preset; (2) re-measure every other affected preset, re-author only on a drop > 0.01 or a lost verification, citing the line; (3) transcribe into `tools/vorago_preset_defs.h` with comments, regenerate, `Vorago_FactoryPresets_TreeMatchesGenerator`; (4) re-run each shard containing an affected preset (`VORAGO_SWEEP_SHARD=i/N`), then the aggregate with `VORAGO_SWEEP_IN` on the checked-in records; (5) the FR-043 T063 re-read; (6) `Vorago_PresetCpu` alone (FR-044) | `reauthor_*.log`, `sweep6_shard_*.log`, `sweep6_aggregate.log`, `cpu_presets_final.log` |
| R | compliance | the FR-050 lever table (feature, cells, every mechanism, before, every rung → reading → log, ruled value, after, override string); FR-051 default-render note; FR-052 CPU delta; `rulings.md` | `compliance.md` |

A shard reading that differs from its probe reading by more than 0.01 is stop-and-surface (FR-042). After step C,
`SUBSET pairs: 0` is read from the aggregate's NoShowcaseSubset line (SC-022).

**FR-003 (i)'s loop-level part is taken at step I, not step B.** The loop-bus meter (§2.1) does not exist on the
unmodified `502e5243` binary. FR-003 (i) asks for E4's loop-level readout on all four takes at both rates as part of
the before-record. It is therefore taken at step I, **after** `instr_inert_*.log` shows the instrumented binary equals
B within FR-003's tolerance (the meter is observation-only, T-M2). The designated artifacts are
`ie4_loopbus_1x.log` (`800=0.5`) and `ie4_loopbus_stored.log` (0.6×). They are the record FR-021b's clause "if the
before-record shows the loop bus failing on the stored take at 1.0×" refers to. If step I's inertness read fails, no
loop-level before-record exists, and that is stop-and-surface.

---

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| **A global lever sinks other presets** (E-1; 13c B-11 / B-15 precedent) | The FR-010c read set is generated, not typed (§4.5). A sinking rung may be adopted, and the sunk preset then joins the full re-author (FR-030). A failed re-author is surfaced |
| **Louder, not more distinct** (E-2); limiter-held passes | The −6 dB re-read on every rung, not only at the close. Arm 1 at 44.1 kHz on every rung |
| **E4 lowering the wake base silences the loop bus** (E-3) | The loops-alive conjunct is read on all four takes for every rung (§2.1). r4 / r5 companions keep the loops fed. The base never goes below `kMinRetunedWakeBase` (seam clamp and static_assert), so the identity path can never trigger the FR-063 sleep edge (E-8) |
| **Loop-bus meter perturbs the render** | Same function with taps (write-only, `feedback_ecology.h:976-978`). T-M2 asserts same-process sample identity, and step I re-reads two cells as compiled |
| **Denormals in the meter's accumulation** | Taps are already `flushDenormal`ed (`feedback_ecology.h:2251`). The sum is accumulated in `double` on the engine, and the bus is never fed back |
| **Movement fixes break zipper or other SweepAxes rows** (E-7) | Per-rung admissibility (FR-018). The §2.5 pre-screen before any rebuild. Per-seed series printed so a single-seed inversion is visible |
| **Macro row edits violate a compile-time predicate** | Re-aims reuse existing targets and bases (§1.3), so `everyRowSharesOneBasePerTarget` and `Count == 41` hold by construction. An added row bumps `kNumRows` and its tally comment by hand, and `kRows.size() == kNumRows` (`:1223-1224`) catches a forgotten bump. The build is the check (SC-012) |
| **Gravity re-aim only travels one way** (E-6) | Candidate targets are chosen with headroom on both sides of their base (§3.5). SC-014 Gravity 0 and 1 |
| **Life re-opens earlier cells** (E-5) | Life last. Every M10 rung's read set includes E1, E4, E6 / E7, S7, D13.x. At most one re-open per lever (Q8) |
| **Portability: MSVC-green code fails Clang / GCC** | No brace-init narrowing (the `std::size_t` seams clamp in float, then `std::lround` and `static_cast`, §4.2, so no negative or out-of-range value ever reaches the conversion). No `std::isnan` (bit-pattern `isFinite`). No SIMD added. `check-portability.js` at F2 and before every commit |
| **Bit-exact goldens** | None introduced. T-M2's identity is a same-process A/B, not a stored digest. Fingerprints use `render_fingerprint.h` tolerances and are harvested once, on the final tree (E-14) |
| **CPU drift between runs** | CPU only in the isolated lane, after idle. Clause (i) by alternating pinned A/B (13c B-2). Never a budget change |
| **Seam hides a block-0 override** (e.g. `kBloomDepthId` re-writing via MB at block 0) | `tweakHeld` re-reads all 11 getters after block 0 (§4.3). None of the new seams is a parameter target, so block-0 parameter changes cannot reach them; the check proves it per take |
| **Time** | Seams for the two route ladders. Macro pre-screen before rebuilds. Sharded sweep only for affected presets (roadmap 680–682) |

---

## 10. ODR sweep (FR-038)

`grep -rn -- "<Name>" dsp/ plugins/ tools/` was run in the planning session for every name below, on the `502e5243`
tree (no code is uncommitted). Every one returned **0** hits:

- **Voice and engine API:** `setLoopBusMeterEnabled`, `isLoopBusMeterEnabled`, `takeLoopBusSumSq`,
  `setPartialBloomLaneGain`, `getPartialBloomLaneGain`, `setPartialMutationLaneGain`, `getPartialMutationLaneGain`,
  `setLoopWakeBase`, `getLoopWakeBase`, `setLoopGainLeverSpan`, `getLoopGainLeverSpan`, `setCouplingLeverSpan`,
  `getCouplingLeverSpan`, `setBloomParentCount`, `getBloomParentCount`, `setBloomChildrenPerEvent`,
  `getBloomChildrenPerEvent`, `getEcoLaneMean`.
- **Members:** `loopBusMeter_`, `loopTap_`, `carryLoopBus_`, `loopBusAcc_`, `vLoopBus_`, `loopBusSumSq_`,
  `partialBloomLaneGain_`, `partialMutationLaneGain_`, `loopGainLeverSpan_`, `couplingLeverSpan_`, `ecoLaneMean_`.
- **Constants:** `kBloomParentCount`, `kBloomChildrenPerEvent`, `kPartialMutationLaneGain`, `kPartialBloomLaneGain`,
  `kLoopWakeLaneGain`.
- **Harness and probe names:** `loopBusPower`, `loopBusMeter`, `loopBusWindowDb`, `blockObserver`, `LaneStats`
  (TU-local, rule probe), `kRosterGateOverrides` (pilot TU).
- **Tests:** `VoragoEngine_LoopBusMeterIsObservationOnly`, `VoragoEngine_CeilingSeamContract`,
  `VoragoVoice_CeilingLeverNeutral`, `VoragoVoice_EcoLaneMeanReadout`, `VoragoMacro_Phase13dRowProbe`.
- **Env options and option values:** `VORAGO_PILOT_LANES`, `VORAGO_PILOT_LOOPBUS`, `VORAGO_PILOT_BLOOM`,
  `VORAGO_PILOT_READBACK`, `VORAGO_PROBE_LANES`, `VORAGO_ROWPROBE`, `liveFor`, `verified36`, `e1route`, `e1attach`.
  The `roster` iterate value is a string literal compared in one branch; `"roster"` has 0 hits in
  `plugins/vorago/tests`.

**Near-names that exist and do not collide** (recorded so the build does not re-discover them):

- `tweakHeld`: exists as `SweepCapture::tweakHeld` (`plugins/vorago/tests/preset_test_support.h:309`), read in
  `preset_pilot_test.cpp:970`, `:1134`, `:1163`, `:1300`. The plan adds a same-name field to a **different** struct,
  `VerificationVector` (§4.4). Members of different classes do not collide.
- `loopWakeBase`: exists as test-only static accessors `Probe::loopWakeBase` (`vorago_ecosystem_lever_test.cpp:195`)
  and `IdentityProbe::loopWakeBase` (`vorago_voice_test.cpp:2493`), both reading the existing voice member
  `loopWakeBase_`. The plan uses `loopWakeBase` only as a `VORAGO_PILOT_LEVER` key string, and adds
  `setLoopWakeBase` / `getLoopWakeBase` on `VoragoVoice` / `VoragoEngine` (0 hits). The existing accessors are
  unchanged.
- `kPartialLaneGain`: exists (`vorago_voice.h:1685`); the plan reuses it, and does not redeclare it.

`kPartialSpawnSpanHz` / `kPartialChildGainSpan` are 13c names that were removed (13c `rulings.md:12`). This plan does
not reuse them. Any name a rung adds that is not listed here is swept by the build before it is written, and the
result is appended to `artifacts/odr_sweep.txt`.

---

## 11. Items for the user (only what FR-010d reserves)

1. **E1's child-attachment change (FR-013).** Any adopted `parentCount` or `childrenPerEvent` other than 4 / 2,
   including a decision to drop attachment because its ladder adds nothing. It is presented with the c1 / c2 ladder
   readings.
2. **A compiled E1 split (§3.1).** It would edit `VoragoVoice_RouteLeverZeroAtZeroLane`, which SC-013 names unedited.
   It is presented only if **no single-gain (arm A) rung** clears E1 under FR-010d and an arm-B rung does. Both arms'
   readings are presented with it.
3. **E4 W2 (§3.2).** It would edit `checkWakes`, and so `VoragoVoice_EcosystemLeverMapping`, which FR-032 names
   unedited. It is presented only if W1 and its companions fail.
4. **Every FR-027 stop-and-surface.** For example: an instrument that refutes the plan's model (E4's sleep edge),
   E6.hi / E7.hi with a flat lane ranking, M4 with no monotone rung at ≥ 4.0, a companion that clears its gate alone,
   or a second drop of the same cell.
5. **Every FR-030 conflict** whose confirming-pass re-author fails.

Every other choice — mechanism, rung value, rung order inside a cell, and companion values — is made by the build
under FR-010d and logged in `artifacts/rulings.md`.
