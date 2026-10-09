# Feature Specification: Vorago Phase 13d — Engine Ceilings

**Spec slug:** `vorago-phase13d-engine-ceilings`
**Roadmap:** `specs/Vorago-roadmap.md` → Part B, Phase 13d (lines 663–707); Cross-Cutting Constraints (lines 765–789)
**Depends on:**
- Phase 13c: its levers, its probe ladders and the `VORAGO_PILOT_LEVER` seam. 13c closed by rulings on 2026-10-06.
- Phase 14's close, commit `502e5243` (roadmap 666–668):
  - sweep 5 as the record of the tree;
  - the T063 table;
  - the `VORAGO_PILOT_OVERRIDE` / `VORAGO_PILOT_LEVER` probes;
  - the sharded sweep;
  - `tools/run-close-lanes.js`.

**Blocks:** the Vorago 1.0.0 release (roadmap 668–669)
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-10-07 (branch `feat/vorago-phase1-events-modulation` at `502e5243`)

---

## Overview

Phase 14 closed **NOT release-green by measurement** (`specs/vorago-phase14-presets-release/compliance.md:1021-1027`).
Sweep 5 verified 36 of 42 required primaries (`sweep5_aggregate.log:363`, quoted at `compliance.md:632-633`). Its
re-author loop showed that the probe predicts the sweep within 0.01 (`compliance.md:673-675`).

Eight capability cells still do not reach their bar at any preset setting on the 13c tree. They are **engine ceilings,
not preset settings** (`compliance.md:654-659`, `:675`; roadmap 670–682):

- six primaries under F = 4.0: E1, E4, M2, M4, M5, M10;
- two secondaries under 1.5 on Colony Pulse: E6.hi and E7.hi.

The Movement macro row carries one more red:

- `VoragoMacro_SweepAxes` reads its rank correlation at rho 0.8333 against the 0.9 bound.
- It is the one red in the `dsp_systems_tests` `[long]` lane.
- It was already present on 13c's base tree, so 13c carried it rather than caused it (13c `compliance.md` FR-031 row;
  roadmap 676–678).

This phase applies **one engine lever per feature, measured before it is ruled** (roadmap 686):

- It uses the same instrument and ladder tooling as 13c.
- The lever families are the ones the roadmap names (roadmap 687–692).
- It continues until each of the eight cells clears its gate on its showcase preset, with the preset's four level arms
  green in the same printout.
- The Movement row must also become monotone, with no threshold moved (roadmap 683–686).

Phase 14's T048 then re-runs as the confirming pass (roadmap 699–702):

- the affected presets are re-authored by probe;
- their shards and the aggregate are re-run;
- the T063 table is re-read.

Like 13c, this is a voicing phase. The default render may change, and every change is recorded (roadmap 695).

**Code facts read this session that bear on the levers.** They constrain the plan. None of them is a ruled diagnosis.

- **On Teeming, one of Life's three rows is clamped by code-read; the other two have headroom.**
  - The `EcosystemDepth` row is base 0.85 + amount 0.15 (`vorago_macro_matrix.h:638-643`).
  - A stored `kEcosystemDepthId` becomes that target's base through `setTargetBase` (`param_routes.h:200`,
    `vorago_macro_matrix.h:1014-1021`).
  - Teeming stores ecosystem depth 1.0 (`tools/vorago_preset_defs.h:954`). `VoragoVoice::setEcosystemDepth` clamps to
    [0, 1] (`vorago_voice.h:1501-1506`).
  - So on M10's showcase preset the Life → EcosystemDepth row is clamped and contributes nothing.
  - The `EventRateScale` row is base 1 + amount 9, Linear. The row's own comment says the 9 was chosen to run the
    scale to the setter's clamp of 10 (`vorago_macro_matrix.h:644-655`; clamp at `vorago_voice.h:1566-1571`). That
    holds only at base 1 and Life 1. **It does not hold on Teeming:**
    - Teeming stores `{kEventsRateScaleId, 0.150515}` (→ 0.2×) and `{kMacroLifeId, 0.70}`
      (`tools/vorago_preset_defs.h:952-953`).
    - `kEventsRateScaleId` is an MB route onto `VoragoMacroTarget::EventRateScale` (`param_routes.h:114`, `:199`),
      so the stored 0.2 replaces the row base through `setTargetBase` (`vorago_macro_matrix.h:1014-1021`), and
      `evaluateAll` seeds from that override (`:1185-1195`).
    - By code-read the row sums to 0.2 + 9 × 0.7 = 6.5 at stored Life, under the clamp of 10. FR-019 confirms this by
      measurement; it is not a given.
  - The `BloomSpawnRateHz` row is base 1/240 + amount 0.0208 (`vorago_macro_matrix.h:656-665`). That stays under the
    bloom's `kMaxSpawnRateHz` 0.05 (`bloom_engine.h:285`). So the Life rows with headroom on Teeming are, by code-read, `EventRateScale` and `BloomSpawnRateHz`.
- **The Age macro has no mutation row.**
  - The roadmap's candidates for M2 are "damping, tilt, mutation" (roadmap 688–689).
  - The shipped Age rows are BodyDamping 0.25 + 0.55, CavernDecaySeconds 20 − 14 and CloudSpectralTiltDb −4 − 4
    (`vorago_macro_matrix.h:342-359`).
  - `VoragoMacroTarget::CloudMutation` already exists as a target (`:144`); its existing row is Entropy's (`:462`).
  - 13c measured an Age → mutation row as rung r3 and read 2.8273 (13c `compliance.md` §1, M2 row).
- **The Partial route writes two destinations through one gain.**
  - The two writes are `cloud_.setMutation(clamp(mutationBase_ + kPartialLaneGain · partialEco))` and
    `bloom_.setDepth(clamp(bloomDepthBase_ + kPartialLaneGain · partialEco))` (`vorago_voice.h:2232-2233`).
  - `kPartialLaneGain = 1.0f` (`:1685`).
  - The bloom's own child gain is a separate constant, `kBloomChildGain = 1.5f` (`:381`), installed at prepare (`:658`).
- **The Feedback route drives three things from one shaped lane.**
  - loop wake: `combineWake(loopWakeBase_[l], eco, sched)` (`:2224-2228`);
  - loop gain: `kLoopGainLeverSpan · x`, with span 0.18 (`:1681`, `:2274-2276`);
  - ring coupling: `kRingCouplingBase + kCouplingLeverSpan · x` (`:1694-1695`, `:2277-2283`).

  The lane is shaped by `kLeverInputGain[Feedback] = 2.0` (`:1742-1743`), and the loop wake base is
  `kLoopWakeBase = 0.45f` (`:431`).
- **The Movement row already runs every member to its ceiling** (`vorago_macro_matrix.h:391-430`):
  - drift 8 + 42 cents, up to `kMaxDriftCents` 50;
  - both wander rates 0.03 + 0.97 on an Exponential curve, up to the 1 Hz clamp;
  - breathing 0.30 + 0.70;
  - cavern damper depth 0.35 + 0.45.

  The 13b colony wander lever is rate-compensated by `(0.03 / rate)^0.75` (`vorago_voice.h:1713-1731`). That exponent
  was chosen against this same rho assertion.
- **The `VORAGO_PILOT_LEVER` seam takes only four keys:** `childGain`, `ghostTapDb`, `ghostDensity` and `ecologyWetDb`.
  - An unknown key fails the run (`plugins/vorago/tests/integration/preset_pilot_test.cpp:875-933`).
  - None of 13d's lever families has a key today.
  - So each 13d lever is laddered either by rebuild or through a new seam (FR-012).

---

## Scope

1. **The cell roster** (FR-001): the six primaries and the two secondaries the roadmap names, plus the Movement row's
   monotonicity.
2. **Before-record** on the `502e5243` tree (FR-003), using:
   - the Phase 14 pilot probe;
   - the `[long]` macro tests.
3. **Levers** (FR-010 – FR-019):
   - one lever per feature, from the roadmap's named families;
   - each measured by a ladder before it is ruled;
   - each applied in Vorago-owned code.
4. **The gate** for each cell on the final tree (FR-020 – FR-027).
5. **No regression** (FR-030 – FR-039).
   - These stay verified:
     - the 36 primaries verified at sweep 5;
     - the secondaries verified at sweep 5;
     - the default-state cells verified at sweep 5.
   - These stay green:
     - Phase 10's bounds;
     - 13b's and 13c's gates;
     - the Phase 2–14 per-push suites and the `[long]` soaks;
     - CPU, pluginval, clang-tidy and portability.
6. **The confirming pass** (FR-040 – FR-044). This is Phase 14's T048 re-run (roadmap 699–702, 706–707):
   - the roster's showcase presets and the three showcase-subset presets (Hull Resonance, Smeared Horizon, Haunted
     Colony) are fully re-authored by probe; every other affected preset is re-measured and re-authored only on a drop
     of more than 0.01 or a lost verification (FR-040);
   - the three subset presets are re-authored to clear every showcase-subset pair (FR-041);
   - the affected shards and the aggregate are re-run;
   - the T063 table is re-read.
7. **Evidence** (FR-050 – FR-053): the lever table, the default-render record and the CPU delta (roadmap 704–707).

## Non-goals (what other phases own, or nobody does)

- **The T050 audition (SC-020), the push, and the Phase 14 items that need the push.** The items are SC-013, the
  SC-019 auval run, the SC-023 nightly and the FR-025 Linux generator build. The Phase 14 verdict defers them to the
  user (`compliance.md:1024-1026`). The roadmap's 13d text does not name them.
- **Any change to a bar, window, K, take count or descriptor.** These are inputs, never levers:
  - F = 4.0 (`kFloorF`, `plugins/vorago/tests/preset_test_support.h:249`);
  - the secondary bar 1.5 (`kSecondaryBar`, `:250`);
  - the attributability margin 1.5 (`kAttribMargin`, `:979`);
  - K = 4 (Phase 14 "Gate G2 re-run ruling", `compliance.md:624-626`);
  - the `VoragoMacro_SweepAxes` rho bound 0.9 and its endpoint thresholds
    (`dsp/tests/unit/systems/vorago_macro_test.cpp:1318-1319`);
  - the `VoragoMacro_NoZipper` bound.

  For the rho bound the roadmap says "without any threshold moving" (roadmap 686).
- **The `EcosystemEngine` simulation:** its rules, stages, agent kinds and energy budget (`ecosystem_engine.h`).
- **13b's FR-023 wake-combine rule** (`VoragoVoice::combineWake`, `vorago_voice.h:1091-1094`).

  Both stay unchanged, as they did in 13b and 13c. The E6.hi / E7.hi lever acts on **the lanes' audible range**
  (roadmap 692), not on the simulation.
