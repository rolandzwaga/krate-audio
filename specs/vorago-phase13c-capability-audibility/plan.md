# Implementation Plan: Vorago Phase 13c — Capability Audibility

**Spec:** `specs/vorago-phase13c-capability-audibility/spec.md` (reviewed; Clarifications Q1–Q6 of 2026-10-01)
**Roadmap:** `specs/Vorago-roadmap.md` Phase 13c (lines 615–660); Cross-Cutting Constraints (717–741)
**Branch / base:** `feat/vorago-phase1-events-modulation` at `64f57e1a`
**Status:** PLAN. This document writes no production code.

Every `file:line` below was read in the planning session on `64f57e1a`. §9 sweeps the names this plan
introduces (FR-038).

---

## 0. What this plan decides (read first)

1. **No new class, no new file in `dsp/include`, no new test TU, no CMake list change.** Production edits
   stay inside `vorago_voice.h`, `vorago_engine.h` and `vorago_macro_matrix.h` (FR-019). These headers are
   **read-only**: `bloom_engine.h`, `harmonic_cloud.h`, `atmosphere_engine.h`, `feedback_ecology.h`,
   `multi_stage_envelope.h` and the life modulators. Every lever below reaches them through setters that
   already exist. Test edits land in TUs that are already registered (§6).
2. **Bloom (FR-011) is solved inside `VoragoVoice`, with no Seraphis-header edit.** The voice floors the
   cloud's *richness*, not its count, at `kCloudRichnessFloor = 0.6346f`. That makes
   `HarmonicCloud::getActivePartialCount()` ≥ 14 at every user richness. The voice keeps the user's richness
   in its existing shadow `cloudRichness_`, and reproduces the user's partial count and rolloff through the
   spectral target it already hands the cloud. The child gain becomes a voice constant, `kBloomChildGain`,
   written once in `prepare()`. Both are one lever-table row (Q5). See §2.1.
3. **Attack (FR-016, option (b)) ships as a stage-0 reshape that tracks the registered time.** Stage 0 runs
   `EnvCurve::Linear`, and while stage 0 runs the voice raises that ramp's phase to the power
   `kAttackShapePower` (ladder 4–7). Neither shipped `EnvCurve` can meet FR-016's "audibly ~20 s" clause:
   Linear reaches RMS(Sus) − 6 dB at 0.426 T and Logarithmic at 0.653 T. Stages 1–5 keep
   `EnvCurve::Exponential`, and no stored default moves. The ruled shape must clear D9.1 (SC-004) **and** put
   the measured `P_rev` reach at ≥ 0.75 of the registered stage-0 time (§2.5). The D9.1 / D8.2 attack window
   is resized from the **measured** reach, in both the sweep helper and the pilot probe. This is the one
   FR-002 exception, and it lands at step L5 (§3.3, §7). See §2.5.