- **New registered parameters or a state-format change.**
  - No parameter is registered, removed, re-typed or re-ranged.
  - `kCurrentStateVersion` stays 3 (`plugins/vorago/src/plugin_ids.h:24`).
- **Choir of Absence's seed-13 arm-1 take** (ruling B-18, measured and declined in 13c). The confirming pass re-reads it
  (FR-043). It is not levered here.
- **Items the roadmap's 13d text does not name:**
  - `FeedbackEcology_CpuBudget` (13c ruling B-17);
  - the untagged wall-clock budget in `SeraphisEngine_VoiceStealIsClickless`;
  - moving the `dsp` `[long]` lane to nightly-only.

  The last two are Seraphis-area or CI scope, and the user has not ruled on them.
- **Glass Well's surfaced FR-017a calibration STOP** (Phase 14 "Gate G2 re-run ruling"). That ruling stands.
- **UI.** The Phase 13 editor and the ecosystem view are untouched.

---

## Cell roster (the phase's input; `specs/vorago-phase14-presets-release/compliance.md:654-660`, roadmap 672–680)

| Cell | Showcase / host preset (`tools/vorago_preset_defs.h` line) | Verification | Role / bar | Sweep-5 reading (the "before") |
|---|---|---|---|---|
| E1 Partial → bloom | Bloom Colony (`:1040`) | route arms | primary: `d ≥ 4.0` and `d ≥ attribBase + 1.5` | 1.1823, attrib 1.1035 (2-take 2.21) |
| E4 Feedback → loop wake | Feeding Loops (`:1139`) | route arms | primary: same bar, **and the loops stay alive** (arm 2 on all takes) | 2.9182, attrib 0.9851. At 2× events: 8.48, but arms 2–3 red on three of four takes. At 1×: 3.71, with arm 2 red on every take |
| M2 Age | Erosion (`:711`) | macro reset | primary, `d ≥ 4.0` | 3.2191 (13c ladder best 2.83) |
| M4 Movement | Drifting Strata (`:771`) | macro reset | primary | 0.8745 (13c ladder 0.85–0.87, twelve macro-test reds per rung) |
| M5 Gravity | Stone Gravity (`:800`) | macro reset (Gravity is bipolar, reset to 0.5) | primary | 3.1864 (every 13c candidate lowered it) |
| M10 Life | Teeming (`:944`) | macro reset | primary | 0.9943 (13c ladder 0.82–0.99) |
| E6.hi sync rate high | Colony Pulse (`:562`), gate read with its Life raised in the override (FR-015, FR-025) | ExtReversion | **secondary**, `d ≥ 1.5` | 1.3700 |
| E7.hi self affinity high | Colony Pulse (`:562`), gate read with its Life raised in the override (FR-015, FR-025) | ExtReversion | **secondary**, `d ≥ 1.5` | 1.3430 |
| Movement row monotonicity | `VoragoMacro_SweepAxes` (`vorago_macro_test.cpp:1211`, row `:946-951`) | Spearman rho over 5 points × 3 seeds | `rho ≥ 0.9`, and endpoint ≥ +20 % per-band total variation | rho 0.8333 (the one `dsp_systems_tests` `[long]` red) |

---

## Existing components (verified this session)

### `VoragoVoice` — `dsp/include/krate/dsp/systems/vorago_voice.h`

- `class VoragoVoice` (`:229`).
- **Route levers** live in `void applyIdentityLanes(const IdentityLanes& lanes) noexcept` (`:2203-2285`):
  - the Partial pair, through `kPartialLaneGain = 1.0f` (`:1685`, `:2232-2233`);
  - the Feedback lane, through `shapeLeverInput(lanes.eco[kFeedback][l], kLeverInputGain[kFeedback])` →
    `loopGainOffset_[l] = kLoopGainLeverSpan * x` (`:2274-2276`), and the ring coupling (`:2277-2283`);
  - the loop wake, `ecology_.setLoopWake(l, combineWake(loopWakeBase_[l], …))` (`:2224-2228`).
- **Lever constants:**
  - `kLoopGainLeverSpan = 0.18f` (`:1681`);
  - `kRingCouplingBase = 0.06f` (`:1694`), `kCouplingLeverSpan = 0.30f` (`:1695`);
  - `kLeverInputGain{1, 2, 2, 2, 1}` (`:1742-1743`); Partial and Ghost are asserted unshaped (`:1744-1747`);
  - `kPeakWakeBase = kLoopWakeBase = 0.45f` (`:430-431`), `kMinRetunedWakeBase = 0.05f` (`:1750`);
  - `kWanderLeverRateCompExponent = 0.75f` and `static float wanderLeverRateComp(float rateHz) noexcept`
    (`:1725-1731`);
  - `kFreqWanderLeverSpanSemis = 3.0f` (`:1701`).
- **Bloom:** `kCloudRichnessFloor = 0.6346f` (`:379`), `kBloomChildGain = 1.5f` (`:381`).
- **Life lanes:** `kBreathGravityLaneGain = 3.0f` (`:390`), `kTidalRate = 0.8f` (`:394`).
- **Ecosystem setters:**
  - `void setEcosystemDepth(float d) noexcept` (`:1501`, clamp [0, 1]);
  - `void setEcosystemDepthFor(EcosystemEngine::Kind dest, float d) noexcept` (`:1528`);
  - `void setEcosystemSyncRate(float v) noexcept` (`:1547`), a straight pass-through to
    `EcosystemEngine::setSyncRate`;
  - `void setEcosystemSelfAffinity(float v) noexcept` (`:1556-1561`), a straight pass-through that writes the affinity
    diagonal through `EcosystemEngine::setAffinity(k, k, v)`;
  - **Sync rate and self-affinity are not lanes in the voice.** They have no gain, span or input-gain term of their
    own. They reach the sound only through the knob values and `EcosystemEngine`'s own simulation, whose outputs
    arrive as the per-`Kind` lanes `lanes.eco[kind]` that `applyIdentityLanes` writes to their destinations
    (`:2203-2285`; `kLeverInputGain` is indexed per `EcosystemEngine::Kind`, `:1742-1743`).
  - `void setEventRateScale(float s) noexcept` (`:1566`, clamp [0.1, 10]).
- `static constexpr float combineWake(float base, float eco, float sched) noexcept` (`:1091`) is **not** changed.

### `VoragoEngine` — `dsp/include/krate/dsp/systems/vorago_engine.h`

- `class VoragoEngine` (`:219`); `kMaxVoices = 6` (`:231`); `kOutputCeilingDb = -0.3f` (`:261`).
- **The 13c / Phase 14 measurement seams.** Each fans out over `kMaxVoices`, and prepare() still installs the compiled
  values. The four `VORAGO_PILOT_LEVER` keys map one-to-one onto them (`preset_pilot_test.cpp:922-931`):
  - `void setGhostTapMakeupDb(float dB) noexcept` (`:1021`) ← `ghostTapDb`;
  - `void setBloomChildGain(float g) noexcept` (`:1040`) ← `childGain`;
  - `void setEcologyWetMakeupDb(float dB) noexcept` (`:1047`) ← `ecologyWetDb`;
  - `void setGhostDensity(float grainsPerSecond) noexcept` (`:1056`) ← `ghostDensity`.
- The registered colony knobs fan out to every voice through `voice.setEcosystemSyncRate(…)` and
  `voice.setEcosystemSelfAffinity(…)` (`:899-900`).

### `VoragoMacroMatrix` — `dsp/include/krate/dsp/systems/vorago_macro_matrix.h`

- `class VoragoMacroMatrix` (`:264`).
- `struct VoragoMacroRow { VoragoMacro macro; VoragoMacroTargetOwner owner; VoragoMacroTarget target; float base;
  float amount; ModCurve curve; }` (`:233-257`).
- `static constexpr std::size_t kNumRows = 51` (`:276`); the row table `kRows` (`:315`).
- `enum class VoragoMacroTarget : std::uint8_t` (`:140-170`). It includes `CloudMutation` (`:144`; the existing
  Entropy → CloudMutation row is at `:462`), `ResonanceGravity`, `ResonanceOctaveLock` and `BloomSpawnRateHz`.
- `[[nodiscard]] float contributionOf(const VoragoMacroRow& row) const noexcept` (`:1168`). Gravity is bipolar: its
  input is `(m − 0.5)·2`, with the sign kept.
- `evaluateAll() const noexcept` (`:1185`).
- `void setTargetBase(VoragoMacroTarget target, float base) noexcept` (`:1014`).
- The compile-time row predicates (`:1223-1231`).
- **The rows this phase may touch:** Age (`:342-359`), Movement (`:391-430`), Gravity (`:434-456`) and Life
  (`:632-665`).

### `BloomEngine` — `dsp/include/krate/dsp/systems/bloom_engine.h`

- `class BloomEngine` (`:193`).
- `void setDepth(float depth) noexcept` (`:537`).
- `void setSpawnRateHz(float hz) noexcept` (`:548`), capped at `kMaxSpawnRateHz = 0.05f` (`:285`).
- `void setParentCount(std::size_t k) noexcept` (`:556`).
- `void setChildGain(float gain) noexcept` (`:566`), clamped to [0, `kMaxChildGain = 2.0f`] (`:265`).
- `std::size_t reserveBase() const noexcept` (`:715`).
- Vorago-only: `grep -rl bloom_engine.h plugins/seraphis plugins/shared dsp/include/krate/dsp/systems/seraphis_*.h`
  returns 0.

### `EcosystemEngine` — `dsp/include/krate/dsp/systems/ecosystem_engine.h`

Read-only in this phase. Vorago-only (0 Seraphis consumers).

- `class EcosystemEngine` (`:143`).
- `enum class Kind : std::uint8_t { Partial, Resonator, Noise, Feedback, Ghost }` (`:282-288`).
- `void setSyncRate(float v) noexcept` (`:595`), clamp [0, 0.5].
- `void setAffinity(Kind from, Kind to, float v) noexcept` (`:707`). The matrix is not symmetrised.

### `FeedbackEcology` — `dsp/include/krate/dsp/systems/feedback_ecology.h`

Read-only unless FR-011's append-only rule applies. Vorago-only.

- `class FeedbackEcology` (`:185`).
- `kMaxCouplingPerPair = 0.5f` (`:269`), `kWakeSilenceEpsilon = 1.0e-6f` (`:428`), `kMaxWetGainDb = 36.0f` (`:588`).
- The Phase 5 FR-063 sleep edge, which clears a loop's audio (roadmap 774–786).

### `ResonanceDriftNetwork` — `dsp/include/krate/dsp/systems/resonance_drift_network.h`

Read-only. Vorago-only.

- `void setGravity(float g) noexcept` (`:633`), clamp [−1, 1].
- `void setOctaveLock(float lock) noexcept` (`:646`), clamp [0, 1].
- `kDefaultWanderRateHz = 0.03f`, `kMaxWanderRateHz = 1.0f` (`:147-149`).

### `HarmonicCloud` — `dsp/include/krate/dsp/systems/harmonic_cloud.h`

**Seraphis-shared (15 consumer hits); not modified.** This phase reaches it only through the voice's existing
forwarders.

### Pilot probe — `plugins/vorago/tests/integration/preset_pilot_test.cpp`

- `TEST_CASE("Vorago_PresetPilot_PrimaryProbe", "[.probe][vorago]")` (`:1347`). Its environment controls:
  - `VORAGO_PILOT_PRESET` (`:1384`);
  - `VORAGO_PILOT_OVERRIDE="id=norm,…"` (`:1389-1420`);
  - `VORAGO_PILOT_MASTER_TRIM` (`:1422-1427`);
  - `VORAGO_PILOT_TAKES=4` for the all-take arms (`:1027-1031`, `:1249`);
  - `VORAGO_PILOT_SECONDARY=1` / `VORAGO_PILOT_CELLS` for the secondary readout (`:1201-1208`);
  - `VORAGO_PILOT_ITERATE=verified27` for the as-compiled iteration (`:1342-1354`).
- `PilotLever readPilotLever()` takes the keys `childGain`, `ghostTapDb`, `ghostDensity` and `ecologyWetDb`
  (`:875-933`).
- `computeLeverTakes(…)` REQUIREs that the tweak was held on every take (`:939-1002`).
- Also in this file: `Vorago_PresetPilot_SwingProbe` (`:1457`).

### Harness — `plugins/vorago/tests/preset_test_support.h`

- `struct PresetDescriptor` (`:48`).
- `double descriptorDistance(const PresetDescriptor&, const PresetDescriptor&)` (`:174`).
- `kFloorF = 4.0`, `kSecondaryBar = 1.5` (`:249-250`); `kAttribMargin = 1.5` (`:979`).
- `bool verifiedAt(const CellOutcome&, Verification, …)` (`:1023`).

### Preset defs — `tools/vorago_preset_defs.h`

- `enum class Capability : std::uint8_t` (`:54`), `enum class Verification : std::uint8_t` (`:82`).
- `std::vector<Capability> requiredPrimaryCells()` (`:297`).
- The eight showcase and host rows listed in the roster table, and Smeared Horizon (`:437`).

### Sweep and matrix tests — `plugins/vorago/tests/integration/`

- `Vorago_PresetSweep_LongRender` (`preset_sweep_test.cpp:1047`) and `Vorago_PresetSweep_AblationVerifiesClaims`
  (`:1150`), both tagged `[long][vorago-sweep]`.
- `Vorago_PresetMatrix_CoverageComplete` (`preset_matrix_test.cpp:148`) and `Vorago_PresetMatrix_NoShowcaseSubset`
  (`:321`).

### Macro tests — `dsp/tests/unit/systems/vorago_macro_test.cpp`

- `VoragoMacro_SweepAxes`, tagged `[long]` (`:1211`):
  - `kSweepSeeds {101, 202, 303}` (`:646`) and `kSweepPoints {0, .25, .5, .75, 1}` (`:649`);
  - the Movement row (`:946-951`);
  - the assertions `CHECK(rho·sign ≥ 0.9)` and `CHECK(endpoint ≥ threshold)` (`:1318-1319`).
- `VoragoMacro_NoZipper`, tagged `[long]` (`:1514`).

### 13b probe — `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp`

`TEST_CASE("Vorago_EcosystemRuleProbe", "[.probe][vorago]")` (`:544`). It produces Gate 1 (GATE1M) and the 14-knob
Gate 2 tables, at the default surface and at Life max.

### Boundedness, determinism and fingerprint tests — `dsp/tests/unit/systems/`

- `VoragoEngine_CapabilityLeverBounded` (`vorago_ecosystem_lever_test.cpp:1974`).
- `VoragoEngine_SlotSeedReproducibility` (`vorago_engine_test.cpp:1840`).
- `VoragoEngine_GhostExtensionWiring` (`vorago_ghost_ext_test.cpp:418`).

### CPU tests

- `VoragoEngine_CpuBudget`, tagged `[.perf]` (`dsp/tests/unit/systems/vorago_perf_test.cpp:971`). Its constants
  `kReferenceNs`, `kEngineBaselineNsAtPoly4` and `kCavernMeasuredNsPerBlock` are in `vorago_perf_budget.h`.
- `Vorago_ProcessorCpu` (`plugins/vorago/tests/integration/processor_cpu_test.cpp:188`): P/D ≤ 1.05.
- `Vorago_PresetCpu` (`plugins/vorago/tests/integration/preset_cpu_test.cpp:173`): `kPresetCostCeiling = 1.15`
  (`:61`).

### Lane runners — `tools/run-close-lanes.js`, `tools/run-cpu-tests.js`

- `run-close-lanes.js` runs the close lanes in parallel and excludes `~[performance]~[perf]~[benchmark]~[!benchmark]`
  (`:1-38`).
- `run-cpu-tests.js` runs the timing lane alone.

## New components

**None.** Roadmap Phase 13d names no new class (roadmap 663–707). The work is:

- retuned constants, rows and route wiring inside the Vorago-owned headers `vorago_voice.h`, `vorago_engine.h` and
  `vorago_macro_matrix.h`;
- new `VORAGO_PILOT_LEVER` keys and measurement seams for the ladders (FR-012);
- probe reporting;
- the confirming pass's preset-def edits.

A new `VoragoMacroTarget` enumerator or a new `kRows` row is data, not a class. It is still subject to FR-038's sweep
and to the compile-time row predicates.

### ODR sweep — run this session

Each name a plan might reach for was swept with `grep -rn "<Name>" dsp/ plugins/ tools/`, counting any occurrence. Every
one returned 0 hits, so **all are clear**:

`EngineCeiling`, `EngineCeilings`, `CeilingLever`, `CeilingProbe`, `VoragoCeilingProbe`, `kLoopWakeLaneGain`,
`kFeedbackLaneGain`, `kPartialBloomLaneGain`, `kBloomRouteGain`, `kLoopSustainFloor`, `kSyncRateLaneGain`,
`kSelfAffinityLaneGain`, `kMovementRateComp`, `MovementRow`, `kLifeSpawnAmount`, `kAgeMutationAmount`, `kGravityTarget`.

FR-038 requires the plan to re-run the sweep for every name it actually introduces.

---

## Functional Requirements

### A. Roster, instrument, before-record

- **FR-001 — Cell roster.** The gated items are exactly the nine rows of the "Cell roster" table:
  - the six primaries;
  - the two secondaries;
  - the Movement row's monotonicity.

  No cell is added or dropped, and no cell's role or host changes. Every other cell is covered in two other places:
  - the no-regression sets (FR-030, FR-030b);
  - the confirming pass's re-read (FR-043).

  [roadmap 670–680, 683–686]
- **FR-002 — Same instrument.** Every gate figure is read from `Vorago_PresetPilot_PrimaryProbe`
  (`preset_pilot_test.cpp:1347`), and its scoring does not change:
  - the stored-seed take;
  - twins over M1…M3, on the preset's own timeline;
  - the C-7.2 descriptor, computed through `preset_test_support.h` only;
  - route cells on their four route arms, with the attributability clause;
  - secondaries through the `VORAGO_PILOT_SECONDARY=1` readout.

  The probe may gain **reporting** and **lever seams** only (FR-012). No bar, window, take count or distance changes.

  A gate figure is taken with no `VORAGO_PILOT_LEVER` set, so it reads the compiled constants. On a lever run, the
  probe REQUIREs that the tweak was held on every take (`:939-1002`). [roadmap 683–687, 693–694]
- **FR-003 — Before-record.** Before any lever lands, the `502e5243` tree is recorded with nothing else running. The
  logs are checked into `specs/vorago-phase13d-engine-ceilings/artifacts/`. The record has three parts:
  - **(i) Roster cells.** Every roster cell is probed exactly as compiled, on all four takes (`VORAGO_PILOT_TAKES=4`).
    - Each reading must agree with its sweep-5 row within the 13c reproduction tolerance:
      `|d_base − d_sweep5| ≤ max(0.01, 0.005·d_sweep5)`.
    - The same tolerance applies to `attribBase` where it is printed.
    - One cell is read twice, to record the repeat spread.
    - E4 is also read at 1.0× events (`VORAGO_PILOT_OVERRIDE="800=0.5"`, the 1.0× normalised value,
      `tools/vorago_preset_defs.h:249`), against the roadmap's premise of 3.71 with arm 2 red on every take. Its
      FR-004 loop-level readout is recorded on all four takes at both rates.
    - A reading outside the tolerance is a stop-and-surface item. The tolerance is never widened.
  - **(ii) Macro tests.** `VoragoMacro_SweepAxes` and `VoragoMacro_NoZipper` (`[long]`) are run with every assertion
    logged. This captures:
    - the base set of passing assertions that the final tree must keep;
    - the Movement rho of 0.8333.
  - **(iii) 13b tables.** 13b's Gate 1 and Gate 2 tables are run with `Vorago_EcosystemRuleProbe`, at the default
    surface and at Life max. The final tree is compared against this base, not against 13b's shipped tree, because 13c
    ruling B-16 surfaced the Life-max knobs.

  [roadmap 695: "every before/after descriptor is recorded"]
- **FR-004 — Lever-cell readouts.** The probe prints, in the same run as the gate, the terms each lever needs in order
  to be ruled. The readouts stay hidden (`[.probe]`) and are never a per-push gate.
  - **E4:**
    - arm 2 and arm 3 for the stored take and for all four takes (already printed under `VORAGO_PILOT_TAKES=4`);
    - a per-take reading of the **wet loop sum**: the per-loop taps of `FeedbackEcology::processBlockTapped`
      (`feedback_ecology.h:979`) summed over the loops and over the voices, as RMS over every 10 s window in `[A, H]`.
      This makes "the loops stay alive" (roadmap 685) measurable on its own, apart from the whole-output arm 2. The
      in-place ecology output (dry plus wet, `vorago_voice.h:2610`) is not this signal, because it carries the dry
      excitation and never goes silent. The plan chooses the read-only tap mechanism.
  - **E6.hi / E7.hi:** the ExtReversion `d`, the bar 1.5, the state predicate and the conjunct (the 13c
    `VORAGO_PILOT_SECONDARY` readout).

  [roadmap 685, 692]
- **FR-005 — Default-surface record.** The default surface is rendered before and after, and recorded on two
  instruments:
  - the 13b probe at the default surface: t0, `t0on`, M1 RMS;
  - the Phase 14 pilot's P0 row: stored-seed descriptor, M1 RMS.

  [roadmap 695, 705]