4. **Every lever candidate is measured on a rebuilt binary** (FR-010's constant route), or through
   `VORAGO_PILOT_OVERRIDE` where a parameter already reaches the lever. Child gain, ghost-tap make-up, ghost
   density, ecology wet make-up, route spans, lane gains, the attack shape and macro rows are all
   `constexpr`, so each candidate value is a rebuild. §2.9's rebuild-free engine-tweak route is **optional**.
   It is built only if the user accepts it at §10.2 **and** the spec adds it to FR-010 / FR-002 as a third
   measurement route. Nothing in §7 depends on it.
5. **The probe gains reporting only** (FR-004). It adds a secondary readout, the arms and `d` on all K
   takes, a 44.1 kHz arm-1 render and a −6 dB master-gain re-read. All of them honour
   `VORAGO_PILOT_OVERRIDE`. See §3.
6. **The order of work is FR-010b's fixed order**: bloom → routes → ghost → S4 → attack → life modulators →
   macros (Life last). Before it come the instrument and the before-record. After it come the final-tree
   re-measure, the regression gates and the single fingerprint re-harvest. See §7.

---

## 1. Verified facts this plan builds on

### 1.1 `VoragoVoice` — `dsp/include/krate/dsp/systems/vorago_voice.h` (Layer 3)

- **Envelope.**
  - `static constexpr EnvCurve kStageCurve = EnvCurve::Exponential;` (`:304-305`). Its doc reads "The ONE
    curve every setStage() call uses".
  - `kDefaultStageLevels{1.00, 0.80, 0.92, 0.85, 0.85, 0.00}` and
    `kDefaultStageTimesMs{20000, 30000, 45000, 60000, 0, 0}` (`:322-326`).
  - The two writers into `mse_` are `setEnvelopeMode` (`:1052-1063`, with
    `mse_.setStage(st, stageLevel_[i], ms, kStageCurve)` at `:1061`) and `applyStage` (`:1866-1883`, at
    `:1880` and `:1882`).
  - The envelope multiplies the **excitation bus** (cloud + noise) only, per sample:
    `const float g = velocity_ * mse_.process();` (`:2449`). Resonance, ecology, bodies and the cavern ring
    after it (`:2436-2470`).
  - `noteOn` calls `mse_.gate(true)` (`:944`). The voice sets `RetriggerMode::Legato` (`:705`) and never
    enables the envelope loop (grep of `setLoop` in the header finds no envelope call). So stage 0 is
    entered only from `Idle` (`multi_stage_envelope.h:106-109`).
  - `getEnvelopeOutput()` returns `envOutput_`, the last applied excitation gain (`:1109`, written at
    `:2445` and `:2452`).
- **Read accessors:** `ecology()` and `bloom()` return const references (`:1568-1569`).
- **Bloom slot budget:** `kBloomChildSlots = 6`, `kMinParentSlots = 8`, `kMinCloudCapacity = 14`
  (`:346-355`).
- **Wake bases:** `kPeakWakeBase = kLoopWakeBase = 0.45f` (`:391-392`).
- **Size bound:** `kVoiceSizeBound = 123840` (`:437`), asserted at `:2682`.
- **Prepare.**
  - `setRichness(0.70f)` (`:558`).
  - Ecology: `setEcologyMix(kDefaultMix 0.15)`, `setEcologyLoopGain(0.72)`, ring coupling at
    `kRingCouplingBase` (`:603-614`).
  - Bloom: `setBloomDepth(0.60f)` (`:617`), spawn 1/240, fades 45/120/180 s, consumer tilt (`:618-625`).
  - Grep this session found no `bloom_.setChildGain` and no `ecology_.setWetGain` call anywhere in the
    header.
- **Richness.** `setRichness(float r)` (`:1141-1144`) writes `cloud_.setRichness(r)` and then
  `cloudRichness_ = cloud_.getRichness()`. `getRichness()` returns `cloud_.getRichness()` (`:1145`). The
  member `cloudRichness_ = 0.70f` (`:2676`) is documented as "B-2's shadow; the ONE source of p(r)".
- **Section setters:**
  - `setEcologyMix` (`:1339-1344`) and `setEcologyLoopGain` (`:1349-1358`);
  - `writeNoiseLevel` (`:1690-1695`, clamps to −96…+12 dB) and `writeLoopGain` (`:1697-1703`);
  - `setBloomDepth` (`:1513-1527`: finite check, clamp, FR-067 early-out) and `setBloomSpawnRateHz`
    (`:1530`, a bare forward).
- **13b levers:**
  - spans `kNoiseLevelLeverSpanDb = 12`, `kPeakLevelLeverSpanDb = 18`, `kLoopGainLeverSpan = 0.18`
    (`:1607-1609`);
  - `kFreqWanderBaseSemis = 0.75`, `kRingCouplingBase = 0.06`, `kCouplingLeverSpan = 0.30` (`:1617-1619`),
    `kFreqWanderLeverSpanSemis = 3` (`:1624`);
  - `kNoiseBusMakeupDb = 30` (`:1634-1635`);
  - `kLeverInputGain{1, 2, 2, 2, 1}`, with a `static_assert` that Partial and Ghost stay 1 (`:1665-1670`);
  - `shapeLeverInput = clamp(gain * lane, 0, 1)` (`:1682-1684`).
- **`applyIdentityLanes`** (`:2096-2175`):
  - wakes via `combineWake` (`:2105-2122`);
  - Partial lane: `cloud_.setMutation(clamp(mutationBase_ + partialEco))` and
    `bloom_.setDepth(clamp(bloomDepthBase_ + partialEco))` (`:2124-2126`);
  - `ghostRequest_ = combineWake(0.0f, lanes.eco[kGhost][0], lanes.sched[kGhost][0])` (`:2128`);
  - the noise, peak and loop levers (`:2135-2174`).
- **`installIdentityNeutral()`** (`:2202-2213`) is `applyIdentityLanes(kNoLanes)` plus the gravity base.
- **`publishLifeLanes()`** (`:2230-2234`):
  - `breathGravityLane_ = breath_.getCurrentValue()`;
  - `resonance_.setGravity(clamp(gravityBase_ + breathGravityLane_, -1, 1))`;
  - `tidalFogDepth_ = max(0, tide_.getCurrentValue())`.

  Its doc (`:2214-2229`) records that both modulators already apply their own depth inside
  `getCurrentValue()`, so a lane is exactly 0 at depth 0.
- **`publishIdentity()`** (`:2257-2268`): gather → apply → `publishLifeLanes()` → event rate scale.
- **`updateSpectrumTarget()`** (`:2341-2385`):
  - `capacity = clamp(cloud_.getActivePartialCount(), 14, 64)`;
  - `parentCount_ = bloom_.reserveBase()`;
  - `p = kRichnessMinExponent + (kRichnessMaxExponent − kRichnessMinExponent) · cloudRichness_`;
  - for every `i < parentCount_`: `ratios_[i] = i + 1` and `amplitudes_[i] = exp2(−p · kHarmonicCloudLog2N[i])`
    (`:2363-2367`). This covers **every** parent slot, whatever the cloud's active count;
  - `count = bloom_.processChunk(...)`;
  - `wantTarget = getLiveChildCount() > 0` (`:2379`), then set the target, or clear it on the edge.
- **Render chunk order:** identity layer → `updateSpectrumTarget()` (step 2) → cloud (step 3)
  (`renderOneChunk`, `:2400-2420`). A richness change therefore reaches the cloud's active count one chunk
  after it reaches the voice's shadow.

### 1.2 `HarmonicCloud` — `systems/harmonic_cloud.h` (read-only, Seraphis-shared)

- `setRichness` (`:412-423`) rejects NaN/Inf, applies `clamp(r, 0, 1)`, early-outs on an equal value, and
  marks freq and amp dirty.
- `recalculateAmplitudes()` (`:1458-1508`):
  - `activeCount_ = clamp(int(std::round(std::pow(64.0f, richness_))), 1, 64)` (`:1462-1463`);
  - slots `≥ activeCount_` get `baseAmplitude_ = 0` (`:1471-1474`);
  - with a target, `baseAmplitude_[i] = targetAmp_[i] · tiltGain(i)`. **The target replaces the rolloff
    only; tilt still multiplies** (`:1484-1494`).
- The normaliser `currentNormGainTarget()` sums `baseAmplitude_[i]²` over `i < activeCount_`
  (`:1797-1807`). Zero slots contribute nothing.
- `richness_` drives only `activeCount_` and the exponent (grep of `richness_`: `:417-420`, `:490`, `:1462`,
  `:1468`). The drift amount uses the fixed 64 capacity, never `activeCount_` (`:1195-1199`).
- `setSpectralTarget` (`:769-857`):
  - wholesale rejection on null, `count == 0`, `count > 64`, non-finite, `ratio ≤ 0` or `amp < 0`;
  - a bit-identical early-out (FR-085 lever 1, `:776-812`);
  - slots `≥ count` are padded with `ratio i+1, amp 0` (`:825-826`).
- `clearSpectralTarget()` (`:862`) and `hasSpectralTarget()` (`:868`).
- The tail sweep zeroes `currentAmplitude_ < kTailSilenceThreshold` wherever the target is 0
  (`:1785-1789`).
- Read surface: `getActivePartialCount()` (`:950-952`) and `getPartialCurrentAmplitude(i)` (`:959-961`).
  Tilt bounds are `kMinTiltDbPerOct −12` / `+12` (`:194-195`).

### 1.3 `BloomEngine` — `systems/bloom_engine.h` (read-only; Vorago-only consumer)

- Constants: `kMaxChildren = 16` (`:209`), `kSilentParentAmplitude = 1e-5f`, `kDefaultChildGain = 0.35f`
  (`:259-260`), `kMaxSpawnRateHz = 0.05` (`:280`).
- Setters:
  - `setDepth` (`:532-538`) and `setSpawnRateHz` (`:543-548`);
  - `setParentCount` (`:551-553`) and `setChildrenPerEvent` (`:556-558`);
  - `setChildGain(float)`: finite check, then `clamp(0, 1)` (`:561-566`).
- Getters:
  - `getChildGain()` (`:687`), `capacity()` (`:701`), `reserveBase() = capacity_ − numChildSlots()`
    (`:710`), `getLiveChildCount()` (`:712`);
  - per child: `getChildSlotIndex(i)` (returns `kMaxSlots` for an Idle entry), `getChildParentIndex(i)`,
    `getChildAmplitude(i)`, `getChildTargetAmplitude(i)` and `getChildPhase(i)` (`:767-792`).
- The latched target is `parentAmp · childGain_ · smoothedDepth / tiltGain(slot)` (`:1335`).
- The parent scan covers `[0, min(pc, reserveBase()))` and skips any `a ≤ kSilentParentAmplitude`
  (`:1403-1418`).
- `applyOutput` (`:1534-1584`):
  - returns `parentCount` while not `engaged_`;
  - once engaged (**sticky**), pads `[pc, reserveBase())` and `[reserveBase(), capacity_)` with
    `ratio i+1, amp 0` (`:1566-1569`), writes the live children (`flushDenormal`, `:1580`), and returns
    `capacity_`.

### 1.4 `VoragoEngine` — `systems/vorago_engine.h` (Layer 3)

- Constants: `kMaxVoices = 6` (`:231`), `kOutputCeilingDb = -0.3f` (`:261`), `kGhostBurstPeak = 0.60f`
  (`:271`).
- Ghost-tap prepare: `atmos_.setDensity(0.30f)` (`:375`), grain 12 s, −12 st, spread 0.90, blur and decorr.
- The config fan-out pattern covers **all** `kMaxVoices` (`:800-830`, for example `setEnvelopeStageTimeMs`).
- `setGhostPeakLevel` (`:986-991`, clamp [0, 1]).
- Ghost-tap make-up: `kGhostTapMakeupDb = 12`, `kGhostTapMakeupGain`, `kMaxGhostTapMakeupDb = 24` and
  `setGhostTapMakeupDb` (`:1001-1010`). The doc says the setter "exists so the make-up can be MEASURED".
  The gain is applied at the bus sum (`:1194-1202`).
- The control step:
  - takes `ghost = max(getGhostRequest())` and `fog = max(getTidalFogDepth())` over the rendering voices
    (`:1522-1530`);
  - writes `atmos_.setLevel(ghostPeak_ * ghost)` (`:1545`);
  - writes `smear_.setSmearAmount(clamp(smearBase_ + fog, 0, 1))` (`:1599`).
- Friends: `VoragoMacroMatrix`, `detail::VoragoEngineNonFiniteProbe`, `VoragoEcosystemRuleProbe`,
  `VoragoEcosystemLeverProbe` (`:1338-1341`).
- `atmosphere()` returns a const `AtmosphereEngine&` (`:1333`).

### 1.5 Other read-only components

- **`AtmosphereEngine`:** `setDensity` takes `[kMinDensity 0.1, kMaxDensity 20]` grains/s and substitutes
  4.0 for a non-finite input (`atmosphere_engine.h:312-313`, `:867-870`). `setLevel` clamps to
  `[0, kMaxLevel 2]` (`:325`, `:1035-1038`).
- **`FeedbackEcology::setWetGain(float db)`:**
  - finite check, `clamp(kMinWetGainDb −24, kMaxWetGainDb +24)`, linear ramp (`feedback_ecology.h:583-585`,
    `:1319-1325`);
  - applied per sample as `wetTrim` (`:2122`); read back in dB by `getWetGain()` (`:1350`);
  - `prepare()` snaps it (`:787-788`) and `reset()` keeps it (`:878`).
- **`MultiStageEnvelope`** (`processors/multi_stage_envelope.h`):
  - Linear is a phase ramp;
  - Logarithmic (rising) is `start + (target − start)·phase²` (`:301-322`);
  - Exponential is the EarLevel one-pole with `kDefaultTargetRatioA = 0.3` (`:323-329`, `:387-396`;
    `envelope_utils.h:54`).

  All three map from `stageStartLevel_`, so a Legato retrigger from a non-zero level stays continuous.
- `enterStage` latches `stageStartLevel_ = output_` (`:370-373`). A stage snaps to its target level when it
  completes (`:335-337`). Read surface: `getState()`, `getOutput()` and `getCurrentStage()` (`:276-280`).
- **`EnvCurve { Exponential, Linear, Logarithmic }`** (`core/env_curve.h:23-27`).

### 1.6 `VoragoMacroMatrix` — `systems/vorago_macro_matrix.h`

- `struct VoragoMacroRow { macro, owner, target, base, amount, curve }` (`:233-257`).
- `kNumRows = 50` (`:276`), with the per-macro count comment (`:273-275`); `kRows` runs `:315-756`.
- `contributionOf` (`:1160-1167`): Gravity is bipolar, `g = (m − 0.5)·2`, contributing
  `amount · curve(|g|) · sign(g)`.
- `evaluateAll` (`:1177-1189`): one base per target (or the parameter override) plus every row's
  contribution; the setters clamp downstream.
- Guards (`:1215-1238`):
  - `kRows.size() == kNumRows`;
  - owner validity and the Cavern POD field;
  - no Stepped curve;
  - every target claimed and one base per target;
  - `VoragoMacroTarget::Count == 41`, and 12 macros.
- The rows in scope were re-read this session:
  - Age `:342-359`;
  - Gravity `:439-455`;
  - Fog `:571-621`. The Fog→CloudSpectralTiltDb row at `:610-615` is **amount 0 by the 2026-09-19 ruling**
    (`:603-609`: a steeper tilt collapses SweepAxes' Fog flatness metric);
  - Life `:630-653`;
  - Mass `:706-756`.

### 1.7 Phase 14 harness — `plugins/vorago/tests/`

**`preset_test_support.h`**

- **Constants and shapes:**
  - `kFloorF 4.0` and `kSecondaryBar 1.5` (`:249-250`); `kRuledTakes = 4` (`:265`); `takeSeedIndex`
    (`:271`);
  - `RenderSpec` / `SweepCapture` (`:284-303`);
  - `kAttribMargin`, `ClaimRole` and `CellOutcome` (`:943-959`);
  - `TakeRecord` (`:1288-1309`);
  - `kSweepPeakCeiling 0.9661`, `kLateLoDb −18`, `kLateHiDb 12`, `kAttackReachBelowSusDb 6` (`:1730-1740`);
  - `kGestureAfterAttackSeconds 65` and `kRate441` (`:2753-2754`).
- **Rendering:**
  - `renderPreset` (`:317-438`): prepares the `PresetHost`, calls `loadState`, sends the block-0 params and
    the NoteOn;
  - `renderTake` (`:2013-2059`): computes the attack descriptor inside, from capture 3;
  - `computeTakes` (`:2065-2083`);
  - `sustainSpec` (`:2822-2836`).
- **Scoring:**
  - `detail::renderScored` (`:970-978`);
  - `verifiedAt(o, Verification, role)` (`:987-1009`) and its Capability overload (`:1019-1041`);
  - `computeVerificationVector` (`:2510-2713`), with attack scoring at `:2577-2600` and `:2683-2708`;
  - `scoreRateArm1` (`:2841-2851`); the sweep's rate renders sit at `:3176` and `:3192-3194`.
- **Attack window:**
  - `audibleAttackSeconds` (`:846-853`);
  - `firstSecondAtOrAbove` (`:1799-1817`, in 1 s blocks);
  - `revertedAttackSpanSeconds` (`:2372-2384`);
  - `attackWindowEndSeconds` (`:2389-2405`): `max(A_P, A_rev) + 5`, with A = `audibleAttackSeconds`.
- **Overrides:**
  - `twinOverrides` (`:2190-2240`): D9.1 reverts stages 0–3 to `kDefaultStageTimesMs`;
  - `routeOverrides` (`:2248`).
- `sweepEnv` (`:441`).

**`preset_pilot_test.cpp`**

- `planPrimary`'s attack branch (`:446-470`).
- `scoreTwin`'s attack scoring and `printReach` (`:530-552`).
- `Vorago_PresetPilot_PrimaryProbe` (`:845-997`):
  - the override parser (`:856-886`);
  - `computeTakes(..., 1, ...)` (`:892-895`);
  - the route arms (`:900-934`);
  - the stored-take arm line (`:949-959`);
  - the verdict line (`:991-995`).

**Host, sweep and defs**

- `vorago_preset_host.h:47-140` (`PresetHost`) owns a `Vorago::Processor`. The processor exposes only
  `const VoragoEngine* engineForTest()` (`processor.h:118`), over a non-const `engine_`. `loadState` already
  carries a `NOLINT(cppcoreguidelines-pro-type-const-cast)`.
- `preset_sweep_test.cpp`:
  - `t041ShardIndices` (`:978-994`; index N is the default surface);
  - `Vorago_PresetSweep_AblationVerifiesClaims` (`:1124`);
  - the shard env `VORAGO_SWEEP_SHARD` (`:336`; `preset_test_support.h:1709`).
- `tools/vorago_preset_defs.h`:
  - `Capability` (`:53-76`) and `Verification` (`:81-90`);
  - the D13/D14 specs (`:252-261`);
  - `kRecordedDefaultStateCells` (`:274-280`) and `requiredPrimaryCells()` (`:297-322`).
- `plugins/vorago/src/parameters/global_params.h:41`, `:65-67`: master gain is linear `[0, 2]`; normalized
  0.5 is unity, and linear = `2 · norm`.
- `kResonanceGravityId = 400` is a macro-base route (`param_routes.h:90`, `:189`; the mapping case is at
  `resonance_params.h:106`).

### 1.8 Roster richness — which presets the bloom decoupling touches

N(r) = `round(64^r)`, by hand:

| r | 0.35 | 0.40 | 0.45 | 0.50 | 0.55 | 0.6346 | 0.70 |
|---|---|---|---|---|---|---|---|
| 64^r | 4.29 | 5.28 | 6.50 | 8.00 | 9.85 | 14.002 | 18.4 |
| N(r) | 4 | 5 | 6 or 7* | 8 | 10 | 14 | 18 |

\* The r = 0.45 case sits on a rounding edge, so the implementer prints the cloud's own count rather than
trusting the hand figure.

- **Roster presets below the floor:**
  - Feedback Mire 0.45 (`vorago_preset_defs.h:470`);
  - Stone Gravity 0.50 (`:772`);
  - Bloom Colony 0.40 (`:993`);
  - Swarm Breath 0.35 (`:1061`);
  - Feeding Loops, Haunted Colony and Sudden Chasm 0.55 (`:1088`, `:1118`, `:1473`).
- **Above the floor:** Slow Bloom (0.70, `:522`) and the default surface (0.70). The decoupling does not
  touch the default render.

---

## 2. Component design (production edits)

All edits are Layer 3 (`systems/`), header-only, `noexcept` and allocation-free. No include changes.

### 2.1 Bloom lever (FR-011; S6, E1; feeds M3, M10) — `vorago_voice.h`

The change set is one lever-table row (Q5) with two parts: (a) active-count decoupling, always on, and
(b) `kBloomChildGain`.

#### (a) Decoupling the cloud's active count from the user's richness

```cpp
// public constants, next to the bloom slot budget (:346-355)
/// FR-011 / Q2. The cloud's richness floor: the smallest round richness at which
/// HarmonicCloud's N(r) = round(64^r) reaches kMinCloudCapacity (14).
/// 64^0.6346 = 14.002 (inside (13.5, 14.5) with margin); pinned by
/// VoragoVoice_CloudRichnessFloor against the component's own count.
static constexpr float kCloudRichnessFloor = 0.6346f;

void setRichness(float r) noexcept;            // body below
[[nodiscard]] float getRichness() const noexcept { return cloudRichness_; }  // the USER value
```

- **`setRichness(float r)`:**
  - if `detail::isNaN(r) || detail::isInf(r)`, return. This mirrors `harmonic_cloud.h:413-415` exactly, so
    the shadow and the component reject the same inputs;
  - `cloudRichness_ = clamp(r, 0, 1)`;
  - `cloud_.setRichness(std::max(cloudRichness_, kCloudRichnessFloor))`.

  The cloud's own early-out (`:417-419`) keeps an unchanged write free. `getRichness()` returns the shadow,
  so the setter-contract table (`vorago_voice_test.cpp:3231`) and every macro readback still read the user
  value.
- **New private helper**, the cloud's expression character for character (`harmonic_cloud.h:1462-1463`):
  ```cpp
  [[nodiscard]] static std::size_t partialCountFor(float r) noexcept {
      const float rounded = std::round(std::pow(static_cast<float>(HarmonicCloud::kMaxPartials), r));
      return static_cast<std::size_t>(std::clamp(static_cast<int>(rounded), 1,
                                                 static_cast<int>(HarmonicCloud::kMaxPartials)));
  }
  ```
- **`updateSpectrumTarget()`** becomes the following. New lines are marked ▲; everything else is unchanged.
  ```cpp
  const std::size_t active = cloud_.getActivePartialCount();              // >= 14 from the floor
  const std::size_t capacity = std::clamp(active, kMinCloudCapacity, HarmonicCloud::kMaxPartials);
  if (capacity != bloom_.capacity()) bloom_.setCapacity(capacity);
  parentCount_ = bloom_.reserveBase();
  const std::size_t userCount = partialCountFor(cloudRichness_);           // ▲ the USER's N(r)
  const float p = ...cloudRichness_...;                                    // unchanged
  for (std::size_t i = 0; i < parentCount_; ++i) {
      ratios_[i] = static_cast<float>(i + 1);
      amplitudes_[i] = (i < userCount)                                      // ▲ zero above N(r_user)
          ? std::exp2(-p * detail::kHarmonicCloudLog2N[i]) : 0.0f;
  }
  std::size_t count = bloom_.processChunk(ratios_.data(), amplitudes_.data(), parentCount_,
                                          kControlChunkSamples);
  const std::size_t live = bloom_.getLiveChildCount();
  if (live == 0u && count < userCount) {                                    // ▲ no child owns
      for (std::size_t i = count; i < userCount; ++i) {                     //   [count, userCount):
          ratios_[i] = static_cast<float>(i + 1);                           //   the user's own
          amplitudes_[i] = std::exp2(-p * detail::kHarmonicCloudLog2N[i]);  //   partials, natural law
      }
      count = userCount;
  }
  const bool wantTarget = (live > 0u) || (userCount < active);              // ▲
  ```
  The set/clear/latch tail (`:2380-2385`) is unchanged.

**How this behaves across the richness range:**

| User r | N_user | Cloud active | reserveBase | Target engaged? | Children sound? |
|---|---|---|---|---|---|
| ≥ 0.6346 | ≥ 14 | N_user | N_user − 6 | only while a child lives (today's path) | yes (`[N−6, N) < N`) |
| 0.55–0.63 | 9–13 | 14 | 8 | always (`N_user < 14`) | yes (`[8, 14) < 14`) |
| < 0.55 | 1–8 | 14 | 8 | always | yes |

**Why it satisfies FR-011 at every richness:**

- **Children sound** (SC-008 (a)). Every child slot lies in `[reserveBase, capacity) ⊆ [0, active)`, so
  `harmonic_cloud.h:1471-1474` never zeroes it.
- **Parents keep their ratio to slot 0** (SC-008 (b)).
  - Slots `< userCount` receive the user's `exp2(−p log2 n)`, with p from the user richness. That is exactly
    what the untargeted cloud at r_user computes (`:1484-1494`: the target replaces the rolloff, tilt still
    multiplies).
  - The normaliser is one scalar for all slots (`:1797-1807`), so it does not change the ratios to slot 0.
  - Its absolute drop, driven by audible children, is accepted and logged (Q1).
- **Children attach to sounding parents.** Slots `[userCount, parentCount_)` reach the bloom as 0, so the
  strongest-K scan skips them (`bloom_engine.h:1415`).

**What stays the same:**

- **At r ≥ 0.6346 with no live child.** The cloud's richness equals the user's richness and the target is
  cleared as today. At the default surface (0.70) the render follows the base tree's path; the target path
  is entered only when a child lives, as before.
- **The `live == 0` extension.** It covers both bloom states in which no child lives:
  - the never-engaged pass-through (`count == parentCount_`);
  - the sticky engaged return (`count == capacity`), where every slot in `[reserveBase, capacity)` is a pad
    zero (`bloom_engine.h:1566-1569`).

  While a child lives, slots `[reserveBase, userCount)` stay the bloom's. This is today's B-1 rule,
  "neutrality is a parent-region claim" (`vorago_voice.h:2304-2311`), unchanged.
- **`setSpectralTarget` acceptance (E-8).** `count` stays in `[8, 64]`. Every slot below `count` is written
  this chunk with `ratio = i+1 ≥ 1` and a finite amplitude ≥ 0, so the four conditions restated at
  `:2319-2340` still hold. `processChunk` still receives the two 64-entry arrays (`bloom_engine.h:467-481`).

**One-chunk lag.** The cloud's count moves one chunk after `setRichness` (§1.1).
- Crossing the floor downward: the old active count is ≥ 14 and above `userCount`, so the target engages on
  the first chunk.
- Crossing upward: `userCount ≥ 14`, so the target clears through the cloud's smoother (`:862`).

The existing click-sentinel pattern (`VoragoVoice_SpectralTargetEdge`, `vorago_voice_test.cpp:1650`) covers
both edges.

**CPU.** Below the floor the kernel runs 14 lanes instead of N_user, and `setSpectralTarget` runs every
chunk. Its bit-identical early-out makes a steady target cost one `memcmp` (`harmonic_cloud.h:776-812`). The
new hidden `VoragoVoice_CloudFloorCpuProbe` (§5) measures the cost, and it is recorded in SC-014's delta (Q2).
The CPU budget tests run the default surface (r = 0.70), which this change does not touch.

**Rejected alternative (recorded).** Keep the target engaged at every richness, reading "no switching path"
as "no target edge". Behaviour at r ≥ 0.6346 would be the same, but:
- it costs the target path on every voice at the default surface;
- it breaks the four `hasSpectralTarget()`-false assertions of the edge tests (`vorago_voice_test.cpp:1551`,
  `:1607`, `:1671`, `:1702`);
- it gains nothing audible.

The **active-count floor** is the unconditional part, which is what Q2 rules. §10 lists this reading for the
user to confirm.

#### (b) Child gain

```cpp
/// FR-011 child gain. Ruled value from the lever ladder (§7 step L1).
static constexpr float kBloomChildGain = 0.35f;  // == BloomEngine::kDefaultChildGain until ruled
```
- It is written once in `prepare()` step 5, next to `setBloomDepth(0.60f)` (`:617`):
  `bloom_.setChildGain(kBloomChildGain);`.
- Each candidate is a rebuild of this constant (§0 item 4). Tests read the installed value through the
  existing `bloom().getChildGain()` (`vorago_voice.h:1569`, `bloom_engine.h:687`). No setter is added unless
  §2.9's optional route is adopted.
- **Ceiling.** The component clamps the gain at 1.0, a child as loud as its parent before tilt. If 1.0 does
  not clear S6/E1, that is a stop-and-surface item. The options are `setChildrenPerEvent` (up to 4) and
  `setParentCount`. An append-only higher-ceiling setter in `bloom_engine.h` is allowed by FR-019, but only on
  that ruling.

### 2.2 Ecosystem route levers (FR-012; E1, E3, E4, E5; feeds E7.hi, D13.x, M10, see §2.10) — `vorago_voice.h`

These are measured on the tree with §2.1 landed. The 13b invariants hold by construction:
- every new term is `gain × lanes.eco[k][s]`, so it is exactly 0 when the lane is 0 (13b FR-017);
- no new term reads `lanes.sched` (FR-019);
- the coupling lever still writes only `(l, (l+1) % n)` (Q8).

| Cell (preset) | Lever (constant) | Today | Candidate ladder | Headroom note |
|---|---|---|---|---|
| E3 (Swarm Breath; noise base −6 dB, `:1048`) | `kNoiseLevelLeverSpanDb` | 12 | 15, 18 | `writeNoiseLevel` clamps at +12 dB (`:1691`), so a span above 18 is inert on this preset. A lower noise base is a **level trim** the override string may carry (FR-025) |
| E4 (Feeding Loops; loop gain 0.54, `:1082`) | `kLoopGainLeverSpan`, then `kCouplingLeverSpan` | 0.18, 0.30 | 0.24, 0.30; then 0.38, 0.44 | `kMaxLoopGain 0.90`, `kMaxCouplingPerPair 0.5`, row sum ≤ 0.95 (`feedback_ecology.h:264-272`) |
| E1 (Bloom Colony) | new `kPartialLaneGain`, a size on the two existing Partial-lane writes (`:2124-2126`) | 1 (implicit) | 1.5, 2, 3 | applied as `cloud_.setMutation(clamp(mutationBase_ + g·partialEco))` and `bloom_.setDepth(clamp(bloomDepthBase_ + g·partialEco))`. No new destination. A spawn-rate destination is **not** in this ladder: it would be a new route, outside FR-012's "size of 13b's route levers". It goes to the user as a scope extension (§10.3) |
| E5 (Haunted Colony) | new `kGhostLaneGain` | none (1) | 1.5, 2 | `ghostRequest_ = combineWake(0, min(1, kGhostLaneGain · eco), sched)`; the sched term is untouched |

- **E1 is measured first with no route change**, because the bloom lever alone may lift it. Bloom Colony read
  `d = attribBase = 0.0568` before (spec roster). The ladder follows only if it is still short.
- **Every L2 rung also reads the ecosystem-limited secondaries** (E7.hi, D13.1 with Teeming's S7 conjunct,
  D13.2), as §2.10 sets out.
- **`kLeverInputGain` and its `static_assert` (`:1665-1670`) are not edited.** The two new constants act on
  the Partial and Ghost writes that FR-012 names (`:2124-2128`), not on `shapeLeverInput`.
- **Clearing paths need no extra code.** `installIdentityNeutral()` runs `applyIdentityLanes(kNoLanes)`
  (`:2202-2213`), so every new term is written at its base on every clearing path. No voice member is added.

### 2.3 Ghost lever (FR-013; S9, E5) — `vorago_engine.h`

- **Candidate 1, level:** `kGhostTapMakeupDb` 12 → 15, 18, 21, 24 (24 is the setter's ceiling, `:1003`).
  - `kGhostTapMakeupGain` is a hand literal (`:1002`). The edit writes both literals as a pair, and the
    comment cites the arithmetic `10^(dB/20)`; there is no constexpr `pow`.
  - `VoragoEngine_GhostLevelProbe` prints `getGhostTapMakeupDb()` and the gain so that a mismatched pair
    is visible.
- **Candidate 2, density**, only "where the make-up alone is short":
  - the literal at `:375` becomes `static constexpr float kGhostDensity = 0.30f;`, with the ladder 0.45, 0.60
    and 1.0 grains/s;
  - `prepare()` installs it as `atmos_.setDensity(kGhostDensity)`. Each rung is a rebuild. Tests read it
    through the existing `atmosphere().getDensity()` (`vorago_engine.h:1333`). A `setGhostDensity` seam
    exists only under §2.9's optional route.
- **Sizing record (FR-013).** `VoragoEngine_GhostLevelProbe` (`vorago_engine_test.cpp:3215`) is re-run at the
  ruled values and records ghost alone vs drone, the loudest second and the seconds sounding.

### 2.4 Feedback ecology (FR-017; S4) — `vorago_voice.h`

The ecology wet path sits about 30 dB down by voicing (roadmap 276). `FeedbackEcology::setWetGain` exists,
but the voice never calls it.

- **The lever:** `static constexpr float kEcologyWetMakeupDb = 0.0f;`, ruled from the ladder 6, 12, 18, 24
  (24 is the component ceiling, `feedback_ecology.h:585`).
  - It is installed in `prepare()` beside `setEcologyMix` (`:604`) as
    `ecology_.setWetGain(kEcologyWetMakeupDb);`.
  - Each rung is a rebuild. Tests read it through `ecology().getWetGain()` (`vorago_voice.h:1568`,
    `feedback_ecology.h:1350`). A `setEcologyWetMakeupDb` seam exists only under §2.9's optional route.
- **Second candidates, only if the make-up is short.** `kLoopWakeBase` is **not** used, because 13b Gate 1
  rests on it. `kLoopGainLeverSpan` is already a route lever (§2.2).
- **Arm 2** (non-silence) on Feedback Mire is part of every reading (FR-017, E-3).

### 2.5 Attack (FR-016, option (b); D9.1, guards D8.2) — `vorago_voice.h`

FR-016 (b) asks for two things. First, the measured first reach of RMS(Sus) − 6 dB must **track** the
registered 20 s stage-0 time, so the stage is "audibly ~20 s". Second, D9.1 must clear its gate (SC-004).
`MultiStageEnvelope` is read-only and offers three curves (`env_curve.h:23-27`). None of them meets the first
part (table below), so the reshape is a voice-side power applied to a Linear stage 0.

```cpp
/// FR-016 option (b): stage 0 runs Linear, and the voice raises its phase to kAttackShapePower so the
/// audible attack tracks the registered stage-0 time. Stages 1..5 keep kStageCurve.
static constexpr EnvCurve kAttackStageCurve = EnvCurve::Linear;
static constexpr int kAttackShapePower = 6;   // ladder 4, 5, 6, 7; n = 1 and n = 2 are measured rungs only
[[nodiscard]] static constexpr EnvCurve stageCurveFor(int st) noexcept {
    return (st == 0) ? kAttackStageCurve : kStageCurve;
}
// private member: float attackStartLevel_ = 0.0f;   // the envelope's level at stage-0 entry
```

- **Both writers use `stageCurveFor`:** `setEnvelopeMode` (`:1061`) and `applyStage` (`:1880`, `:1882`).
- **Latching the start level.** In `noteOn`, before `mse_.gate(true)` (`:944`):
  `if (mse_.getState() == MultiStageEnvState::Idle) attackStartLevel_ = mse_.getOutput();`. This is the value
  `enterStage(0)` latches as `stageStartLevel_` (`multi_stage_envelope.h:370-373`). On this voice, Idle is the
  only way into stage 0 (§1.1). Wherever `mse_.reset()` runs (`:1797`), `attackStartLevel_` is set to 0.
- **The shaping** sits in step 5's Standard loop (`:2449`):
  ```cpp
  float e = mse_.process();
  if (mse_.getCurrentStage() == 0 && mse_.getState() == MultiStageEnvState::Running)
      e = shapeAttack(e);   // e0 + span·u^n, u = clamp((e − e0) / span, 0, 1), span = stageLevel_[0] − e0
  const float g = velocity_ * e;
  ```
  - `shapeAttack` returns `e` unchanged when `span ≤ 1e-6f`, which covers a non-rising stage 0. It computes
    `u^n` by repeated multiplication, with no `pow` per sample.
  - At u = 0 it returns e0, the level at entry. At u = 1 it returns the stage-0 level, which is where the
    envelope snaps on completion (`multi_stage_envelope.h:335-337`). The output is therefore continuous at
    both ends.
  - Growth mode zeroes stage 0's time (`:1882`), so the shaping is inert there. The Growth loop (`:2442`) is
    not edited.
- **The `kStageCurve` doc is rewritten.** It currently says "The ONE curve every setStage() call uses"
  (`:304`). The new text states the stage-0 exception and cites FR-016.
- **Unaffected:** `vorago_perf_test.cpp:588` passes `kStageCurve` for a perf fixture.
- **Size and CPU.** One float member (+4 B; `kVoiceSizeBound`, §8). The per-sample cost applies during stage 0
  only: two getters, one divide and n − 1 multiplies.

**Predicted reach.** These figures are for the envelope alone. Stage 0 rises from 0 to 1.0 over T = 20 s. The
Sus level is 0.85, so RMS(Sus) − 6 dB corresponds to an envelope of 0.85 · 0.501 = 0.426.

| Shape | Reach formula | Reach at T = 20 s | Clears the 0.75 T tracking floor (envelope only)? |
|---|---|---|---|
| Exponential (today) | reference `1.3(1 − (0.3/1.3)^{t/T})` → t ≈ 0.27 T | 5.4 s (measured 6 s, `compliance.md:518`) | no |
| Linear (n = 1) | t = 0.426 T | 8.5 s | no |
| Logarithmic, phase² (≡ Linear with n = 2) | t = √0.426 · T = 0.653 T | 13.1 s | no |
| Linear, n = 4 | t = 0.426^{1/4} T = 0.808 T | 16.2 s | yes |
| Linear, n = 5 | 0.843 T | 16.9 s | yes |
| Linear, n = 6 | 0.867 T | 17.3 s | yes |
| Linear, n = 7 | 0.885 T | 17.7 s | yes |

- **The tracking criterion.** This is the plan's reading of FR-016's "audibly ~20 s". On the probe, the
  measured `P_rev` reach must lie in `[0.75 · T0, T0]`. The reach is the `printReach("P_rev", …)` line
  (`preset_test_support.h:2705-2708`; pilot `:549-552`). T0 is the registered stage-0 time that
  `audibleAttackSeconds` reads: 20 s on the default. The resonance, ecology, body and cavern tails pull the
  output's reach earlier than the envelope's, so only the measured reading rules. The proposed n is the
  smallest rung whose measured reach clears 0.75 T0 **and** whose D9.1 reading clears SC-004. The 0.75 figure
  is this plan's own, and it goes to the user at §10.5. A per-push voice test asserts the same bound at voice
  level (`VoragoVoice_AttackTracksStageTime`, §5).
- **Linear and Logarithmic stay as measured rungs** (n = 1 and n = 2). The lever table then shows why they do
  not ship: they may clear D9.1's distance, but they fail the tracking clause.
- **If no rung reaches 0.75 T0 on the probe**, FR-016's tracking clause becomes a stop-and-surface item
  (FR-027) with the reach readings. The plan does not claim it met.
- **Sudden Chasm.** Its stage 0 is 0.5 s (`vorago_preset_defs.h:1466`). Its reach (4 s today) stays dominated
  by the downstream sections under every shape.
- **D8.2** is re-read at L5 as compiled, on the window actually used (FR-016, FR-030).

**Option (a), measured only (FR-016).**
- On a scratch build, the stage-0 default moves from 20 s to 40 s: `kDefaultStageTimesMs[0]`, with the
  `envelope_params.h:56-62` default following it.
- Expected direction, stated before measuring: `P_rev`'s reach moves from about 6 s to about 11 s
  (0.27 · 40), away from Sudden Chasm's 4 s, so `d_att` should **rise**.
- The reading and this prediction both go in the lever table. Option (a) is not shipped (Q4).

### 2.6 Life modulators (FR-017b; D14.1 Slow Bloom, D14.2 Drifting Strata) — `vorago_voice.h`

```cpp
static constexpr float kBreathGravityLaneGain = 1.0f;  // ladder 1.5, 2, 3
static constexpr float kTidalFogLaneGain = 1.0f;       // ladder 1.5, 2, 3
// publishLifeLanes():
breathGravityLane_ = kBreathGravityLaneGain * breath_.getCurrentValue();
resonance_.setGravity(std::clamp(gravityBase_ + breathGravityLane_, -1.0f, 1.0f));
tidalFogDepth_ = std::clamp(kTidalFogLaneGain * tide_.getCurrentValue(), 0.0f, 1.0f);
```

- **Depth 0 still means a true "off".** At depth 0, `getCurrentValue()` is 0 (`:2214-2229`), so each lane is
  exactly 0 and the reversion twin is a true "off" (FR-017b).
- `getBreathingGravityLane()` (`:1022`) keeps publishing the applied lane. That lane is
  `kBreathGravityLaneGain ×` the modulator value and is **not** clamped; only the gravity sum is. So for a ruled
  gain above 1 the lane's bound is `kBreathGravityLaneGain`, not 1. Two tests depend on this (§5, FR-034
  list).
- **Second destination, only if a ladder tops out.** Gravity saturates at ±1, and Slow Bloom's resonance mix
  is 0.25 (`:531`), so the breathing ladder may top out.
  - The fallback is an excitation swell, breathing → `g *= 1 + kBreathSwellDepth · breathLane`, interpolated
    linearly across the chunk in step 5 (`:2447-2453`).
  - `kBreathSwellDepth ≤ 0.3` (±2.3 dB), so arm 3 and the zipper bound are not disturbed.
  - It is a proposal, not pre-built. It is measured only at that stop-and-surface.

### 2.7 Macro rows (FR-014; M2, M3, M4, M5, M9, M10, M12) — `vorago_macro_matrix.h`

Each macro gets one lever-table row (Q5).
- A widening edits `amount`.
- A new target appends a row that uses the target's existing base (`everyRowSharesOneBasePerTarget`).
- Every appended row increments `kNumRows` and the count comment (`:273-275`).
- `VoragoMacroTarget::Count` stays 41: no new target is added (`:1232-1233`).

The table gives the candidate order per macro; the first rung is the smallest change. Each rung is measured
on the macro's showcase preset with the macro-reset ablation.

| Macro (preset) | Before | Rung 1 | Rung 2 | Rung 3 |
|---|---|---|---|---|
| M2 Age (Erosion) | 2.45 | Age→CloudSpectralTiltDb −4 → −7 | Age→BodyDamping 0.55 → 0.70 | new Age→CloudMutation (base 0.15, +0.40) |
| M3 Density (Crowded Dark) | 3.92 | re-read after bloom (its BloomDepth row rides on §2.1) | Density→NoiseLevelDb +6 → +12 | new Density→EcologyMix (base 0.15, +0.30) |
| M4 Movement (Drifting Strata) | 3.00 | Movement→CavernDamperDepth +0.45 → +0.60 | new Movement→TidalDepth (base 0.40, +0.30) | new Movement→CloudMutation (base 0.15, +0.25). Drift (+42) already reaches kMaxDriftCents, and the wander rates are 13b's zipper axis |
| M5 Gravity (Stone Gravity) | 3.53, plus arms | §2.8 cause table first | new Gravity→CloudSpectralTiltDb (base −4, −3; bipolar: stone darker, air brighter) | Gravity→ResonanceMix (base 0.45, +0.25) |
| M9 Fog (Fogbound) | 1.82 | Fog→CavernFog +0.55 → +0.70 | new Fog→SmearDecoherence (its existing base, +0.40) | Fog→SmearAmount +0.70 → +0.80 (to the 1.0 clamp) |
| M12 Mass (Monolith) | 3.37 | Mass→SubToneLevelOffsetDb +3 → +6 | new Mass→BodyDamping (base 0.25, −0.15: more ring) | Mass→BodyResonance +0.28 → +0.29 (to the 0.99 clamp) |
| M10 Life (Teeming), **last** | 0.38 | re-read after every other lever | Life→BloomSpawnRateHz +0.0208 → +0.0458 (reaches kMaxSpawnRateHz) | new Life→BreathingDepth (base 0.30, +0.40) |

- **The Fog→CloudSpectralTiltDb row stays at amount 0.** Widening it would reverse the 2026-09-19 ruling
  (`vorago_macro_matrix.h:603-609`) and collapse SweepAxes' Fog flatness clause.
- **Clamps.** The setters clamp shared-target sums (for example tilt to `[−12, +12]`,
  `harmonic_cloud.h:194-195`). A rung that only lands in a clamp at the showcase preset is reported as such,
  not ruled.
- **Rebuilds.** Every rung is a rebuild: `dsp_systems_tests` for the row tests, `vorago_tests` for the probe.
- **FR-066 neutral identity** follows from the arithmetic for appended rows (`applyModCurve(c, 0) = 0`,
  `:1170-1176`). `VoragoMacro_NeutralIsIdentity` (`vorago_macro_test.cpp:1419`) proves it on every build.
- **E-7.** Every Movement and Gravity rung is gated on `VoragoMacro_NoZipper` (≤ 1.5×) and
  `VoragoMacro_SweepAxes` before it is ruled. 13b measured Movement as the zipper-critical axis
  (`vorago_voice.h:1619-1624`).

### 2.8 M5 self-growth cause (FR-015): instrument, then lever

Each ablation arm runs on Stone Gravity at Gravity 1.0, as a pilot probe run that prints the all-take arm-3
line (§3):

| Arm | How it is expressed | What stays on |
|---|---|---|
| A0 as compiled | none | both rows + breathing |
| A1 OctaveLock row off | `104=0.5` (Gravity neutral, both rows at 0) + `400=<norm for +1.0>` (the ResonanceGravity base, a macro-base route, `param_routes.h:189`) | ResonanceGravity's effect only + breathing |
| A2 ResonanceGravity row off | scratch rebuild with that row's `amount = 0` (logged, never shipped) | OctaveLock + breathing |
| A3 breathing lane off | `1500=0.0` (`kLifeBreathingDepthId`) | both rows |
| A4 all off | `104=0.5` + `1500=0.0` | none |

- **Arm A1's normalized value.** The value for +1.0 on parameter 400 is read from that parameter's mapping
  (`resonance_params.h:106`) when the arm is built, and printed in the log.
- **Takes.** The cause table and **every M5 rung at L7** run with `VORAGO_PILOT_TAKES=4` (§3.1). SC-005's gate
  spans all K = 4 takes, and its arm-1 red is on take 3, so a stored-take ruling could not see it (FR-010:
  measured before it is ruled). The 44.1 kHz arm-1 line prints on every run (§3.1). M5 is the one ladder that
  uses the all-take readout (§8).
- **Output.** `artifacts/m5_cause.log` holds a table of arm 3 (late − Sus, dB) and arm 1 for each arm, on all K
  takes.
- **The M5 lever** (one row, Q5) is the macro rung from §2.7 **plus** whichever row or lane change the table
  names, for example the OctaveLock row's curve going Linear → SCurve, or a smaller amount.
- **If no single ablation** brings arm 3 inside [−18, +12] dB, the table is surfaced with the proposal
  (FR-015).

### 2.9 Measurement seams — ADOPTED (ruling P2, 2026-10-01; tasks Group 3b)

FR-010 names two measurement routes: `VORAGO_PILOT_OVERRIDE` where a parameter reaches the lever, and a rebuilt
binary where the lever is a constant. FR-002 allows the probe reporting changes only. The route below is a
third route, and it changes renders. So it is **not** built by default. Every candidate in §7 is a rebuild.
The route is built only if the user accepts it at §10.2 **and** the spec gains it as an FR-010 / FR-002
amendment. If both happen, it is described here so the build stage does not re-derive it.

**Engine fan-outs.** These follow the `:800-830` pattern, over all `kMaxVoices`. Each is documented as
existing "so the lever can be MEASURED", after `setGhostTapMakeupDb` (`:998-1000`).
- `setBloomChildGain(float)` / `getBloomChildGain()` (the getter reads voice 0);
- `setEcologyWetMakeupDb(float)` / `getEcologyWetMakeupDb()`;
- `setGhostDensity(float)` / `getGhostDensity()`. It rejects non-finite input itself (FR-071 style), so the
  component's 4.0 substitute is never reached.

**Voice side.** `setBloomChildGain`, `getBloomChildGain`, `setEcologyWetMakeupDb` and
`getEcologyWetMakeupDb` are bare forwards to `bloom_` / `ecology_`. The owners reject non-finite input
(`bloom_engine.h:562-564`, `feedback_ecology.h:1321-1322`; E-13). No voice member is added.

**PresetHost tweak** (tests only).
- `RenderSpec` gains `std::function<void(Krate::DSP::VoragoEngine&)> engineTweak;`.
- `SweepCapture` gains `bool tweakHeld = true;`.
- `renderPreset` applies the tweak after `host.loadState`, then re-reads the getters after block 0 and sets
  `tweakHeld = false` on a mismatch.
- `PresetHost` gains `Krate::DSP::VoragoEngine* engineForTweak()`, defined as a `const_cast` of
  `engineForTest()`. This is well-defined: `engine_` is a non-const `unique_ptr` member. It carries a NOLINT
  with the reason, after the `loadState` precedent.
- The probe REQUIREs `tweakHeld` for every capture.

**`VORAGO_PILOT_LEVER`.** The value has the form
`"childGain=0.7,ghostTapDb=18,ghostDensity=0.6,ecologyWetDb=12"`.
- The probe TU parses it into an `engineTweak` attached to every `RenderSpec` it builds: takes, twins, route
  arms, the 44.1 kHz render and the −6 dB re-read.
- Unknown keys fail the run.
- No gate reading is ever taken with a lever env set; FR-026 reads the compiled constants.

### 2.10 Ecosystem-limited secondaries (FR-001, FR-023, SC-006: E7.hi, D13.1, D13.2)

These three cells have no lever of their own. Phase 14 records E7.hi as "an ecosystem-limited cell for 13c"
and D13.x as "claimed only as secondaries on presets whose conjunct fails" (`compliance.md:584-587`). The plan
targets them through FR-012's route sizes and FR-014's Life rows. It reads them at every rung that can move
them, and surfaces them if those levers do not reach.

| Cell | Host (def) | Verification and conjunct | Before reading (I1) | Expected lever(s) |
|---|---|---|---|---|
| E7.hi selfAffinity high | Colony Pulse (`vorago_preset_defs.h:537-546`, 902 = 1.0) | ExtReversion; no conjunct. Sweep 4: "is no preset's verified secondary claim" (`sweep4_aggregate.log:438`) | I1 prints `d`, `stateOk`, `attribBase` | route sizes (§2.2: the spans, the non-Partial/Ghost `kLeverInputGain` entries, `kPartialLaneGain`, `kGhostLaneGain`). Self-affinity reaches the output only through the eco lanes. The EcosystemEngine rules are a non-goal |
| D13.1 event rate ≤ 0.3 | Teeming (`:894-905`, 800 → 0.2×) | StateWithReversion (800 → 0.5). Conjunct: **S7 Ecosystem on Teeming** at the secondary bar (`:250-251`; `preset_test_support.h:2712-2719`) | I1 prints `d`, `stateOk`, `conjunctOk`, and S7's own `d` on Teeming (`VORAGO_PILOT_CELLS=S7`) | the conjunct needs S7's ecosystem-depth ablation on Teeming at ≥ 1.5. Route sizes first (L2), then the Life rows (L7; Teeming is M10's showcase) |
| D13.2 event rate ≥ 3.0 | Colony Pulse (`:537-546`, 800 → 4.0×) | StateWithReversion (800 → 0.5). Conjunct: S7 on Colony Pulse, its own primary (`:252-253`) | I1 prints `d`, `stateOk`, `conjunctOk` | route sizes (L2). The I1 reading names the failing term |

- **Read at every L2 rung**, besides the rung's own cell:
  - Colony Pulse with `VORAGO_PILOT_SECONDARY=1`, which reads E7.hi and D13.2;
  - Teeming with `VORAGO_PILOT_SECONDARY=1 VORAGO_PILOT_CELLS=S7`, which reads D13.1 and its conjunct.

  The same readings are taken at every Life rung at L7 (M10 is Teeming), and at F1.
- **Ruling.** An L2 rung is ruled on its own cell (E1, E3, E4 or E5) with these readings beside it. Where more
  than one rung clears the own cell, the plan proposes the one that moves these secondaries furthest.
- **Re-open rule.** Once one of the three verifies at a ruled rung, it joins the R step's set (FR-010b). A
  later lever that drops it re-opens the lever that lifted it.
- **Stop-and-surface (FR-027).** After L7, any of E7.hi, D13.1 (or Teeming's S7 conjunct) or D13.2 that is
  still short, with every named lever at its ladder top, is surfaced to the user with its readings across all
  rungs. It is never recorded UNMET. No lever outside FR-012 / FR-014 is added without a ruling.

---

## 3. Probe and harness changes (FR-002, FR-004, FR-016, FR-024b/c, E-10)

All of these changes are in `plugins/vorago/tests/preset_test_support.h` and
`.../integration/preset_pilot_test.cpp`. All are hidden (`[.probe]`) except the harness's attack-window
change, which the `[long]` sweep consumes.

### 3.1 FR-004 readouts

Each is an environment option, and all honour `VORAGO_PILOT_OVERRIDE`.

| Option | Effect |
|---|---|
| `VORAGO_PILOT_SECONDARY=1` | After the primary verdict, calls `computeVerificationVector(&patched, p.comp, p.tl, 0.0, meanOf(storedTake.minutes), width, &storedTake)` (`preset_test_support.h:2510`). For **each claimed secondary** of the def, plus any cell named in `VORAGO_PILOT_CELLS=<label,...>`, it prints `d`, the bar 1.5, `stateOk`, `conjunctOk`, `attribBase`, the skip reason and `verifiedAt(o, cell, ClaimRole::Secondary)` |
| `VORAGO_PILOT_TAKES=4` | Runs `computeTakes(..., VoragoTest::kRuledTakes, ...)` and prints one arm line per take, in the `:949-959` format, prefixed `take j seed s:`. For E-10's record it then repeats the twin and route scoring with `p.storedSeed = takeSeedIndex(stored, 0, j, 4)` and prints `d` per take beside the gated stored-take `d` |
| (always on) | A **44.1 kHz** stored-take render, `detail::sustainSpec(comp, tl, storedSeed, kRate441, false)`, scored by `detail::scoreRateArm1(cap, tl.A + kGestureAfterAttackSeconds, kRate441, ...)`. It prints `arm1@44.1k: finite, peak, hi, PASS/NO` against `kSweepPeakCeiling` / `kRunawayDb`, the sweep's own rule (`preset_sweep_test.cpp`, `Vorago_PresetSweep_SustainAtAllRates`) |
| `VORAGO_PILOT_ITERATE=verified27` | Derives SC-007's roster inside the probe: `requiredPrimaryCells()` (`vorago_preset_defs.h:297-322`) minus the 15 roster primaries, which sit in a `kRosterPrimaries` array in the probe TU copied from the spec's Cell roster. It finds each cell's host def by its primary and runs the probe body once per host **as compiled**; the run fails if `VORAGO_PILOT_OVERRIDE` is set. It REQUIREs a derived count of 27 and prints `cell, host, d, bar, arms` per row into `final_verified27.log` |
| `VORAGO_PILOT_MASTER_TRIM=-6` | Before the state is built, multiplies the patched def's `kMasterGainId` normalized value (the stored value, or 0.5 when absent) by `10^(−6/20) = 0.501187`, and prints the value used. The whole run (takes, twins and arms) renders at it (FR-024c) |

The stored-take arm line (`:949-959`) is unchanged; FR-024's gate reads it.

### 3.2 Verdict line additions

The verdict line (`:991-995`) keeps its format. A second line, `levels: arms [y y y y] arm1@44.1k [y]`, is
added so that a compliance row can cite one printout (FR-024).

### 3.3 Measured-reach attack window (FR-016 option (b), the one FR-002 exception)

- **This lands at step L5, with the attack lever, not at I1.** It moves D9.1's and D8.2's `d_att`, so it is a
  scoring change. I1's FR-003 (ii) re-read must see a reporting-only change (§3.1 and §3.2 only).
- **`TakeRecord` gains in-process fields** `std::vector<float> attackCapL, attackCapR;`. `renderTake` fills
  them instead of computing `t.attack` itself (`:2052-2056`). `attackReachSeconds` is still computed there.
- **New helper:**
  ```cpp
  /// FR-016 (b): W_end = max(reach_P, reach_rev) + 5 s, each the first 1 s window at RMS(Sus) - 6 dB.
  [[nodiscard]] inline std::optional<double> measuredAttackWindowEndSeconds(
      std::optional<double> reachP, std::optional<double> reachRev, double captureEnd);
  ```
  It returns `nullopt` if either reach is absent or later than `captureEnd − 5`: the reach must lie inside the
  registered-bound capture. The caller then marks the cell skipped with the new reason
  `kSkipReachOutsideCapture = "reach outside capture"`.
- **The capture stays** `[0, attackWindowEndSeconds(def, comp)]`, the registered bound (`:2389-2405`). It is
  an upper bound under (b), because the curve still reaches level 1.0 at the registered stage-0 time. Both
  attack descriptors are then `describeWithEnergyFloor(...)` over the first `round(W_meas·sr)` samples of
  each capture.
- **It applies at all four sites:** the sweep (`:2577-2600`, `:2683-2708`) and the pilot (`:446-470`,
  `:530-552`). Every site prints `W_end registered %.1f s, measured %.1f s`, so SC-004 records both.
- **D8.2 runs through the same code** (FR-016). Its preset is in Growth mode, but its twin reverts to
  Standard, so `A_rev` reads stage 0.

---

## 4. State layout, contracts, RT safety

| Owner | Added state | Reset / prepare contract |
|---|---|---|
| `VoragoVoice` | `attackStartLevel_` (float, §2.5) | latched in `noteOn` when the envelope is `Idle`; set to 0 wherever `mse_.reset()` runs (`:1797`). It is note state, not configuration |
| `VoragoVoice` | none for the floor (the cloud stores the floored value; `cloudRichness_` already exists) | `prepare()` → `setRichness(0.70f)` (`:558`) runs the new body |
| `VoragoVoice` | none for child gain or wet make-up (component configuration) | `prepare()` installs `kBloomChildGain` and `kEcologyWetMakeupDb`. `reset()` keeps them, because `bloom_engine.h` and `feedback_ecology.h:878` preserve configuration |
| `VoragoEngine` | none (`atmos_` stores the density) | `prepare()` installs `kGhostDensity` |
| `VoragoMacroMatrix` | `kRows` rows only | compile-time |

**RT safety (E-12).**
- Every edit is a constant, a row, a scalar setter, or arithmetic inside an existing per-chunk function.
- `std::pow` in `partialCountFor` runs once per control chunk, as the cloud's own `:1462` does.
- No allocation, lock, exception or I/O, and no new pool.

**Non-finite inputs (E-13).** Every new setter either rejects non-finite input or forwards to an owner that
does. `setRichness` rejects before it touches the shadow.

---

## 5. Test plan

**Where the tests live.** All TUs are already registered, so this phase adds new TEST_CASEs only, with no new
file.
- In `dsp_systems_tests` (`dsp/tests/CMakeLists.txt:324`):
  - `dsp/tests/unit/systems/vorago_voice_test.cpp` (`:511`);
  - `vorago_engine_test.cpp` (`:513`);
  - `vorago_ecosystem_lever_test.cpp` (`:560`);
  - `vorago_macro_test.cpp` and `vorago_perf_test.cpp`.
- In `vorago_tests` (`plugins/vorago/tests/CMakeLists.txt:27-60`): `integration/preset_pilot_test.cpp`,
  `preset_sweep_test.cpp` and `soak_test.cpp`.

**Bloom (FR-011, SC-008)**

| FR / SC | File | TEST_CASE | Assertion strategy |
|---|---|---|---|
| FR-011 / SC-008 (a)(b), per-push | `vorago_voice_test.cpp` | `VoragoVoice_BloomChildrenAudible` `[systems][vorago]` | See the steps below this table |
| FR-011 floor pin | `vorago_voice_test.cpp` | `VoragoVoice_CloudRichnessFloor` | A standalone `HarmonicCloud`, prepared, `setRichness(kCloudRichnessFloor)`, one control update: `getActivePartialCount() == kMinCloudCapacity`. On the voice, `getRichness()` returns the user value at r ∈ {0, 0.35, 0.6346, 0.7, 1}, and the cloud count is ≥ 14 at every r on a 0.02 grid |
| FR-011 neutrality below the floor | `vorago_voice_test.cpp` | `VoragoVoice_CloudRichnessFloorNeutral` | Bloom off, mutation 0, at r ∈ {0.0, 0.25, 0.40, 0.55}: the per-slot amplitude ratio to slot 0 matches a standalone `HarmonicCloud` at r_user within 0.5 dB, for every slot below N_user; every slot from N_user to 13 is below `kSilentParentAmplitude` |
| FR-011 neutrality above the floor | `vorago_voice_test.cpp` | existing `VoragoVoice_SpectralTargetIsNeutral` (`:1439`) | **Surfaced (FR-034).** Its richness set `{0, 0.25, 0.5, 1}` (`:1369`) includes values below the floor. There, the "plain" arm's `clearSpectralTarget()` leaves the cloud at the floor richness rather than the user law. Re-specified: the plain-vs-forced identity is asserted at `{0.6346, 0.75, 1.0}`; the below-floor half moves to `VoragoVoice_CloudRichnessFloorNeutral` |
| FR-011 edges | `vorago_voice_test.cpp` | existing `VoragoVoice_SpectralTargetEdge` (`:1650`), `VoragoVoice_ClearingPathsDropTheSpectralTarget` (`:1589`), `VoragoVoice_BloomCapacityTracksCloud` (`:1383`); new `VoragoVoice_CloudFloorEdge` | The existing cases run at the default r 0.70 / 1.0 and are expected green unedited. The new case sweeps r 0.70 → 0.40 → 0.70; at each edge, `maxDeltaInWindow` ≤ 1.5 × the pre-edge value (the `:1655-1700` statistic) |
| FR-011 instrument (FR-010) | `vorago_voice_test.cpp` | `VoragoVoice_BloomCountsProbe` (`:4032`), extended, hidden | Adds per child: slot, `getActivePartialCount()`, whether slot < active, and the sounding amplitude; bloom on vs off. Run on the base tree first, to confirm the Overview's mechanism (children silent for r < 0.626) → `artifacts/bloom_mechanism_base.log` |
| FR-011 CPU (Q2) | `vorago_perf_test.cpp` | `VoragoVoice_CloudFloorCpuProbe` `[.perf]` | One voice at r 0.40 and at 0.70, bloom depth 0, 30 s; prints ns/block. Base and after are recorded; no assertion |

`VoragoVoice_BloomChildrenAudible` steps:
1. Set up at 48 kHz with `makeBloomDeterministic` (1 s fade-in), mutation 0 (so `w_i ≡ 1`) and seed 1.
2. For each r ∈ {0.0, 0.35, 0.40, 0.70, 1.0}, run twin voices: bloom on (`triggerBloom()`) and bloom off
   (depth 0, wake 0). Render 2 chunks per richness change, then 1.6 s, so every child spawned at the change
   is past the 1 s fade-in before (a) is read.
3. **(a)** For each `i < kMaxChildren` with `getChildSlotIndex(i) != kMaxSlots` and
   `getChildAmplitude(i) > 1e-4`: the slot is `< getActivePartialCount()`, and
   `getPartialCurrentAmplitude(slot) > kSilentParentAmplitude`.
4. **(b)** For each `i < min(N_user, 8)` that is not a latched parent (`getChildParentIndex` of a live
   child): `|20 log10((a_on[i]/a_on[0]) / (a_off[i]/a_off[0]))| ≤ 0.5 dB`.
5. Non-vacuity: at every r, at least one child passes step 3's own filter (`getChildSlotIndex(i) != kMaxSlots`
   **and** `getChildAmplitude(i) > 1e-4`), so (a) is never vacuous. The count is printed.
6. Print the absolute per-parent drop (dB) at `kBloomChildGain`, for FR-040.

**Levers, bounds and invariants**

| FR / SC | File | TEST_CASE | Assertion strategy |
|---|---|---|---|
| FR-012 / SC-009 | `vorago_ecosystem_lever_test.cpp` | `VoragoVoice_EcosystemLeverNeutral` (`:1137`); new `VoragoVoice_RouteLeverZeroAtZeroLane` | The existing case stays green with no edit, because the new terms are 0 at lane 0. The new case injects lanes at 0 via `Probe` and checks, exactly, that mutation and bloom depth equal their bases and the ghost request equals the sched term, at the compiled `kPartialLaneGain` / `kGhostLaneGain` |
| FR-016 / SC-004 | `vorago_voice_test.cpp` | `VoragoVoice_AttackCurveReach` | Default envelope, 48 kHz, no cavern. The shaped gain (`getEnvelopeOutput()` / velocity) first reaches 0.426 at a t/T within ±0.02 of 0.426^{1/n} for the compiled `kAttackShapePower`. Continuity: \|Δ\| < 1e-3 per sample across the stage-0 → stage-1 boundary. Stages 1–3 are unchanged: the gain at each stage boundary equals the stage level ±1e-4. A note-on while the envelope is Running at 0.85 does not re-enter stage 0 and stays continuous (\|Δ\| < 1e-3) |
| FR-016 tracking | `vorago_voice_test.cpp` | `VoragoVoice_AttackTracksStageTime` `[systems][vorago]` | The whole default voice (every section at its `prepare()` value; no engine, no cavern), at 8 kHz on the `kIdentitySampleRate8k` pattern (`:2506`), rendered to 180 s. RMS(Sus) is taken over the sustain stage; the first 1 s window at RMS(Sus) − 6 dB must lie in `[0.75 · T0, T0]`, with T0 = 20 s. This is the per-push form of §2.5's tracking criterion; the probe's `P_rev` reach line is the compliance reading. The wall clock is printed, and the case is tagged `[long]` only if it costs more than ~15 s (CLAUDE.md) |
| FR-016 harness | `preset_sweep_test.cpp` (the T034 arithmetic area) | `Vorago_PresetSweep_MeasuredAttackWindow` | On synthetic captures, `measuredAttackWindowEndSeconds` equals max(reach) + 5, and returns nullopt when a reach is missing or later than captureEnd − 5 |
| FR-017b | `vorago_voice_test.cpp` | `VoragoVoice_LifeLaneDepthZero` | At breathing or tidal depth 0, `getBreathingGravityLane() == 0` and `getTidalFogDepth() == 0` exactly over 60 s. At depth 1: \|`getBreathingGravityLane()`\| ≤ `kBreathGravityLaneGain` (the lane is published unclamped, `vorago_voice.h:1022`); the applied `resonance().getGravity()` stays within [−1, 1]; `getTidalFogDepth()` stays within [0, 1] |
| SC-013 boundedness | `vorago_ecosystem_lever_test.cpp` | `VoragoEngine_CapabilityLeverBounded` `[systems][vorago]` | See the worst-case setup below this table |
| SC-010 | `vorago_macro_test.cpp` | `VoragoMacro_NeutralIsIdentity` (`:1419`) and the build's `static_assert`s | Unchanged tests |
| SC-011 | `vorago_macro_test.cpp` | `VoragoMacro_NoZipper` (`:1514`), `VoragoMacro_SweepAxes` (`:1211`), both `[long]` | Base-tree logs (FR-003 iii) vs final: every assertion that passes on the base tree still passes |
| SC-012 / 12b | `ecosystem_rule_probe_test.cpp` | `Vorago_EcosystemRuleProbe` (`:544`) | GATE1M ≥ 0.5 on both surfaces; syncRate counted on the default surface, selfAffinity and syncRate at Life max |
| SC-014 | `vorago_perf_test.cpp:971`, `processor_cpu_test.cpp:188` | `VoragoEngine_CpuBudget`, `Vorago_ProcessorCpu` | `node tools/run-cpu-tests.js`, run alone; ns and % delta recorded |
| SC-015 | `vorago_engine_test.cpp:1837` | `VoragoEngine_SlotSeedReproducibility` | Unchanged |
| SC-016 | `vorago_ghost_ext_test.cpp:415` | `VoragoEngine_GhostExtensionWiring` | Re-harvested once on the final tree: two byte-identical runs, a verify run, a new PROVENANCE block |
| SC-019 | plugin | `param_table_test`, `state_v2_test`, `state_v3_test` | Unchanged and green |
| SC-001 – SC-007b, SC-020, SC-023 | `preset_pilot_test.cpp` | `Vorago_PresetPilot_PrimaryProbe` (hidden) | Readings, not assertions. Compliance applies the thresholds to the printed lines |

`VoragoEngine_CapabilityLeverBounded` worst case, following the `:1742-1790` pattern:
- six voices held;
- all 12 macros at 1, with Gravity run at **both 0 and 1**;
- bloom depth 1 and spawn rate `kMaxSpawnRateHz`;
- ghost peak 1;
- eco lanes injected at 1;
- 60 s per arm at 44.1, 48 and 96 kHz.

The assertions follow the cited pattern in full:
- **Allocation.** The render loop runs inside `TestHelpers::AllocationScope`. The count
  `AllocationDetector::instance().getAllocationCount()` is taken inside the scope, and
  `REQUIRE(allocations == 0u)` is checked after the scope closes. All buffers, the matrix and the lane
  injections are built before the scope opens. No REQUIRE or INFO runs inside it, because both allocate
  (`vorago_engine_test.cpp:1738-1781`, `:1789`). The TU gains `#include <allocation_detector.h>` and **never**
  `<allocation_operator_overrides.h>`, whose single owner is `selectable_oscillator_test.cpp`
  (`vorago_engine_test.cpp:27-34`, `:60-61`).
- **Pools.** `getAllocatedBytes()` equals its after-prepare value, as a second assertion.
- **Bounds.** Every sample is finite by bit pattern, and \|out\| ≤ 0.9661. Both results are accumulated inside
  the scope into plain locals and asserted after it closes.
- The wall clock is printed (R-2: more than 4 min measured alone → surface).

**Tolerances.**
- No float golden anywhere.
- Per-slot ratios are in dB, with stated bounds.
- The probe reproduction tolerance is `max(0.01, 0.005·d)` (FR-003).
- Fingerprints go through `render_fingerprint.h` only.

**FR-034 surfaced list.** These pre-existing tests may encode the old voicing as data. Each is listed with its
reason before it is re-measured, and none is edited silently:
- `VoragoVoice_SpectralTargetIsNeutral`: its below-floor richness points (above);
- `VoragoEngine_GhostExtensionWiring` clause (a): its fingerprint moves with any ghost, bloom or ecology
  change (FR-035);
- any SweepAxes endpoint figure printed as data in `vorago_macro_test.cpp` that a row change moves;
- `VoragoEngine_GhostLevelProbe`'s sizing constants (record only);
- `VoragoVoice_SilenceClearsEcologyAudio` (`vorago_voice_test.cpp:877`), its steal-teardown positive control.
  - It drives the default 20 s attack for 4 s and REQUIREs `steadyDb > -50.0` (`:909-923`; MSVC measured
    −47.2 dBFS driven, `:902`).
  - At t = 0.2 T, today's Exponential envelope is about 0.33. Linear gives 0.20 (−4.3 dB), and phase^n gives
    0.2^n (n = 6: about −74 dB). The control therefore falls under −50 at **every** §2.5 rung.
  - Rung: L5, if a stage-0 reshape ships.
  - Proposed re-spec: shorten stage 0 for the drive through the voice's `setEnvelopeStageTimeMs(0, …)`
    (`vorago_voice.h:1068`). The case measures the ecology's stored energy after a steal, not the attack, so
    the −50 dB control and the −80 dB claim stay as they are;
- `VoragoVoice_LifeModulatorLanes` (`vorago_voice_test.cpp:2910`).
  - It REQUIREs the breath lane's extremes to be the configured depth ±0.01, "not its square"
    (`:2971-2972`). Any `kBreathGravityLaneGain` other than 1 breaks it.
  - Rung: L6, if the gain is ruled ≠ 1.
  - Proposed re-spec: extremes = `kBreathGravityLaneGain · 0.30` ±0.01, which still rules out a squared
    depth. The exact-sum assertion (`worstSumError == 0`) holds unedited while base + lane stays inside the
    ±1 clamp: 0.30 · 3 = 0.9 at the ladder top;
- `VoragoEngine_GhostConfiguration` clause 1 (`vorago_engine_test.cpp:2767`), which REQUIREs
  `atmos.getDensity() == 0.30f`.
  - Rung: L3, if the density candidate is ruled.
  - Proposed re-spec: `== VoragoEngine::kGhostDensity`. This keeps the clause's "a drifted copy" intent
    (`:2765-2766`).

Compliance extends the list if a build-stage run finds another.

---

## 6. Build integration

- **No CMake list changes.** Every new TEST_CASE goes into a TU that is already listed: `dsp/tests/CMakeLists.txt:511`,
  `:513`, `:560`, plus the macro and perf TUs in the same `dsp_systems_tests` list, and
  `plugins/vorago/tests/CMakeLists.txt:27-60`. The new tests use `detail::isFinite` bit-pattern checks, so no
  `-fno-fast-math` list entry is needed.
- **Targets per step:**
  - `dsp_systems_tests` (voice, engine, macro);
  - `vorago_tests` (probe, sweep helpers, plugin suites);
  - `seraphis_tests` and `shared_tests` (the FR-034 / FR-037 guard);
  - `Vorago` (pluginval).
  ```
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_systems_tests vorago_tests
  build/windows-x64-release/bin/Release/dsp_systems_tests.exe "~[.perf]~[long]" 2>&1 | tail -5
  VORAGO_PILOT_PRESET="Slow Bloom" build/windows-x64-release/bin/Release/vorago_tests.exe "Vorago_PresetPilot_PrimaryProbe" > specs/vorago-phase13c-capability-audibility/artifacts/<name>.log 2>&1
  ```
- `[long]` and `[.perf]` run separately and alone (CLAUDE.md "Timing-sensitive tests").

---

## 7. Order of work (FR-003, FR-010b, FR-026) and artifacts

- All logs go to `specs/vorago-phase13c-capability-audibility/artifacts/`.
- Each probe runs alone, with nothing else executing.
- Slow output is captured to a log and never re-run just for its output.
- Every candidate is a rebuild (§0 item 4). `VORAGO_PILOT_TAKES=4` is used only for M5 at L7 and at F1 (§8).

| Step | Tree | Work | Artifacts |
|---|---|---|---|
| B1 | `64f57e1a`, unmodified | Probe the 15 roster primaries as compiled; read one cell (Slow Bloom) twice for the repeat spread | `before_<cell>.log`, `before_repeat.log` |
| B2 | same | `VoragoMacro_SweepAxes` and `VoragoMacro_NoZipper`; the 13b Gate 1 and 14-knob Gate 2 tables at the default surface and at Life max; the FR-005 default surface (13b t0/t0on/M1, pilot P0); the CPU baseline; the `VoragoVoice_BloomCountsProbe` mechanism read | `base_sweepaxes.log`, `base_nozipper.log`, `gate1_base_*.log`, `gate2_table_{default,lifemax}_base.log`, `default_before_*.log`, `cpu_base.log`, `bloom_mechanism_base.log` |
| I1 | + §3.1 and §3.2 only (reporting; §3.3 waits for L5) | Re-read the primaries, within tolerance of B1 (FR-003 (ii)). Read the roster secondaries: E7.hi and D13.2 on Colony Pulse; D13.1 with S7 on Teeming (§2.10); D14.1 and D14.2. Read the **FR-030b set**, enumerated below. Record D7.1 as unverified-before | `before2_<cell>.log`, `before2_secondaries.log` (its header carries the enumerated FR-030b list), `before2_default_state.log` |
| L1 bloom | + §2.1 (a); one rebuild per `kBloomChildGain` rung | S6 (Slow Bloom) and E1 (Bloom Colony); the SC-008 sentinel; rule `kBloomChildGain`. Each rung's ruling reading includes the `VORAGO_PILOT_MASTER_TRIM=-6` re-read (FR-024c) | `l1_bloom_<value>.log`, `l1_bloom_<value>_minus6.log` |
| L2 routes | + a rebuild per rung | The E1 → E3 → E4 → E5 ladders (§2.2). At **every** rung, also read E7.hi and D13.2 (Colony Pulse) and D13.1 with Teeming's S7 conjunct (§2.10); re-read S6 | `l2_<cell>_<rung>.log`, `l2_secondaries_<rung>.log` |
| L3 ghost | + a rebuild per make-up rung, then per density rung | S9 (Choir of Absence) and E5; the ghost sizing probe. Every rung's ruling reading includes the −6 dB re-read (FR-024c) | `l3_ghost_*.log`, `l3_ghost_*_minus6.log`, `ghost_level_probe.log` |
| L4 S4 | + a rebuild per wet make-up rung | Feedback Mire with arm 2; re-read E4. Every rung's ruling reading includes the −6 dB re-read (FR-024c) | `l4_ecology_*.log`, `l4_ecology_*_minus6.log` |
| L5 attack | + §3.3 (the measured-reach window, landing here); a rebuild per `kAttackShapePower` rung (n = 1, 2, 4, 5, 6, 7); an option (a) scratch build | Sudden Chasm (D9.1) with both window ends and the `P_rev` reach against `[0.75 T0, T0]` (§2.5); D8.2's host as compiled (FR-030) | `l5_attack_n<k>.log`, `l5_attack_optionA.log`, `l5_d82.log` |
| L6 life | + lane-gain rebuilds | Slow Bloom (D14.1) and Drifting Strata (D14.2), via the secondary readout | `l6_life_*.log` |
| L7 macros | + row rebuilds; the M5 cause table first | M2, M3, M4, M5, M9, M12, then M10 last. **M5's cause table and every M5 rung run with `VORAGO_PILOT_TAKES=4`** (§2.8). Every Life rung re-reads E7.hi, D13.1 (with S7 on Teeming) and D13.2 (§2.10) | `m5_cause.log`, `l7_<macro>_<rung>.log` |
| R | after each ruling | Any earlier roster cell, or any §2.10 secondary that has verified at a ruled rung, that drops below its bar re-opens its lever (FR-010b). It is read from the cells the ruled lever can reach (E-1) | in the step's log |
| F1 | final tree | Every roster gate once, alone, with the all-take, 44.1 kHz and −6 dB re-reads. The 27 verified primaries as compiled, through `VORAGO_PILOT_ITERATE=verified27` (§3.1), which derives the roster from `requiredPrimaryCells()`. The FR-030b set. The roster secondaries | `final_<cell>.log`, `final_minus6_<cell>.log`, `final_verified27.log`, `final_secondaries.log`, `final_default_state.log` |
| F2 | final tree | SweepAxes and NoZipper; the 13b gates; the FR-005 default surface **after** (13b t0/t0on/M1 at the default surface, pilot P0 descriptor and M1 RMS, finite, peak ≤ 0.9661; SC-018); the soaks; CPU; the full per-push suites, including `seraphis_tests` and `shared_tests`; the fingerprint re-harvest; check-portability, then `wsl --shutdown`; clang-tidy `dsp` + `vorago`; pluginval 5; `git diff --name-only 64f57e1a..HEAD` for FR-037 / SC-022 | `final_*.log`, `default_after_13b.log`, `default_after_p0.log`, `git_diff_names.txt` |
| C | compliance | Build, from the checked-in logs only: the **FR-040 lever table** (one row per feature; every candidate value → reading → log; the ruled value; the override string); the **FR-041 default-render note** (B2's `default_before_*` vs F2's `default_after_*`, recorded as intentional voicing); the **FR-042 hand-off list** (per showcase preset, the override string its F1 gate used, plus any descriptor-visible change to the E0 / P0 default surface); the SC-007 and SC-007b before/after tables; the SC-022 row citing `git_diff_names.txt` | `compliance.md` |

**The FR-030b set (written into `before2_secondaries.log`'s header at I1).** It is read from the sweep-4
`CoverageComplete` matrix (`specs/vorago-phase14-presets-release/artifacts/sweep4_aggregate.log`; legend at
`:91`, "S verified secondary"). It holds every cell outside the 42 required primaries that shows an `S`, plus
the measured default-state set (`:445-450`).
- **Host-preset secondaries (22):** E6.hi; D4.1, D4.2, D4.3, D4.4, D4.5, D4.7, D4.8, D4.9, D4.10, D4.11,
  D4.12; D5.1, D5.2; D6.1, D6.2, D6.3; D7.2, D7.3; D10.1; D11; D12.1. Each is read on its host through
  `VORAGO_PILOT_SECONDARY=1`, with `verifiedAt(…, ClaimRole::Secondary)`.
- **Default-state cells (8):** D1.6, D1.7, D2, D5.3, D8.1, D9.2, D10.2, D12.2. They are read on the default
  pseudo-preset with `VORAGO_SWEEP_SHARD=42/43` on `Vorago_PresetSweep_AblationVerifiesClaims`.
- **D7.1** is in the recorded constant but missing from the sweep-4 measurement. It is recorded as
  unverified-before, not attributed to a 13c lever.

The build stage re-derives the matrix rows from the log before I1 runs. If its count differs from 22 + 8, the
log wins and the difference is surfaced.

The user rules each lever on its logged readings (OQ-1). This plan proposes the ladders, not the values.

---

## 8. Risks and mitigations

| Risk | Mitigation |
|---|---|
| **The floor constant drifts from the cloud's rounding**, because float `pow` differs across toolchains | 0.6346 gives 64^r = 14.002, about 0.5 from either rounding edge. `VoragoVoice_CloudRichnessFloor` asserts the component's own count on every OS leg. `partialCountFor` is the cloud's expression verbatim, so voice and cloud agree on N_user by construction |
| **A global lever moves verified presets (E-1).** The floor alters every preset with r < 0.6346 | When no child lives, the default surface and every r ≥ 0.6346 follow the same code path as today. The FR-030 set (27 primaries) and the FR-030b set are re-read as compiled at F1. A lever that needs a repair override on one of them is not adopted |
| **Louder is not more distinct (E-2)**; limiter-held passes | Every reading carries arm 1. Every level make-up rung (L1 child gain, L3 ghost, L4 ecology wet) is ruled on a reading that **includes** the FR-024c `VORAGO_PILOT_MASTER_TRIM=-6` re-read (§7), not only at F1. Make-up ladders stop at the first rung that clears with the arms green **and** the −6 dB reading at its bar |
| **The attack reshape fails the tracking clause on the full output** | §2.5's ladder goes up to n = 7 (0.885 T, envelope only). `VoragoVoice_AttackTracksStageTime` and the L5 probe reach line measure the full output. If no rung reaches 0.75 T0, the clause is a stop-and-surface item (FR-027), never claimed |
| **The reshape breaks tests that drive the default attack** | The steal-teardown control is on the FR-034 list (§5) with a proposed re-spec. The build stage greps `vorago_*_test.cpp` for other cases that read level inside the first 20 s of a default-attack note and adds each to the list before L5 is ruled |
| **The ecosystem-limited secondaries stay short** (E7.hi, D13.x) | They are read at every L2 rung and every Life rung (§2.10). If they are still short after L7, they are surfaced (FR-027) |
| **Denormals** in newly-sounding child slots or zeroed parents | The cloud's tail sweep zeroes `currentAmplitude_ < 1e-8` wherever the target is 0 (`harmonic_cloud.h:1785-1789`). Bloom amplitudes pass `flushDenormal` (`bloom_engine.h:1580`). FTZ/DAZ is unchanged |
| **Fast-math NaN** | The new tests use `detail::isFinite` bit-pattern checks. `setRichness` uses the cloud's `detail::isNaN` / `isInf` |
| **Narrowing in brace init** (Clang) | New rows use designated initializers, `VoragoMacroRow{.macro=..., .base=0.15f, ...}`, with `f` literals, as at `:322-756` |
| **Zipper / SweepAxes (E-7)** on Movement or Gravity rows, and on the breathing-swell fallback | Every rung is gated on the two `[long]` tests before it is ruled. Movement rungs avoid the wander rate, where 13b measured the break |
| **The attack curve changes D9.2 or the default `A`** | `A`, the Sus placement and the D9 state predicates read stage **times**, which do not move (E-9). D9.2 is a StateOnly default-state cell |
| **The measured window falls outside the capture** | `measuredAttackWindowEndSeconds` returns nullopt, which gives an explicit skip reason, never a silently short window |
| **A child gain of 1.0 is not enough** | Stop-and-surface with the readings (§2.1 (b)); no clamp edit without a ruling |
| **`kVoiceSizeBound`** | Only +4 B (`attackStartLevel_`, §2.5). `sizeof` is printed at L5, and the bound stays unless it is exceeded (then surface) |
| **Fingerprint churn (E-14)** | One re-harvest at F2, inside `dsp_systems_tests`, with two byte-identical runs |
| **Portability: MSVC-green proves nothing** | `node tools/check-portability.js` before commit, then `wsl --shutdown`. No SIMD is added, so there is no aligned-load lint exposure |
| **Probe cost** (all takes × per-take twins is about 4× a probe) | `VORAGO_PILOT_TAKES=4` is used at F1 and, as the one ladder exception, for M5's cause table and every M5 rung at L7: SC-005 gates all K takes, and its arm-1 red is on take 3 (§2.8). Every other ladder reads the stored take |

---

## 9. ODR sweep (FR-038)

Each name below was run this session through `grep -rn "<Name>" dsp/ plugins/ tools/` on `64f57e1a`, and every
one returned **0 hits**:

- **Production constants and functions:** `kCloudRichnessFloor`, `partialCountFor`, `kBloomChildGain`,
  `setBloomChildGain`, `getBloomChildGain`, `bloomChildGain_`, `kAttackStageCurve`, `stageCurveFor`,
  `kEcologyWetMakeupDb`, `setEcologyWetMakeupDb`, `getEcologyWetMakeupDb`, `ecologyWetMakeupDb_`,
  `kGhostDensity`, `setGhostDensity`, `getGhostDensity`, `ghostDensity_`, `kBreathGravityLaneGain`,
  `kTidalFogLaneGain`, `kPartialLaneGain`, `kGhostLaneGain`, `userPartialCount_`.
- **Harness and probe names:** `measuredAttackWindowEndSeconds`, `kSkipReachOutsideCapture`, `engineTweak`,
  `tweakHeld`, `attackCapL`, `printAllTakes`, `kPilotMasterTrimDb`.
- **Environment options:** `VORAGO_PILOT_LEVER`, `VORAGO_PILOT_SECONDARY`, `VORAGO_PILOT_TAKES`,
  `VORAGO_PILOT_MASTER_TRIM`.
- **TEST_CASE names:** `VoragoVoice_BloomChildrenAudible`, `VoragoVoice_CloudRichnessFloor`,
  `VoragoVoice_CloudRichnessFloorNeutral`, `VoragoVoice_AttackCurveReach`, `VoragoVoice_LifeLaneDepthZero`,
  `VoragoVoice_LifeLaneLever`, `VoragoVoice_CloudFloorCpuProbe`, `VoragoEngine_CapabilityLeverBounded`.

- **Also swept, 0 hits:** `kPartialSpawnLaneGain`, `bloomSpawnBase_`, `kBreathSwellDepth`, `engineForTweak`,
  `VORAGO_PILOT_CELLS`, `VoragoVoice_CloudFloorEdge`, `VoragoVoice_RouteLeverZeroAtZeroLane`,
  `Vorago_PresetSweep_MeasuredAttackWindow`.
- **Swept in the review revision, 0 hits:** `kAttackShapePower`, `attackStartLevel_`, `shapeAttack`,
  `VoragoVoice_AttackTracksStageTime`, `VORAGO_PILOT_ITERATE`, `kRosterPrimaries`. (`kPartialSpawnLaneGain`
  and `bloomSpawnBase_` are no longer introduced by the default design; they would come back only with
  §10.3's scope extension.)

The build stage re-runs the sweep for any name it adds beyond this list (FR-038).

---

## 10. Items for the user (beyond OQ-1's per-lever values)

> **Ruled 2026-10-01 (spec Clarifications "Plan stage"):** 1 confirmed (floor unconditional, target on demand);
> 2 **adopted** (§2.9 is built, Group 3b, FR-002 / FR-010 amended; no gate reading under a lever env);
> 3 confirmed (size constants, `kLeverInputGain` untouched, spawn-rate route stays unadopted);
> 4 as written (fallbacks measured only when a first rung is short); 5 confirmed (0.75 · T0 = [15, 20] s).
> Child-gain ladder 0.35 / 0.50 / 0.70 / 1.00 confirmed; `VoragoVoice_EcosystemLaneShapingFidelity` on the
> FR-034 list, re-specced only if a lane gain other than 1 is ruled.

1. **Q2 reading.** This plan reads "always on, no switching path" as follows:
   - the cloud's active count is floored at 14, unconditionally;
   - the spectral target is engaged whenever the user's count is below the cloud's, or a child lives.

   Keeping the target engaged at every richness behaves identically above the floor. But it costs the target
   path at the default surface, and it breaks four edge assertions (§2.1). Confirm.
2. **Measurement seams (optional).** The default design measures every candidate on a rebuild (§0 item 4).
   §2.9's rebuild-free route would add three engine fan-outs, four voice forwards and a test-only
   `engineForTweak()`. It is built only if you want it **and** the spec adds it to FR-010 / FR-002 as a third
   measurement route, because it changes renders and FR-002 allows the probe reporting changes only.
3. **E1 / E5 route terms, and a spawn-rate scope extension.**
   - The default ladders add two constants that **size** the existing Partial and Ghost writes FR-012 names
     (`kPartialLaneGain` on mutation and bloom depth, `kGhostLaneGain` on the ghost request). They do not edit
     `kLeverInputGain`'s 13b `static_assert`. Confirm this reading of FR-012 against 13b FR-016 / FR-019a.
   - **Proposed scope extension (not in the default design):** a new Partial-lane destination, the bloom spawn
     rate, `bloom_.setSpawnRateHz(min(bloomSpawnBase_ · (1 + kPartialSpawnLaneGain · partialEco),
     kMaxSpawnRateHz))`. It needs a voice shadow `bloomSpawnBase_`, written by `setBloomSpawnRateHz`
     (`:1530`), with the Life row still writing the base. This is a new route, not a size, so it needs an
     FR-012 amendment. If adopted, it brings an enforcing test: at lane 0, the Life → BloomSpawnRateHz row
     still reads exactly its base (`VoragoVoice_RouteLeverZeroAtZeroLane`, extended). It is measured only if
     the size ladder leaves E1 short.
4. **Fallback proposals.** The second destinations for the life lanes (§2.6) and the macro "new target" rungs
   (§2.7) are proposals. They are measured only when the first rung is short.
5. **The attack tracking floor.** §2.5 reads FR-016's "audibly ~20 s" as: the measured `P_rev` reach lies in
   `[0.75 · T0, T0]`. The shipped `EnvCurve`s cannot reach it (Linear 0.426 T, Logarithmic 0.653 T), so the
   plan adds a voice-side phase power on a Linear stage 0. Confirm the 0.75 figure, or name another. If you
   would rather keep a shipped curve, FR-016's tracking clause must be amended in the spec, because no shipped
   curve can meet it.

---

## Review notes

Revision of 2026-10-01, against the plan review's eleven issues. None was rejected. The resolutions that chose
between two offered options:
- **FR-016 tracking (major).** Took option (1): a voice-side `phase^n` on a Linear stage 0 (§2.5). The tracking
  clause is now a measured criterion with a per-push test and a probe reading, not a silent gap. Its 0.75 floor
  is the plan's own figure, put to the user at §10.5.
- **Measurement seams (minor).** Took the first option: rebuild is the default route for every candidate, and
  §2.9 is optional pending §10.2 plus a spec amendment. The setters and getters are no longer part of the
  default design. Tests read through the existing const accessors `bloom()`, `ecology()` and `atmosphere()`.
- **E1 spawn-rate destination (minor).** Took the first option: the E1 ladder is restricted to a size on the
  existing Partial-lane writes (`kPartialLaneGain`). The spawn-rate route moved to §10.3 as a scope extension,
  with its enforcing test named.
- **−6 dB re-read (minor).** Took the first option: the FR-024c re-read is part of every level make-up rung's
  ruling reading (L1, L3, L4), so §8's mitigation now matches §7.