### B. Levers (one per feature, each measured before it is ruled)

- **FR-010 — Measure first.**
  - No lever is adopted until the probe has read the target cell with it and the reading is in a checked-in log.
  - Where the Overview states a mechanism read from code, the plan confirms it with an existing or added hidden
    instrument before choosing the lever. One example is Teeming's clamped EcosystemDepth row.
  - Each candidate is laddered by `VORAGO_PILOT_LEVER` or by rebuild, as in 13c. A ladder has at least three rungs, or
    every rung up to the component's clamp.

  [roadmap 686–687]
- **FR-010b — Fixed lever order.** Levers are measured and ruled in this order:
  1. **E1** (FR-013);
  2. **E4** (FR-014);
  3. **E6.hi / E7.hi** (FR-015);
  4. **M2** (FR-016);
  5. **M5** (FR-017);
  6. **M4 and the Movement rho** (FR-018);
  7. **M10 Life, last** (FR-019).

  Each lever is measured on the tree with every earlier lever already landed. Life is last because its rows drive the
  ecosystem depth, the event rate and the bloom spawn rate, and those are the destinations of E1, E4 and E6/E7 (13c
  E-5). If measuring a later lever drops an earlier, already-ruled cell below its bar, the earlier lever is re-opened
  and re-ruled before work continues. [roadmap 686, "as in 13c"]
  - **Termination rule.** Each lever may be re-opened and re-laddered **at most once** after a later lever drops its
    cell under the bar (13c re-opened S4 exactly once).
  - A **second** drop of the same cell is an FR-027 stop-and-surface ruling for the user with the ladder readings of
    both ladders. It is never a third ladder. (SC-027)
- **FR-010c — Per-lever regression read set.** Before a lever is ruled, and for every rung presented to the user, the
  probe re-reads, as compiled with the rung in place:
  - **(a)** every earlier roster cell in FR-010b's order (its gate, arms and attributability as applicable);
  - **(b)** every sweep-5-verified primary or secondary on every preset where that lever's path is **live**.

  A lever's path is **live** in a preset when:
  - for a macro-row lever: the preset stores that macro away from its neutral (0, or 0.5 for Gravity);
  - for a route or lane constant: the preset's ecosystem depth feeding that route is > 0;
  - for a compiled constant on a component path: the preset's stored settings make that component sound (a non-zero
    depth, mix or level for it).

  When liveness cannot be decided by these rules, the preset counts as live. Each reading goes into a checked-in log
  under `artifacts/`, cited from the lever table (FR-050). These readings are what make FR-010b's re-open trigger and
  the "not adopted" conditions of FR-013, FR-017, FR-030 and E-1 observable when the decision is made.
- **FR-010d — Pre-authorised adoption rule.** The build adopts each lever itself, without a user stop, under this rule:
  - It adopts the **smallest-change rung** (the fewest mechanisms, then the smallest step from the compiled value) that
    clears **all** of:
    - the cell's gate (FR-020, FR-021, FR-021b or FR-022, as applicable);
    - the FR-010c read set;
    - the four level arms of FR-024, including arm 1 at 44.1 kHz;
    - the `VORAGO_PILOT_MASTER_TRIM=-6` re-read (FR-024c).
  - **The user rules only these items:**
    - every stop-and-surface item (FR-027, a second drop of the same cell under FR-010b's termination rule, and any
      other item this spec names as one);
    - E1's child-attachment change (FR-013);
    - an FR-030 conflict (a sunk preset whose confirming-pass re-author fails, FR-030).
  - Every adoption is logged in `artifacts/rulings.md` (FR-053) with its ladder readings and the adoption rule's clauses
    it cleared. A rung that clears none of the rule's clauses is never adopted by this rule; the cell goes to FR-027.
  - Each lever's mechanism and value (OQ-1) are chosen on its logged ladder readings under this rule.
- **FR-011 — Vorago-owned code only.**
  - Every lever lands in `vorago_voice.h`, `vorago_engine.h`, `vorago_macro_matrix.h`, or the Vorago plugin or test
    tree.
  - `bloom_engine.h`, `feedback_ecology.h`, `resonance_drift_network.h` and `ecosystem_engine.h` may be touched only by
    an append-only change. That change:
    - keeps the component's own unit suite green with no test edited;
    - is itemised in the lever table (the 13c B-3 / B-8 pattern).
  - No Seraphis-consumed header is modified (FR-037).

  [roadmap 787–789, the shared-component rule]
- **FR-012 — Ladder seams.** For each lever family that is a compiled constant or a macro row, the plan does one of
  two things:
  - **(a)** It adds a `VORAGO_PILOT_LEVER` key and a matching `VoragoEngine` measurement seam. The seam follows the
    13c pattern:
    - prepare() installs the compiled value;
    - the seam fans out over `kMaxVoices`;
    - non-finite input is rejected;
    - the probe REQUIREs that the tweak was held on every take.
  - **(b)** It declares the family rebuild-laddered.

  An unknown key still fails the run (`preset_pilot_test.cpp:910-911`). [roadmap 686–687]
- **FR-013 — E1 Partial → bloom.** The lever is **the bloom route's gain and the children's attachment to a sounding
  parent** (roadmap 687–688).
  - It applies in two places:
    - the Partial-lane write to `bloom_.setDepth(…)` (`vorago_voice.h:2233`);
    - the parent/child hand-off to `BloomEngine`.
  - The Partial lane's mutation write (`:2232`) uses the same lane. Whether the route gain is split per destination is
    a plan choice, and both ways are measured.
  - The 13c always-on cloud floor and child gain (`:379`, `:381`) stay in force unless they are re-ruled under
    FR-010b.
  - Both halves are laddered. Any change to the child attachment (including dropping it, if the ladder shows it adds
    nothing) is a user ruling logged in `artifacts/rulings.md` (FR-053), never an FR-010d adoption and never a silent
    plan choice.
  - The gate is E1's route-arm gate (FR-021), and the attributability clause binds it.
  - 13c declined its "extension" candidate because it lowered S6 to 1.27–2.50 (13c `compliance.md` §1, routes row). A
    rung that de-verifies S6 Slow Bloom follows FR-030: it may be adopted, and S6 Slow Bloom must re-verify through its
    probe re-author in the confirming pass.
- **FR-014 — E4 Feedback → loop wake.** The lever is **the loop-wake route gain at 1× events, with the loops' own
  sustain preserved** (roadmap 688).
  - **The primary lever is always the loop-wake route gain**, the `combineWake(loopWakeBase_[l], eco, sched)` write
    (`vorago_voice.h:2224-2228`). The other Feedback-lane writes, `loopGainOffset_` and ring coupling (`:2274-2283`),
    may move only alongside it, and each such companion change is itemised in the FR-050 lever table.
  - **The gate is read at 1.0× events**: `kEventsRateScaleId` = 0.5 normalised (the 1.0× default,
    `tools/vorago_preset_defs.h:249`). Feeding Loops stores 0.389076 (0.6×, `:1159`), so the confirming pass
    re-authors its events rate to 0.5 (FR-025, FR-040). Until then the gate is read through
    `VORAGO_PILOT_OVERRIDE="800=0.5"`.
  - A candidate that reaches the bar only at an events rate above 1.0× is not a lever. The 2.0× premise reading is the
    counter-example: 8.48, with arms 2–3 red on three of four takes. The 1.0× premise reading (3.71, arm 2 red on every
    take) is the before for this gate.
  - FR-004's loop-level readout is printed for every rung.
  - The loops may be falling silent because of the Phase 5 FR-063 sleep edge, which clears audio state. If so, the
    cross-cutting "Dormancy" exception (roadmap 774–786) stays as written: the lever may not suppress the clear. It may
    only change when and how often the sleep edge fires.
- **FR-015 — E6.hi / E7.hi.** The lever is **the sync-rate and self-affinity lanes' audible range on a Life-high
  surface** (roadmap 692).
  - **What is real in the code:** sync rate and self-affinity are not lanes in the voice. `setEcosystemSyncRate`
    (`vorago_voice.h:1547`) and `setEcosystemSelfAffinity` (`:1556-1561`) pass straight through to
    `EcosystemEngine::setSyncRate` / `setAffinity`, fanned out by the engine at `vorago_engine.h:899-900`. The
    knobs reach the sound only through the simulation's per-`Kind` output lanes `lanes.eco[kind]`, which
    `applyIdentityLanes` writes to their destinations (`vorago_voice.h:2203-2285`).
  - So the roadmap's "sync-rate and self-affinity lanes" (roadmap 692) has no dedicated term to retune. **The mechanism
    is open and is found by instrument first (FR-010):** a hidden readout identifies which per-`Kind` lanes carry
    the difference between Colony Pulse's E6.hi / E7.hi state and its ExtReversion twin.
  - The candidate surfaces, once that readout names the lanes, are:
    - the per-`Kind` destination lane gains, lever spans and input gains (`kLeverInputGain`,
      `vorago_voice.h:1742-1743`) of those lanes;
    - an append-only change in `ecosystem_engine.h` under FR-011 that exposes the knob's effect more strongly without
      changing the simulation's rules.
  - It does not act on the `EcosystemEngine` rules, or on the registered knob ranges (`setSyncRate` clamps to
    [0, 0.5]; `setEcosystemSelfAffinity` clamps to [−2, 2]; `ecosystem_engine.h:595`, `:707`;
    `vorago_voice.h:1549-1555`).
  - **The gate surface is Colony Pulse with its Life raised in the gate override.** Colony Pulse stores Life 0.45
    (`tools/vorago_preset_defs.h:574`). The raised Life value is a ladder value chosen under FR-010d, recorded in the
    override string, and transcribed into Colony Pulse by the confirming pass (FR-025, FR-040). This is a named FR-025
    exception, like E4's 800.
  - Colony Pulse's S7 and D13.2 must re-verify with the raised Life (FR-022).
  - Each rung is read in two places:
    - on Colony Pulse with the raised Life, which is the gate;
    - on 13b's Life-max surface, with `Vorago_EcosystemRuleProbe`, as a recorded readout.
  - The selfAffinity and syncRate rows of the Life-max table are recorded alongside. These are the knobs 13c ruling
    B-16 surfaced.
- **FR-016 — M2 Age.** The lever is **the Age row's targets — damping, tilt, mutation — widened or re-aimed** (roadmap
  688–689), on the rows at `vorago_macro_matrix.h:342-359`.
  - An Age → `CloudMutation` row is admissible. It is a new `kRows` entry, which takes `kNumRows` from 51 to 52, and
    every compile-time predicate must still hold.
  - 13c already measured three rungs: tilt −7 (2.7865), damping 0.70 (2.7900) and an Age → mutation row (2.8273). These
    are the floor of the ladder, not rungs to repeat. Every rung in the plan must differ from them in target or amount.
- **FR-017 — M5 Gravity.** The lever gives **the Gravity row (resonance gravity, octave lock) a target the descriptor
  hears** (roadmap 690–691).
  - A new or re-aimed target is admissible only if its contribution is bipolar through `contributionOf`
    (`vorago_macro_matrix.h:1168-1176`), so that both halves of the knob travel (13c E-6).
  - Stone Gravity's arms 1 and 3 were red before 13c (13c FR-015). For every rung they are printed on all four takes and
    at 44.1 kHz. A rung that re-opens either arm is not adopted.
- **FR-018 — M4 Movement and the Movement row's monotonicity.** The lever **re-aims the Movement row (drift depth,
  wander rates, breathing, damper) so that it both moves the descriptor and stays monotone** (roadmap 689–690). It acts
  on the rows at `vorago_macro_matrix.h:391-430`.
  - Each rung is read three ways:
    - on Drifting Strata, with the probe;
    - with `VoragoMacro_SweepAxes`;
    - with `VoragoMacro_NoZipper`.
  - A rung is admissible only if all of these hold:
    - Movement rho ≥ 0.9;
    - Movement endpoint ≥ +20 %;
    - every other SweepAxes assertion that passed in FR-003 (ii) still passes;
    - NoZipper stays within its bound.
  - The 13b wander-lever rate compensation (`kWanderLeverRateCompExponent`, `vorago_voice.h:1725`) may be re-measured as
    part of the same set, because it was tuned against this same assertion.
- **FR-019 — M10 Life (ruled last).** The lever acts on **the Life row: ecosystem depth, event rate, bloom spawn**
  (roadmap 691–692), on the rows at `vorago_macro_matrix.h:632-665`.
  - Before laddering, the plan records each Life row's summed value on Teeming, from the matrix readback, at stored
    Life 0.7 and at the ablation reset, with the instrument FR-010 requires.
  - The code-read candidate for clamping is the `EcosystemDepth` row only: its 0.85 base is overridden to 1.0 by
    Teeming's stored depth (`tools/vorago_preset_defs.h:954`), and the setter clamps at 1. The readback confirms it.
  - The `EventRateScale` row is not presumed clamped: by code-read it sums to about 6.5 on Teeming (Overview). Its
    measured value decides.
  - A rung that only raises a row the readback shows clamped is not a candidate.
  - Teeming's stored ecosystem depth 1.0, which clamps the `EcosystemDepth` row, may be un-clamped (lowered) in the gate
    override only as a **companion** to an adopted Life engine lever, under FR-025b. It is itemised in the lever table
    and is never the sole fix.
  - S7 Teeming (verified at 13c at 10.1021) and D13.1 are read alongside.

### C. The gate

- **FR-020 — Gate, macro primaries (M2, M4, M5, M10).** On the final tree, the probe on the cell's showcase preset
  reads `d ≥ 4.0`. [roadmap 683–684]
- **FR-021 — Gate, route primaries (E1, E4).** Both conditions hold:
  - `d(R_k, R_k0) ≥ 4.0`;
  - `d(R_k, R_k0) ≥ d(R_0, R_00) + 1.5` (the attributability clause).

  [roadmap 683–684, 693–694]
- **FR-021b — E4 loops alive.**
  - Feeding Loops' arm 2 (every 10 s RMS over `[A, H]` ≥ −60 dBFS) and arm 3 are green on **all four** takes, not only
    on the stored take.
  - **The loops themselves stay alive:** on each of the four takes, FR-004's **wet loop sum** (the per-loop taps of
    `FeedbackEcology::processBlockTapped` summed over loops and over voices) has RMS ≥ −60 dBFS over every 10 s window
    in `[A, H]`. This is arm 2's floor and window applied to the loop bus itself (roadmap 688: "the loops' own sustain
    preserved (arm 2)"), so a lifted bed cannot turn the gate green while the loops are silent. One live loop carries
    the gate: the sum, not every individual loop tap, must meet the floor. It is a gated conjunct of the E4 gate, not a
    recorded figure.
  - If the before-record (FR-003) shows the loop bus failing this on the stored take at 1.0×, the bar is not lowered:
    that reading is the lever's starting point.
  - The premise for this requirement: the loops fell silent on three of four takes (roadmap 674–675, 685).
- **FR-022 — Gate, secondaries (E6.hi, E7.hi).** On Colony Pulse **with its Life raised in the gate override**
  (FR-015, FR-025), each gives `verifiedAt(…, ClaimRole::Secondary)` true (`preset_test_support.h:1023`):
  - `d ≥ 1.5`;
  - its state predicate holds;
  - its conjunct holds.

  Colony Pulse's S7 and D13.2 must re-verify with the same raised Life (read as part of the same probe pass and again
  after the confirming pass, FR-040). A failed re-verify is a stop-and-surface item (FR-027).

  [roadmap 684]
- **FR-023 — Gate, Movement row.** On the final tree, `VoragoMacro_SweepAxes` passes the Movement row's rho assertion
  and endpoint assertion, with the thresholds unchanged. [roadmap 685–686, 697]
- **FR-024 — Level arms in the same printout.** A gate counts only if the same probe run prints all four arms of the
  stored take as green:
  - arm 1: peak ≤ 0.9661 and every 10 s RMS ≤ −6 dBFS;
  - arm 2: ≥ −60 dBFS over `[A, H]`;
  - arm 3: within [−18, +12] dB;
  - arm 4: the tail criterion.

  The same run also reads arm 1 at 44.1 kHz, with the 13c FR-024b instrument. [roadmap 684, 694]
- **FR-024c — Not a limiter pass.** Each roster gate is re-read with `VORAGO_PILOT_MASTER_TRIM=-6`, and the cell must
  still reach its bar. This repeats 13c FR-024c; 13c E-2 explains why: arm 1 alone does not exclude a render the limiter
  is holding.
- **FR-025 — Override strings are part of the gate record.**
  - A gate read with `VORAGO_PILOT_OVERRIDE` records the exact string in the lever table and in the compliance row. That
    string is what the confirming pass transcribes (FR-040).
  - The override may change only the cell's own controls (the macro, route depth or knob under test), plus level trims.
    Two named exceptions:
    - E4's gate sets `kEventsRateScaleId` (800) to 0.5, the 1.0× rate the roadmap names (FR-014). The confirming pass
      transcribes that value into Feeding Loops.
    - E6.hi / E7.hi's gate raises Colony Pulse's Life (FR-015). The confirming pass transcribes that value into Colony
      Pulse.
  - Beyond those, a gate override may carry a preset-level change outside the cell's own controls only as a companion
    under FR-025b.
  - The 36 sweep-5-verified primaries are read **as compiled** (FR-030), except a preset that FR-030 records as sunk,
    which is read after the confirming pass.
  - A gate read with no override records "as compiled".

  [roadmap 699–700]
- **FR-025b — Preset-level companions.**
  - A preset-level change beyond the cell's own controls (for example, Teeming's stored ecosystem depth 1.0 lowered so
    the Life → EcosystemDepth row is un-clamped) is allowed in a gate override **only as a companion to an adopted
    engine lever**.
  - It is never the sole fix. For each cell that uses a companion, the gate is also read with the companion alone on the
    base tree (no engine lever), and that reading is logged. If the companion alone clears the gate, the cell is not
    fixed by an engine lever, and it becomes a stop-and-surface item (FR-027).
  - Each companion is itemised in the FR-050 lever table with its exact override string, and is transcribed by the
    confirming pass (FR-040).
- **FR-026 — Final-tree re-measure.** Once every lever is in, in FR-010b's order and including any re-opened levers,
  every gate is re-run once on the final binary with nothing else running. The compliance row cites that log line, never
  a mid-phase reading. [roadmap 694]
- **FR-027 — No UNMET.**
  - If no measured lever brings a cell to its gate, the cell becomes a **stop-and-surface ruling** for the user, with
    the ladder readings.
  - A cell dropped a second time by a later lever (FR-010b's termination rule) is also a stop-and-surface ruling under
    this requirement, never a third ladder.
  - It is never recorded as UNMET, and never as met.
  - No bar is relaxed.

  [roadmap 701–702, 706–707]

### D. No regression

- **FR-030 — Verified primaries stay verified.** On the final tree, each of the 36 primaries verified at sweep 5 is
  probed, and it reaches its bar with its arms green. It is probed **as compiled**, except a preset a lever has sunk
  (below), which is probed **after the confirming pass**.
  - The set is derived, not hand-typed: `requiredPrimaryCells()` (`tools/vorago_preset_defs.h:297`, 42 cells) minus the
    six roster primaries.
  - Each cell's before value is its sweep-5 record (`f:/tmp/p14/sweep5-out/record_*.txt`, cited from Phase 14
    compliance "Sweep 5").
  - **A lever that rescues a roster cell but sinks a sweep-5-verified cell on another preset may be adopted** (FR-010d).
    The sunk preset is then recorded in the lever table and in `artifacts/rulings.md` with both readings, joins the
    affected set (FR-040), and **must re-verify through its probe re-author in the confirming pass**.
  - The sunk cell is surfaced to the user (FR-010d, FR-027) only if that re-author fails to re-verify it, with both
    readings.

  [roadmap 680, 695–696]
- **FR-030b — Secondary and default-state verifications stay verified.** Every cell verified at sweep 5 in either of
  these ways stays verified on the final tree:
  - as a claimed secondary on a host preset;
  - in the measured default-state set (`sweep5_aggregate.log`, the `CoverageComplete` matrix).

  The sunk-preset rule of FR-030 applies to a secondary on a host preset: it may be sunk by an adopted lever, and must
  re-verify after the confirming pass. A before/after table is kept. [roadmap 695–696]
- **FR-031 — Phase 10 bounds.** These stay green:
  - `VoragoMacro_NoZipper` (bound 1.5×);
  - `VoragoMacro_SweepAxes`: every assertion that passed in FR-003 (ii) still passes, and **the Movement row passes
    too** (FR-023);
  - the Phase 10 overnight and boundedness soaks.

  [roadmap 695–697]
- **FR-032 — 13b's and 13c's gates.**
  - 13b Gate 1: GATE1M ≥ 0.5 at the default surface and at Life max.
  - 13b Gate 2: every knob that counts on the FR-003 (iii) base tables still counts.
  - 13c roster cells: every one that passed at 13c's close and is verified at sweep 5 stays verified. The set is S4, S6,
    S9, M3, M9, M12, D9.1 and D14.1, plus E3 and E5, which became verified at sweep 5 by re-author.
  - These tests stay green with no edit:
    - from 13b: `VoragoVoice_EcosystemLever*`, `VoragoVoice_WakeCombineRule`, `VoragoVoice_AgentReductionRule`;
    - from 13c: `VoragoVoice_BloomChildrenAudible`, `VoragoVoice_RouteLeverZeroAtZeroLane`,
      `VoragoEngine_GhostDensityRoute`.

  [roadmap 695–696]
- **FR-033 — CPU.** Measure with `node tools/run-cpu-tests.js`, with nothing else running and after the machine has
  idled. All of these pass:
  - `VoragoEngine_CpuBudget` clauses (i) and (ii);
  - `Vorago_ProcessorCpu`: P/D ≤ 1.05;
  - `Vorago_PresetCpu`: every factory preset ≤ `kPresetCostCeiling` 1.15 × the default surface.

  The before figures (base binary) and the after figures are recorded in ns and %. Clause (i) is judged by the 13c B-2
  alternating pinned A/B against the base binary. [roadmap 705–706: "CPU delta", "the CPU lane alone"]
- **FR-034 — Suites.**
  - The Phase 2–14 per-push suites pass: `dsp_*_tests`, `vorago_tests`, `seraphis_tests`, `shared_tests`.
  - The `[long]` soaks pass.
  - Both run as the parallel close lanes, through `node tools/run-close-lanes.js`.
  - Some pre-existing tests encode the old voicing **as data**. Each one is listed with its reason before it is
    re-measured, in the shape of 13c FR-034's surfaced list. Such a test is never edited silently.

  [roadmap 696, 705]
- **FR-035 — Fingerprints re-harvested in their consuming binary.**
  - Every fingerprint that moves is re-harvested once, on the final tree, inside the binary that consumes it.
  - This includes `VoragoEngine_GhostExtensionWiring` clause (a), which is on its eighth harvest.
  - Each harvest runs twice, the two runs give byte-identical literals, and it gets a new PROVENANCE block.

  [roadmap 697–698]
- **FR-036 — Cross-cutting gates.**
  - zero warnings;
  - `node tools/check-portability.js` clean, then `wsl --shutdown`;
  - clang-tidy `dsp` and `vorago`: 0 findings;
  - pluginval strictness 5 on `Vorago.vst3`.

  [roadmap 696]
- **FR-037 — Seraphis untouched.**
  - `git diff --name-only 502e5243..HEAD` lists none of these:
    - any `plugins/seraphis/**` path;
    - `harmonic_cloud.h`, `atmosphere_engine.h`, `continuous_body.h`, `aether_reverb.h`, `entropy_processor.h`;
    - any life-modulator header;
    - any `seraphis_*.h`.
  - `seraphis_tests` is green.

  [roadmap 787–789]
- **FR-038 — ODR.** Before any new class, struct, enumerator or constant name is written, it is swept with
  `grep -rn "<Name>" dsp/ plugins/ tools/`, and the sweep is recorded in the plan. [roadmap 772]
- **FR-039 — Surface unchanged.**
  - The registered parameter count, IDs, types, ranges and defaults are unchanged.
  - `kCurrentStateVersion == 3`.
  - The v2 and v3 round-trips are green.

  [Non-goals]

### E. The confirming pass (Phase 14 T048 re-run)

- **FR-040 — Re-author on the new levers.** The confirming pass treats presets in two tiers.
  - **Full re-author** (an override search followed by a defs edit, by probe) for:
    - the roster's showcase and host presets (including Feeding Loops' events rate, FR-014, and Colony Pulse's raised
      Life, FR-015);
    - the three showcase-subset presets: Hull Resonance, Smeared Horizon and Haunted Colony (FR-041);
    - any preset recorded as sunk by FR-030 / FR-030b.
  - **Re-measure, re-author on a drop** for every other affected preset. Its claimed cells are re-measured by probe on
    the final tree. It is re-authored (override search and defs edit) only if a claimed cell's reading **drops by more
    than 0.01** against its sweep-5 record, or a cell it verified at sweep 5 is **no longer verified**. Otherwise its
    defs stay untouched.
  - Companion changes (FR-025b) and gate-override exceptions (FR-025) are transcribed into the defs of their preset.

  A preset is **affected** when either holds:
  - any adopted lever's path is live in it (FR-010c's definition);
  - for any of its claimed cells, the final-tree probe reading differs from its sweep-5 record by more than 0.01.

  A preset counts as affected unless both are shown false with a cited log line.

  The re-author follows the Phase 14 sweep-5 loop:
  - `VORAGO_PILOT_OVERRIDE` batches;
  - each adopted override transcribed into `tools/vorago_preset_defs.h`, with a comment;
  - the presets regenerated;
  - `Vorago_FactoryPresets_TreeMatchesGenerator` green.

  [roadmap 699–700]
- **FR-041 — Showcase-subset re-author.**
  - `Vorago_PresetMatrix_NoShowcaseSubset` reads "SUBSET pairs: 51" (`sweep5_aggregate.log:2612`). In
    `sweep5_aggregate.log` the 51 `SUBSET` lines name three P presets: Hull Resonance (25 pairs), Smeared Horizon (18,
    `tools/vorago_preset_defs.h:437`) and Haunted Colony (8).
  - **All three** are re-authored at preset level in this pass, fully (FR-040), and the three join the affected set.
  - The target is `SUBSET pairs: 0`, covering all three presets.
  - Any pair that remains is a stop-and-surface ruling under FR-027's rule, never UNMET.

  [roadmap 700–701]
- **FR-042 — Shards and aggregate.**
  - Re-run every sweep shard that contains an affected preset, then the aggregate, with `VORAGO_SWEEP_SHARD=i/N`.
  - The full sweep is not required. The roadmap's reason: the probe predicts the sweep within 0.01 (roadmap 680–682).
  - A shard verdict that disagrees with its probe reading by more than 0.01 is a stop-and-surface item.
  - Unaffected presets keep their sweep-5 records, and the aggregate reads them back (`VORAGO_SWEEP_IN`).
  - Before the aggregate reads them, those records are copied from `f:/tmp/p14/sweep5-out/record_*.txt` into
    `specs/vorago-phase13d-engine-ceilings/artifacts/sweep5-records/`, with each file's SHA-256 recorded in
    `artifacts/sweep5-records.sha256`. The aggregate reads the checked-in copies only.

  [roadmap 682, 700]
- **FR-043 — T063 re-read.** The rows of Phase 14's T063 table that were red, or recorded by ruling, are re-read against
  the new aggregate, citing log lines:
  - the nine red rows: FR-011a, FR-013, FR-033, FR-037 and FR-075; SC-008, SC-011, SC-012 and SC-028;
  - the five recorded-by-ruling rows: FR-015, FR-017a, FR-036, FR-076 and SC-010
    (`specs/vorago-phase14-presets-release/compliance.md:918-919` counts "9 ❌, 5 ☑"). Each is re-read against the new
    aggregate with a cited log line, so a 13d lever that changes a ruled row's basis is visible.

  Every primary and every secondary is either verified or individually ruled by the user (roadmap 706–707). That
  includes the sweep-5 secondaries outside the roster (`compliance.md:660-662`):
  - D13.1, D13.2 and D14.2;
  - Spore Drift D11 and D6.2;
  - Steam Vent D6.3 and D7.2.

  The re-read is recorded in this phase's compliance.
- **FR-044 — Preset CPU after re-author.** `Vorago_PresetCpu` is re-run alone on the re-authored library (FR-033's third
  clause).

### F. Evidence

- **FR-050 — Lever table.** Compliance carries one row per feature, with these columns:
  - feature;
  - cells served;
  - lever, with every mechanism in the set itemised;
  - before;
  - every candidate measured (value → reading, log);
  - ruled value;
  - after;
  - override string.

  [roadmap 704]
- **FR-051 — Default-render change documented.** The change is recorded as intentional voicing, with FR-005's before
  and after figures and the named cause. [roadmap 695, 705]
- **FR-052 — CPU delta.** FR-033's before and after figures, in ns and %, for every clause. [roadmap 706]
- **FR-053 — Rulings log.** Every user ruling and every FR-010d adoption is one line in `artifacts/rulings.md`, cited
  from the compliance row it closes.
  - An adoption line carries the lever, the rung, its ladder readings, and the FR-010d clauses it cleared (OQ-1).
  - A user-ruling line covers each stop-and-surface (FR-027), E1's child-attachment change (FR-013) and each FR-030
    conflict.

---

## Success Criteria

### Before-record

- **SC-001 — Before-record fidelity.**
  - **Threshold:**
    - On the unmodified `502e5243` binary, each roster cell's `d` (and `attribBase`) agrees with sweep 5 within
      `max(0.01, 0.005·d)`.
    - One repeat spread is recorded, and it lies within that tolerance.
    - SweepAxes reads Movement rho 0.8333.
  - **How measured:**
    - `VORAGO_PILOT_PRESET=<name> VORAGO_PILOT_TAKES=4 vorago_tests.exe "Vorago_PresetPilot_PrimaryProbe"`, with
      nothing else running → `artifacts/before_<cell>.log`;
    - `dsp_systems_tests.exe "VoragoMacro_SweepAxes"` → `artifacts/base_sweepaxes.log`.

### Cell gates

- **SC-002 — M2, M4, M5, M10 gate.**
  - **Threshold:** `d ≥ 4.0`; the four arms green in the same printout; arm 1 at 44.1 kHz green.
  - **How measured:** the probe on Erosion, Drifting Strata, Stone Gravity and Teeming on the final tree. The
    compliance row cites the verdict line and the `levels:` line of `artifacts/final_<cell>.log`.
- **SC-003 — E1 gate.**
  - **Threshold:** `d(R_k, R_k0) ≥ 4.0` and `≥ attribBase + 1.5`; arms green.
  - **How measured:** the probe's `route arms:` line and verdict line on Bloom Colony.
- **SC-004 — E4 gate, loops alive.**
  - **Threshold:**
    - the same route gate as SC-003, read at 1.0× events (`kEventsRateScaleId` = 0.5 normalised; Feeding Loops
      stores 0.6×, 0.389076, until the confirming pass re-authors it). A candidate that needs a rate above 1.0× fails;
    - arms 2 and 3 green on **all four** takes;
    - on each of the four takes, the wet loop sum (the per-loop taps of `FeedbackEcology::processBlockTapped` summed
      over loops and voices) has RMS ≥ −60 dBFS over every 10 s window in `[A, H]` (FR-021b); one live loop carries it.
  - **How measured:** the probe on Feeding Loops with `VORAGO_PILOT_TAKES=4` and `VORAGO_PILOT_OVERRIDE="800=0.5"`
    (or as compiled after the re-author), plus the FR-004 readout.
- **SC-005 — E6.hi and E7.hi.**
  - **Threshold:** `verifiedAt(Secondary)` is true on Colony Pulse **with its Life raised in the gate override**, with
    `d ≥ 1.5`, the state predicate true and the conjunct true. Colony Pulse's S7 and D13.2 re-verify with that Life.
    Before: 1.3700 (E6.hi) and 1.3430 (E7.hi).
  - **How measured:** `VORAGO_PILOT_SECONDARY=1` on Colony Pulse with the raised-Life override string recorded (FR-025).
    The syncRate and selfAffinity rows of the 13b Life-max table are recorded beside it; S7 and D13.2 are read in the
    same pass.
- **SC-006 — Movement row monotone.**
  - **Threshold:** in `VoragoMacro_SweepAxes`, the Movement row reads `rho ≥ 0.9` (before: 0.8333) and endpoint ≥ +20 %,
    with the thresholds untouched. The whole test case is green.
  - **How measured:** `dsp_systems_tests.exe "VoragoMacro_SweepAxes"` on the final tree →
    `artifacts/final_sweepaxes.log`.
- **SC-007 — Not a limiter pass.**
  - **Threshold:** with `VORAGO_PILOT_MASTER_TRIM=-6`, every roster primary and secondary still meets its `d` bar
    (and, for E1 and E4, the attributability conjunct; for E6.hi / E7.hi, the state predicate and conjunct). The level
    arms are not gated on the trimmed run; they are printed and recorded.
  - **How measured:** the probe re-run on each roster preset → `artifacts/final_minus6_<cell>.log`.

### No regression

- **SC-008 — Verified primaries kept.**
  - **Threshold:** all 36 sweep-5-verified primaries (`requiredPrimaryCells()` minus the six roster cells) reach their
    bar with arms green, as compiled, except a preset FR-030 records as sunk, which reaches its bar after the confirming
    pass.
  - **How measured:** the probe on each preset, with a sweep-5 vs final table generated from `requiredPrimaryCells()`.
- **SC-009 — Verified secondaries and default-state cells kept.**
  - **Threshold:** every FR-030b cell is verified on the final tree, with a before/after table.
  - **How measured:** the FR-004 secondary readout on each host; `Vorago_PresetSweep_AblationVerifiesClaims` on the
    default pseudo-preset.
- **SC-010 — Phase 10 bounds.**
  - **Threshold:**
    - `VoragoMacro_NoZipper`: all assertions pass (≤ 1.5×);
    - `VoragoMacro_SweepAxes`: every assertion that passed in FR-003 (ii) still passes, **plus** the Movement row;
    - the soaks are green.
  - **How measured:** the `[long]` logs, with the base and final logs both cited.
- **SC-011 — 13b gates.**
  - **Threshold:** GATE1M ≥ 0.5 at the default surface and at Life max; every Gate-2 knob that counts on the FR-003
    (iii) base tables still counts.
  - **How measured:** `Vorago_EcosystemRuleProbe` at both surfaces → `artifacts/gate{1,2}_*.log`.
- **SC-012 — Macro neutrality and predicates.**
  - **Threshold:** at the FR-061 neutral (every macro at its neutral: 0, with Gravity at 0.5), every target equals its
    base exactly, as `VoragoMacro_NeutralIsIdentity` section 2 asserts (`vorago_macro_test.cpp:1447`). Every `static_assert` row predicate
    compiles, including with any added row.
  - **How measured:** `VoragoMacro_NeutralIsIdentity` green; the build.
- **SC-013 — Lever neutrality.**
  - **Threshold:** at ecosystem depth 0, every route destination reads exactly its base. At lane 0, every route lever
    is 0.
  - **How measured:** `VoragoVoice_EcosystemLeverNeutral` and `VoragoVoice_RouteLeverZeroAtZeroLane` green, unedited.
- **SC-014 — Boundedness.**
  - **Threshold:** the 13c worst case, extended by every 13d lever at its ruled value:
    - six voices;
    - all macros at 1, with Gravity run at both 0 and 1;
    - the eco lanes injected at 1;
    - every route at its ruled gain;
    - at 44.1, 48 and 96 kHz.

    Every sample is finite by bit pattern, |out| ≤ 0.9661, and nothing is allocated after prepare.
  - **How measured:** `VoragoEngine_CapabilityLeverBounded` (`vorago_ecosystem_lever_test.cpp:1974`), extended; the
    soaks green.
- **SC-015 — CPU.**
  - **Threshold:**
    - `VoragoEngine_CpuBudget` clauses (i) and (ii) pass;
    - `Vorago_ProcessorCpu`: P/D ≤ 1.05;
    - `Vorago_PresetCpu`: worst preset ≤ 1.15;
    - the delta is recorded in ns and %.
  - **How measured:** `node tools/run-cpu-tests.js dsp_systems_tests`, then `vorago_tests`, alone and after the
    machine has idled. Clause (i) is judged by the alternating pinned A/B against the base binary.
- **SC-016 — Determinism.**
  - **Threshold:** the same seed twice stays within `render_fingerprint.h` tolerances.
    `VoragoEngine_SlotSeedReproducibility` and `Vorago_PresetSweep_RendersAreReproducible` are green.
  - **How measured:** the per-push suite and the sweep shards.
- **SC-017 — Fingerprint re-harvest.**
  - **Threshold:** every moved fingerprint is re-harvested in its consuming binary: two byte-identical runs, a green
    verify run, and a new PROVENANCE block.
  - **How measured:** the `VoragoEngine_GhostExtensionWiring` printer; md5 of the literals.
- **SC-018 — Regression suites.**
  - **Threshold:** 100 % pass on the eight per-push suites and on the `[long]` lanes. Test files are edited only under
    FR-034's surfaced list.
  - **How measured:** `node tools/run-close-lanes.js` → `summary.txt`, with the lane logs cited.
- **SC-019 — Default render recorded.**
  - **Threshold:** before and after figures are recorded: the 13b probe's t0, `t0on` and M1 RMS; the Phase 14 P0
    descriptor and M1 RMS. The output is finite, with peak ≤ 0.9661.
  - **How measured:** the FR-005 logs.
- **SC-020 — Surface unchanged.**
  - **Threshold:** parameter count, IDs, types, ranges and defaults unchanged; `kCurrentStateVersion == 3`; the v2 and
    v3 round-trips green.
  - **How measured:** `param_table_test`, `state_v2_test`, `state_v3_test`.

### Confirming pass

- **SC-021 — Confirming pass.**
  - **Threshold:**
    - the affected presets are regenerated, and `Vorago_FactoryPresets_TreeMatchesGenerator` is green;
    - the re-run shards and the aggregate read `verified primaries: 42`, or each remaining cell is ruled;
    - every shard reading is within 0.01 of its probe reading;
    - every record the aggregate reads back is a checked-in copy whose SHA-256 matches `artifacts/sweep5-records.sha256`
      (FR-042).
  - **How measured:** the `VORAGO_SWEEP_SHARD=i/N` shard logs and `artifacts/sweep6_aggregate.log` (the
    `CoverageComplete` line).
- **SC-022 — Showcase subsets.**
  - **Threshold:** `Vorago_PresetMatrix_NoShowcaseSubset` reads "SUBSET pairs: 0" (before: 51, across Hull Resonance 25,
    Smeared Horizon 18 and Haunted Colony 8), or each remaining pair is ruled.
  - **How measured:** the aggregate's NoShowcaseSubset line.
- **SC-023 — T063 re-read.**
  - **Threshold:** each FR-043 row is re-read with a cited log line. Every primary and secondary is verified or
    individually ruled.
  - **How measured:** the compliance table and `artifacts/rulings.md`.

- **SC-026 — Adoption, companions and confirming-pass tiers.**
  - **Threshold:**
    - every adopted lever has an `artifacts/rulings.md` line showing it is the smallest-change rung that cleared its
      gate, the FR-010c read set, the four level arms and the −6 dB re-read (FR-010d);
    - the only user rulings are stop-and-surface items, E1's child-attachment change and FR-030 conflicts;
    - every preset-level companion is itemised in the lever table, and its companion-alone base-tree reading is logged
      and below the gate (FR-025b);
    - every sunk preset (FR-030) re-verifies after the confirming pass, or is surfaced;
    - the roster showcase presets and the three subset presets carry a full re-author; every other affected preset was
      re-authored only on a drop of more than 0.01 or a lost verification, and the log line proving that drop or loss is
      cited (FR-040).
  - **How measured:** `artifacts/rulings.md`, the lever table, and the confirming pass's per-preset re-measure log.

### Cross-cutting

- **SC-027 — Re-open termination.**
  - **Threshold:**
    - no lever is re-laddered more than once after a later lever drops its cell under the bar (FR-010b);
    - every second drop of the same cell has a stop-and-surface ruling line in `artifacts/rulings.md` carrying both
      ladders' readings, and no third ladder exists in the artifacts;
    - the compliance row for such a cell is never UNMET and never met (FR-027).
  - **How measured:** `artifacts/rulings.md`, the lever table's re-open count per lever, and the `artifacts/` ladder
    logs.
- **SC-024 — Cross-platform.**
  - **Threshold:** zero warnings; check-portability clean; clang-tidy `dsp` and `vorago` 0/0; pluginval 5 clean.
  - **How measured:** the tool logs.
- **SC-025 — Seraphis untouched.**
  - **Threshold:** the diff lists no Seraphis path and no Seraphis-consumed header; `seraphis_tests` is green.
  - **How measured:** `git diff --name-only 502e5243..HEAD`, recorded in compliance.

---

## Edge cases

- **E-1 A global lever moves every preset.**
  - Route gains and macro rows reach all 42 presets. FR-030, FR-030b, SC-008 and SC-009 guard against this.
  - 13c's B-15 bisect found one global constant, the child gain, that helped one cell and sank another.
  - So a rung that rescues a roster cell and sinks a verified one is observed in FR-010c's per-lever read set. It may
    be adopted, and the sunk preset must re-verify through its probe re-author in the confirming pass (FR-030).
- **E-2 Louder is not more distinct.**
  - The descriptor is level-invariant.
  - 13b measured that a lane gain of 3 becomes "a fixed boost the level-normalised descriptor largely cancels"
    (`vorago_voice.h:1736-1741`).
  - FR-024c and SC-007 exclude a pass that the limiter is holding up.
- **E-3 Silence is not distinct either.**
  - E4's 2× reading of 8.48 came from loops that fell silent, compared against a loud twin.
  - FR-021b's all-take arm 2 and its gated loop-bus floor exclude this.
- **E-4 Attributability.**
  - For E1 and E4, a lever that raises the route-independent residue `d(R_0, R_00)` as much as it raises
    `d(R_k, R_k0)` does not pass.
  - E1's before reading already has `d − attribBase` = 0.08.
- **E-5 Life depends on everything else.**
  - Life's rows drive the destinations of E1, E4 and E6/E7.
  - So its ladder runs last (FR-010b), and a row the readback shows clamped is not a candidate (FR-019).
- **E-6 Gravity is bipolar.**
  - `contributionOf` maps Gravity as `(m − 0.5)·2`, keeping the sign (`vorago_macro_matrix.h:1168-1176`). The M5
    ablation resets it to 0.5.
  - Any new Gravity target must travel on both halves of the knob. SC-014's Gravity-0 arm covers the air half.
- **E-7 Zipper and monotonicity pull against motion.**
  - Movement's members already run to their component ceilings (`vorago_macro_matrix.h:391-430`).
  - A re-aim that adds motion can break NoZipper. 13b measured a 6 st wander at 1.62×, against the 1.5× bound
    (`vorago_voice.h:1696-1700`).
  - FR-018 therefore makes both bounds per-rung admissibility conditions, not close-time checks.
- **E-8 Wake floor.**
  - A retuned loop or peak wake base stays ≥ `kMinRetunedWakeBase` 0.05, enforced by a static_assert
    (`vorago_voice.h:1750-1755`).
  - So the colony path can never generate a dormancy edge (Phase 5 FR-063).
- **E-9 Seed determinism.**
  - Gates are stored-seed readings.
  - The all-take `d` and arms are printed for every roster cell, so a lever that works only on the stored colony is
    visible when it is ruled. They are gated only for E4's arms (FR-021b).
  - The same seed run twice stays within fingerprint tolerances (SC-016).
- **E-10 Sample-rate changes.**
  - Levers are constants, rows or per-chunk setter calls, with ramp times in seconds.
  - Arm 1 is checked at 44.1 kHz (FR-024).
  - SC-014 bounds the output at 44.1, 48 and 96 kHz.
- **E-11 RT safety.**
  - Every lever is a constant, a row, or a setter call on the control path.
  - Nothing allocates, locks or throws on the audio thread, and no pool is added.
  - SC-014 asserts zero allocation after prepare.
- **E-12 Non-finite input.**
  - Every setter a lever drives already rejects non-finite values:
    - `setChildGain` (`bloom_engine.h:566-570`);
    - `setEcosystemDepth` (`vorago_voice.h:1501-1504`);
    - `setSyncRate` (`ecosystem_engine.h:595-598`);
    - `setGravity` / `setOctaveLock` (`resonance_drift_network.h:633-650`).
  - A new seam (FR-012) rejects non-finite input itself, as `setGhostDensity` does (`vorago_engine.h:1056-1059`).
  - Finiteness is checked by bit pattern, never with `std::isnan`.
- **E-13 Parameter extremes.**
  - Each macro row is evaluated at 0 and 1 (the neutral identity, SC-012) and in the bounded-test worst case (SC-014).
  - `setTargetBase` does not clamp (`vorago_macro_matrix.h:1011-1013`). A row whose summed value exceeds its setter's
    range is clamped by the destination setter.
- **E-14 Fingerprint churn.**
  - Each mid-phase lever moves the ghost fingerprint.
  - So FR-035 re-harvests once, on the final tree.

---

## Lever choices (decided by measurement, where the roadmap defers to this spec)

- **OQ-1 — Which lever ships for each cell, and at what value (decided to be measurement-led, FR-010d).** The roadmap
  names a lever family for each cell and requires each lever to be "measured before it is ruled" (roadmap 686–692). It
  does not fix the mechanism or the value. The plan proposes candidates and rungs within FR-010b's order, and the build
  adopts each lever by FR-010d on its logged ladder readings; the user rules only the items FR-010d reserves. The
  choices per cell that the ladders settle:
  - **E1:** how the route gain and the child attachment are combined, and at what values (dropping the child
    attachment is itself a logged ruling, FR-013).
  - **E4:** whether loop gain or ring coupling move alongside the loop-wake route gain, and at what values.
  - **E6.hi / E7.hi:** which per-`Kind` lane surface, once the FR-015 instrument names it.
  - **M2:** widen or re-aim, and whether to add the mutation row.
  - **M5:** which new target.
  - **M4:** which members are re-aimed, and whether the rate-compensation exponent moves.
  - **M10:** which unclamped row carries Life.

---

## Traceability

| Roadmap statement (lines) | FR / SC |
|---|---|
| Depends on 13c's levers and ladders; Phase 14 close, sweep 5, T063, probes, sharded sweep, close lanes (666–668) | FR-002, FR-012, FR-034, FR-042, FR-043 |
| Premise: the eight cells and their readings; Movement rho 0.8333 (670–680) | Cell roster, FR-001, FR-003, SC-001 |
| The probe predicts the sweep within 0.01; only affected shards + aggregate (680–682) | FR-042, SC-021 |
| Goal: d ≥ 4.0 / ≥ 1.5 with arms green; E4 loops alive; Movement rho ≥ 0.9, no threshold moving (683–686) | FR-020 – FR-024, FR-021b, SC-002 – SC-006 |
| One lever per feature, measured before ruled, ladders by `VORAGO_PILOT_LEVER` or rebuild (686–687) | FR-010, FR-010b, FR-010c, FR-010d, FR-012, OQ-1, SC-026, SC-027 |
| E1: bloom route gain + child attachment (687–688) | FR-013, SC-003 |
| E4: loop-wake route gain at 1×, sustain preserved (688) | FR-014, FR-021b, SC-004 |
| M2: Age row targets (688–689) | FR-016, SC-002 |
| M4: Movement row re-aimed so it moves and stays monotone (689–690) | FR-018, FR-023, SC-002, SC-006 |
| M5: Gravity row given a target the descriptor hears (690–691) | FR-017, SC-002 |
| M10: Life row (691–692) | FR-019, SC-002 |
| E6.hi / E7.hi: the lanes' audible range on a Life-high surface (692) | FR-015, FR-022, SC-005 |
| Gate per cell, route arms with attributability, final tree, arms green (693–694) | FR-020 – FR-026, SC-002 – SC-007 |
| Default render may change and is recorded; Phase 10 bounds, 13b / 13c gates, suites, pluginval, tidy, portability, `[long]`, SweepAxes Movement row (695–697) | FR-005, FR-030 – FR-036, FR-051, SC-008 – SC-019, SC-024 |
| Fingerprints re-harvested in their consuming binary (697–698) | FR-035, SC-017 |
| T048 re-run: re-author by probe, shards + aggregate, T063 re-read; Smeared Horizon subset re-author (699–701; extended to Hull Resonance and Haunted Colony by clarification Q1) | FR-040 – FR-044, FR-025b, SC-021 – SC-023, SC-026 |
| Nothing ships inaudible; stop-and-surface, never UNMET (701–702) | FR-027, FR-041, FR-043 |
| Success criteria: gates with logs, lever table, default render, no regression (close lanes, CPU lane alone), CPU delta, sweep re-read (704–707) | FR-050 – FR-053, SC-015, SC-018, SC-021 – SC-023 |
| Cross-cutting: RT safety, boundedness, ODR, CPU as FR, no bit-exact goldens, portability, shared-component rule (765–789) | E-11, SC-014, FR-038, FR-033, SC-016, FR-036, FR-011, FR-037 |

---

## Review notes

- **All fourteen review issues applied; none rejected.**
- E4's operating point is fixed at 1.0× events (normalised 0.5), the roadmap's "at 1× events". The roadmap lists the
  stored 2.92, the 2× 8.48 and the 1× 3.71 as three separate readings, so 1× is not the stored 0.6×. No bar moved.
- The E4 loops-alive bar reuses arm 2's own floor (−60 dBFS) and window (10 s over `[A, H]`), applied to the loop bus.
  The roadmap ties the lever to "sustain preserved (arm 2)", so no new number was invented.
- The Teeming event-rate "clamped" claim was a code-read error: it ignored the stored base override. It is now a
  measurement, and the Overview gives the corrected code-read (about 6.5 of 10).

---

## Clarifications

### Session 2026-10-07

- Q1 (FR-041 / SC-022: which presets does the showcase-subset re-author cover?) → Re-author all three subset P presets (Hull Resonance 25 pairs, Smeared Horizon 18, Haunted Colony 8) at preset level; target `SUBSET pairs: 0`; the three join the affected set. [FR-040, FR-041, SC-022]
- Q2 (FR-004 / FR-021b / SC-004: what signal is the E4 loops-alive conjunct?) → The wet loop sum (the per-loop taps of `FeedbackEcology::processBlockTapped` summed over voices) gates at or above -60 dBFS per 10 s window on each take; one live loop carries the gate. [FR-004, FR-021b, SC-004]
- Q3 (OQ-1 / FR-053: who adopts each lever?) → Pre-authorised adoption rule: adopt the smallest-change rung that clears the cell's gate, the FR-010c read set, the four level arms and the -6 dB re-read; the user rules only stop-and-surface items, E1's child-attachment change and FR-030 conflicts; every adoption is logged in `rulings.md`. [FR-010d, FR-013, FR-053, SC-026]
- Q4 (FR-030 / E-1: may a lever that sinks a verified cell on another preset be adopted?) → Yes. The sunk preset must re-verify through its probe re-author in the confirming pass and is surfaced only if that re-author fails; FR-030's "as compiled" becomes "after the confirming pass" for a sunk preset. [FR-030, FR-030b, FR-025, FR-040, FR-013, E-1, SC-008, SC-026]
- Q5 (FR-025 / FR-040: preset-level changes beyond the cell's own controls?) → Allowed only as a companion to an adopted engine lever, never as the sole fix, and itemised in the lever table (example: Teeming's stored ecosystem depth 1.0 un-clamped alongside the Life lever). [FR-025b, FR-019, FR-025, SC-026]
- Q6 (FR-015 / FR-022: what is the Life-high surface for E6.hi / E7.hi?) → The gate is Colony Pulse with its Life raised in the gate override and the confirming pass, a named FR-025 exception (like E4's 800); Colony Pulse's S7 and D13.2 must re-verify. [FR-015, FR-022, FR-025, FR-040, SC-005]
- Q7 (FR-040 / FR-042: what does "re-authored by probe" require for an affected preset?) → The confirming pass fully re-authors (override search and defs edit) the roster showcase presets and the three subset presets; every other affected preset is re-measured and re-authored only on a drop of more than 0.01 or a lost verification. [FR-040, SC-026]
- OQ-1 (which lever ships for each cell, and at what value) → Deferred to measurement under Q3's pre-authorised rule: each lever's mechanism and value is chosen on its logged ladder readings; the user rules only the items Q3 reserves. [FR-010d, FR-053, SC-026]
- Q8 (FR-010b: when do re-opens stop?) → Each lever may be re-opened and re-laddered at most once after a later lever drops its cell under the bar (13c re-opened S4 exactly once); a second drop of the same cell is an FR-027 stop-and-surface ruling for the user, never a third ladder. [FR-010b, FR-010d, FR-027, SC-027]
