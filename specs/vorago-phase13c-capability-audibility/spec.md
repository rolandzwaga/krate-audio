# Feature Specification: Vorago Phase 13c — Capability Audibility

**Spec slug:** `vorago-phase13c-capability-audibility`
**Roadmap:** `specs/Vorago-roadmap.md` → Part B, Phase 13c (lines 615–660); Cross-Cutting Constraints (lines 717–741);
Phase 14 status note (lines 667–672)
**Depends on:** Phase 13b (the ecosystem levers, ✅, roadmap 600–611); Phase 14's harness (the C-7.2 descriptor,
`Vorago_PresetPilot_PrimaryProbe` with `VORAGO_PILOT_OVERRIDE`, the route arms, the level arms, the sweep protocol —
committed at `64f57e1a`)
**Blocks:** Phase 14, paused at T048 (roadmap 654–656, 667)
**Status:** DRAFT — specification only, no implementation
**Date:** 2026-10-01 (branch `feat/vorago-phase1-events-modulation` at `64f57e1a`)

---

## Overview

Phase 14 ran three full library sweeps and ten probe batches. Each one measured how far a capability can move its
own showcase preset, using the C-7.2 descriptor against the primary bar **F = 4.0** (`kFloorF`,
`plugins/vorago/tests/preset_test_support.h:249`). The strong half of the instrument clears that bar by a wide
margin. A third of the capability matrix does not reach the output at any preset setting (roadmap 621–636). The
paused tree's record is sweep 4, `specs/vorago-phase14-presets-release/compliance.md:578-591`, which calls itself
"the Phase 13c input". It lists three things:

- fifteen primaries under F;
- one ecosystem-limited secondary with no verifier (E7.hi, `:585`);
- one known level-arm failure (Stone Gravity, `:569`, `:589`).

The roadmap's diagnosis is that these are **level and lever-size problems, not design problems** (roadmap
634–636). Phase 14 already shipped two make-ups, and each one moved a feature from inaudible to present:

- noise bus +30 dB, `VoragoVoice::kNoiseBusMakeupDb`, `dsp/include/krate/dsp/systems/vorago_voice.h:1634`;
- ghost tap +12 dB, `VoragoEngine::kGhostTapMakeupDb`, `dsp/include/krate/dsp/systems/vorago_engine.h:1001`.

This phase applies **one lever per feature, measured before it is ruled** (roadmap 639–640). It continues until
every cell clears its gate on its showcase preset, with the preset's level arms green, on the final tree. This is
a voicing phase. The default render may change, and every change is recorded.

The Phase 14 record explains the bloom cells (S6, E1) one way. Two code facts read this session point somewhere
more specific. That is why FR-010 requires an instrument to confirm the diagnosis before any bloom lever is ruled.

- **At low richness the bloom's child slots sit above the cloud's active count.**
  - `VoragoVoice::updateSpectrumTarget` floors the bloom capacity at `kMinCloudCapacity = kBloomChildSlots +
    kMinParentSlots = 6 + 8 = 14` (`vorago_voice.h:346-355`, `:2345-2349`). The six child slots are therefore
    `[reserveBase(), capacity) = [8, 14)` (`bloom_engine.h:710`).
  - `HarmonicCloud` sounds only `N(r) = round(64^r)` partials. Before the spectral-target branch it zeroes every
    slot at or above that count (`harmonic_cloud.h:1462-1474`).
  - So every child is silent while `N(r) ≤ 8` (r below about 0.515). Some children are silent while
    `9 ≤ N(r) ≤ 13` (r below about 0.626). All children can sound only from r ≈ 0.626 upward.
  - Bloom Colony (E1) stores richness 0.40, so N = 5 (`tools/vorago_preset_defs.h:993`).
  - The compliance note gives a different mechanism: "most buds attach to silent parents"
    (`compliance.md:464-468`). The code does not support that reading once N(r) ≥ 4. The voice hands
    `amplitudes_[i] = exp2(-p·log2 N_i)` for every `i < reserveBase()` regardless of the active count
    (`vorago_voice.h:2356-2367`). The strongest-K scan (`bloom_engine.h:1403-1418`) picks the lowest slots, and
    those slots do sound once N(r) ≥ K = 4.
- **The voice never sets the child gain.**
  - `BloomEngine::setChildGain(float)` exists (`bloom_engine.h:561`, clamp [0, 1]) with default
    `kDefaultChildGain = 0.35f` (`:260`).
  - `grep setChildGain vorago_voice.h` finds no call.
  - The latched child target is `parentAmp * childGain_ * smoothedDepth / tiltGain(slot)` (`:1335`).

---

## Scope

1. **The cell roster** (FR-001): every cell the sweep-4 record hands to this phase, in its Phase 14 role.
2. **Before-record** on the base tree, using the Phase 14 pilot probe (FR-003).
3. **Levers** (FR-010 – FR-019):
   - one lever per feature;
   - where the roadmap names candidates, the lever comes from them;
   - every lever is measured with the probe before it is ruled;
   - every lever is applied in Vorago-owned code.
4. **The gate** per cell on the final tree (FR-020 – FR-027), together with the lever table and the
   default-render record.
5. **No regression** (FR-030 – FR-038). The 27 primaries verified at sweep 4 stay verified, and so do the cells
   verified at sweep 4 only as secondaries or on the default surface (FR-030b). The following all stay green:
   - Phase 10's bounds;
   - 13b's Gate 1 and the knobs counted under 13b's Gate 2 (FR-032);
   - the Phase 2–14 per-push suites and the `[long]` soaks;
   - CPU, pluginval, clang-tidy and portability.

## Non-goals (what other phases own, or nobody does)

- **Editing the preset library** (`tools/vorago_preset_defs.h` and the 42 generated `.vstpreset` files). When
  Phase 14 resumes at T048 it re-authors the showcase presets on the new levers (roadmap 654–655).
  - This phase reads each gate on the def as compiled **or** with a `VORAGO_PILOT_OVERRIDE` set.
  - Every override set it used goes to Phase 14 verbatim in the lever table (FR-025).
- **The distinctness floor.** At sweep 4, 51 pairs sit under `max(F, 2·t_max)` (`compliance.md:575-577`).
  Phase 14 meets the floor after re-authoring (roadmap 655–656). It is not gated here.
- **The Resonant Shaft seed-4 master-gain trim** (`compliance.md:590-591`). This is a Phase 14 authoring fix at
  resume, not a feature-audibility cell. (Stone Gravity's take-3 and 44.1 kHz arm-1 reds are **not** in this
  bucket: they belong to the M5 cell and are gated here, FR-015.)
- **The confirming library sweep.** Phase 14 owns it (roadmap 655).
  - The roadmap calls it "sweep 4". The Phase 14 record already used that name for the pause-time run
    (`compliance.md:571`), so the resume run is the fifth sweep.
  - This is a naming point only. No decision is taken here.
- **Any change to a bar, window, K or descriptor.** These are inputs, not levers:
  - F = 4.0;
  - the secondary bar 1.5 (`kSecondaryBar`, `:250`);
  - the attributability margin 1.5 (`kAttribMargin`, `:943`);
  - K = 4;
  - the M1…M3 twin scoring (Phase 14 ruling S-3);
  - the S-9 audible-attack window (`audibleAttackSeconds`, `:846-853`).
- **The `EcosystemEngine` simulation** (rules, stages, agent kinds, energy budget, `ecosystem_engine.h`), and
  13b's FR-023 wake-combine rule (`VoragoVoice::combineWake`, `vorago_voice.h:1038`). Both stay unchanged, as in
  13b.
- **New parameters or a state-format change.**
  - No parameter is registered, removed, re-typed or re-ranged.
  - `kCurrentStateVersion` stays 3 (`plugins/vorago/src/plugin_ids.h:24`).
  - No registered **default** value moves: OQ-2 is ruled option (b) (FR-016), which reshapes stage 0's curve in
    the voice rather than the stored defaults.
- **UI.** The Phase 13 editor and the ecosystem view are untouched.

---

## Cell roster (the phase's input, `compliance.md:578-591`)

- "Sweep-4 reading" is the "before" value on `64f57e1a`.
- Each cell's role and bar are Phase 14's (C-7.4; G2 ruling 1 for E7.hi).

| Cell | Showcase preset (`vorago_preset_defs.h` line) | Verification | Role / bar | Sweep-4 reading |
|---|---|---|---|---|
| S4 feedback ecology | Feedback Mire (`:455`) | Ablation 500 → 0 | primary, d ≥ 4.0 | 1.7072 |
| S6 harmonic bloom | Slow Bloom (`:513`) | Ablation 1300 → 0 | primary | 1.4453 |
| S9 ghost / atmosphere | Choir of Absence (`:599`) | Ablation 1400 → 0 | primary | 1.8349 |
| M2 Age | Erosion (`:679`) | macro reset | primary | 2.4545 |
| M3 Density | Crowded Dark (`:708`) | macro reset | primary | 3.9226 |
| M4 Movement | Drifting Strata (`:735`) | macro reset | primary | 3.0007 |
| M5 Gravity | Stone Gravity (`:761`) | macro reset | primary **and arms 1, 3** | 3.5289; arm 3 late-sustain +15.7 dB (take 0); arm 1 hi −5.92 dBFS (take 3) and 44.1 kHz hi −5.97 dBFS (`compliance.md:589`; same at sweep 3, `:549`) |
| M9 Fog | Fogbound (`:864`) | macro reset | primary | 1.8229 |
| M10 Life | Teeming (`:894`) | macro reset | primary | 0.3794 |
| M12 Mass | Monolith (`:943`) | macro reset | primary | 3.3670 |
| E1 Partial → bloom | Bloom Colony (`:982`) | route arms | primary, d ≥ 4.0 and d ≥ attribBase + 1.5 | 0.0568 (attribBase 0.0568) |
| E3 Noise → noise wake | Swarm Breath (`:1040`) | route arms | primary, same | 0.6496 (attribBase 0.0760) |
| E4 Feedback → loop wake | Feeding Loops (`:1072`) | route arms | primary, same | 1.2203 (attribBase 0.4488) |
| E5 Ghost → ghost bursts | Haunted Colony (`:1103`) | route arms | primary, same | 1.1920 (attribBase 0.3296) |
| D9.1 fast attack | Sudden Chasm (`:1459`) | attack window (S-9) | primary, d_att ≥ 4.0 and d_att ≥ d_Sus + 1.5 | d_att 2.7661, d_Sus 4.3038 |
| E7.hi selfAffinity high | Colony Pulse (`:541`) | ExtReversion | **secondary**, d ≥ 1.5 | under 1.5 (no verifier) |
| D13.1 / D13.2 events slow / fast | Teeming / Colony Pulse | StateWithReversion (S7 conjunct) | secondary | no verifier (`:585-587`) |
| D14.1 / D14.2 breathing / tidal | Slow Bloom / Drifting Strata | StateWithReversion (depth ablation) | secondary | no verifier (`:585-587`) |

The roadmap's premise paragraph was written from sweep 3 and does not name S4 or M3. Both are still in the
roster:

- S4 passed at sweep 3 only because the render was silent (`compliance.md:544-548`).
- M3 read 3.92 at sweep 3.
- Both are under F on the paused tree.
- The roadmap's goal covers them: "every capability cell … with no cell recorded UNMET" (roadmap 637–639).

---

## Existing components (verified this session)

| Component | Header | What is reused (real signature, read this session) |
|---|---|---|
| `VoragoVoice` | `dsp/include/krate/dsp/systems/vorago_voice.h` | `static constexpr float combineWake(float base, float eco, float sched) noexcept` (`:1038`); `void updateSpectrumTarget() noexcept` (`:2341`; capacity floor `:2345-2349`, `parentCount_ = bloom_.reserveBase()` `:2356`, parent amplitudes `:2366`); `static constexpr std::size_t kBloomChildSlots = 6`, `kMinParentSlots = 8`, `kMinCloudCapacity` (`:346-355`); `void setRichness(float r) noexcept` (`:1141`); `void setBloomDepth(float d) noexcept` (`:1513`); Partial lane `cloud_.setMutation(clamp(mutationBase_ + partialEco))` / `bloom_.setDepth(clamp(bloomDepthBase_ + partialEco))` (`:2124-2126`); `ghostRequest_ = combineWake(0.0f, lanes.eco[kGhost][0], lanes.sched[kGhost][0])` (`:2128`); 13b lever spans `kNoiseLevelLeverSpanDb = 12`, `kPeakLevelLeverSpanDb = 18`, `kLoopGainLeverSpan = 0.18` (`:1607-1609`), `kFreqWanderBaseSemis = 0.75`, `kRingCouplingBase = 0.06`, `kCouplingLeverSpan = 0.30` (`:1617-1618`), `kFreqWanderLeverSpanSemis = 3.0` (`:1624`), `kLeverInputGain{1, 2, 2, 2, 1}` (`:1665-1666`); lever application (`:2129-2175`); `kNoiseBusMakeupDb = 30`, `kNoiseBusMakeupGain` (`:1634-1635`); `kPeakWakeBase = kLoopWakeBase = 0.45f` (`:391-392`); envelope `kDefaultStageLevels{1.00, 0.80, 0.92, 0.85, 0.85, 0.00}`, `kDefaultStageTimesMs{20000, 30000, 45000, 60000, 0, 0}` (`:322-326`), `enum class EnvelopeMode : std::uint8_t { Standard = 0, Growth = 1 }` (`:342`) |
| `VoragoEngine` | `dsp/include/krate/dsp/systems/vorago_engine.h` | `static constexpr float kGhostBurstPeak = 0.60f` (`:271`); `void setGhostPeakLevel(float v) noexcept` (`:986`, clamp [0, 1]); `atmos_.setLevel(ghostPeak_ * ghost)` (`:1545`); `atmos_.setDensity(0.30f)` (`:375`); `static constexpr float kGhostTapMakeupDb = 12.0f`, `kGhostTapMakeupGain`, `kMaxGhostTapMakeupDb = 24.0f`, `void setGhostTapMakeupDb(float dB) noexcept` (`:1001-1008`); `kMaxVoices = 6` (`:231`); `kOutputCeilingDb = -0.3f` (`:261`) |
| `VoragoMacroMatrix` | `dsp/include/krate/dsp/systems/vorago_macro_matrix.h` | `enum class VoragoMacro : std::uint8_t { Darkness = 0, Age, Density, Movement, Gravity, Entropy, Pressure, Weight, Fog, Life, Depth, Mass, Count }` (`:106-121`); `struct VoragoMacroRow { VoragoMacro macro; VoragoMacroTargetOwner owner; VoragoMacroTarget target; float base; float amount; ModCurve curve; }` (`:233-257`); `static constexpr std::array<VoragoMacroRow, kNumRows> kRows` with `kNumRows = 50` (`:276`, `:315`); `float contributionOf(const VoragoMacroRow&) const noexcept` (`:1160`, Gravity bipolar); `evaluateAll() const noexcept` (`:1177`); the compile-time row predicates (`:1215-1226`). This phase's macro rows (FR-014): **Age** → BodyDamping 0.25 + 0.55, CavernDecaySeconds 20 − 14, CloudSpectralTiltDb −4 − 4 (`:342-358`). **Density** → CloudRichness 0.70 + 0.28, NoiseWakeBase 0.35 + 0.65, NoiseLevelDb −18 + 6, BloomDepth 0.60 + 0.40 (`:365-388`). **Movement** → CloudDriftDepthCents 8 + 42, ResonanceWanderRate and NoiseWanderRate 0.03 + 0.97 (Exponential), BreathingDepth 0.30 + 0.70, CavernDamperDepth 0.35 + 0.45 (`:402-430`). **Gravity** → ResonanceGravity 0 + 1, ResonanceOctaveLock 0 + 1 (`:439-455`). **Fog** → SmearAmount 0.20 + 0.70, GhostPeakLevel 0.60 + 0.40, AtmosBlur, CavernFog, CavernDarkness, CloudSpectralTiltDb (amount 0), TidalDepth 0.40 + 0.40 (`:571-620`). **Life** → EcosystemDepth 0.85 + 0.15, EventRateScale 1 + 9, BloomSpawnRateHz 1/240 + 0.0208 (`:630-658`). **Mass** → BodyResonance 0.70 + 0.28, BodyMix (amount 0), ResonanceMix 0.45 − 0.30, SubTrackingAmount (amount 0), SubToneLevelOffsetDb 0 + 3 (`:706-756`) |
| `BloomEngine` | `dsp/include/krate/dsp/systems/bloom_engine.h` | `void setChildGain(float gain) noexcept` (`:561`, [0, 1]), `kDefaultChildGain = 0.35f` (`:260`), `float getChildGain() const noexcept` (`:687`); `void setDepth(float) noexcept` (`:532`); `void setSpawnRateHz(float) noexcept` (`:543`, ≤ `kMaxSpawnRateHz` 0.05, `:280`); `void setParentCount(std::size_t) noexcept` (`:551`, [1, `kMaxParents` 8]); `void setChildrenPerEvent(std::size_t) noexcept` (`:556`, ≤ 4); `std::size_t reserveBase() const noexcept` (`:710`); `std::size_t getLiveChildCount() const noexcept` (`:712`); strongest-K scan over `[0, min(pc, reserveBase()))`, silent-parent skip at `kSilentParentAmplitude = 1e-5` (`:259`, `:1403-1418`); latched target (`:1335`). Vorago-only: `grep -rl bloom_engine.h dsp/include plugins/seraphis plugins/shared` found no Seraphis consumer |
| `HarmonicCloud` | `dsp/include/krate/dsp/systems/harmonic_cloud.h` | Read-only, **Seraphis-shared**: `std::size_t getActivePartialCount() const noexcept` (`:950`); `N(r) = clamp(round(64^r), 1, 64)` and the zeroing of slots `≥ activeCount_` before the target branch (`:1458-1474`); `void setSpectralTarget(const float*, const float*, std::size_t) noexcept` (`:769`) |
| `AtmosphereEngine` | `dsp/include/krate/dsp/systems/atmosphere_engine.h` | Read-only, **Seraphis-shared**: `void setDensity(float grainsPerSecond) noexcept` (`:868`), `void setLevel(float level) noexcept` (`:1035`) |
| Pilot probe | `plugins/vorago/tests/integration/preset_pilot_test.cpp` | `TEST_CASE("Vorago_PresetPilot_PrimaryProbe", "[.probe][vorago]")` (`:845`): `VORAGO_PILOT_PRESET=<name>`, `VORAGO_PILOT_OVERRIDE="id=norm,…"` (`:855-883`), the stored take's four level arms printed (`:949-959`), the route arms R_k / R_k0 / R_0 / R_00 scored as `d(R_k, R_k0)` against `attribBase = d(R_0, R_00)` (`:900-980`), verdict line (`:991-995`) |
| Harness | `plugins/vorago/tests/preset_test_support.h` | `struct PresetDescriptor` (`:48`), `double descriptorDistance(const PresetDescriptor&, const PresetDescriptor&)` (`:174`); `kFloorF = 4.0`, `kSecondaryBar = 1.5` (`:249-250`); `kAttribMargin = 1.5` (`:943`); `bool verifiedAt(const CellOutcome&, Verification, ClaimRole) noexcept` (`:988`); `double audibleAttackSeconds(const DecodedPresetState&)` (`:846`); `std::optional<double> attackWindowEndSeconds(…)` (`:2389`); `ParamOverrides routeOverrides(Capability kept, bool depthZero = false)` (`:2248`); `struct ArmResult` and the arm bounds `kLateLoDb = -18`, `kLateHiDb = 12` (`:1735-1736`, `:1925-1945`) |
| Capability roster | `tools/vorago_preset_defs.h` | `enum class Capability : std::uint8_t` (`:54-77`, 79 cells), `enum class Verification : std::uint8_t` (`:82-91`) |
| Ecosystem probe (13b) | `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp` | `TEST_CASE("Vorago_EcosystemRuleProbe", "[.probe][vorago]")` (`:544`), 13b's Gate 1 instrument (six-seed GATE1M) |
| Instruments (dsp) | `dsp/tests/unit/systems/` | `VoragoVoice_BloomCountsProbe` (`vorago_voice_test.cpp:4032`), `VoragoEngine_GhostLevelProbe` (`vorago_engine_test.cpp:3215`), `VoragoEngine_GhostExtensionWiring` (`vorago_ghost_ext_test.cpp:415`), `VoragoMacro_SweepAxes` / `VoragoMacro_NoZipper` `[long]` (`vorago_macro_test.cpp:1211`, `:1514`), `VoragoEngine_CpuBudget` `[.perf]` (`vorago_perf_test.cpp:971`; `kReferenceNs`, `kCavernMeasuredNsPerBlock = 124497`, `kEngineBaselineNsAtPoly4 = 2694479` in `vorago_perf_budget.h:82`, `:99`, `:138`) |
| Plugin CPU | `plugins/vorago/tests/integration/processor_cpu_test.cpp` | `TEST_CASE("Vorago_ProcessorCpu", "[vorago][.perf][performance]")` (`:188`), SC-014 arms P / D (`:284-287`) |
| Envelope params | `plugins/vorago/src/parameters/envelope_params.h` | Defaults taken from `VoragoVoice::kDefaultStageTimesMs` (`:56-62`); IDs `kEnvelopeStage0TimeId = 1201` … `kEnvelopeGrowthDurationId = 1206` (`plugin_ids.h:211-217`) |

## New components

**None.** Roadmap Phase 13c names no new class. The work is retuned constants and rows and lever wiring inside
Vorago-owned headers, plus probe reporting. The headers are `vorago_voice.h`, `vorago_engine.h` and
`vorago_macro_matrix.h`. `bloom_engine.h` is touched only if a needed setter does not exist.

### ODR sweep — run this session

Each name a plan might reach for was swept with `grep -rn "<Name>" dsp/ plugins/ tools/` (any occurrence). Every
one returned 0 hits, so **all are clear**:

`CapabilityAudibility`, `CapabilityLever`, `CapabilityProbe`, `AudibilityLever`, `LeverTable`, `BloomLever`,
`kBloomChildGain`, `kBloomChildGainBase`, `GhostBurstLever`, `kGhostBurstDensity`, `MacroRowWidening`,
`VoragoCapabilityProbe`, `kAudibleAttack`.

FR-038 requires the plan to re-run the sweep for every name it actually introduces.

---

## Functional Requirements

### A. Roster, instrument, before-record

- **FR-001 — Cell roster.** The gated cells are exactly the cells in the "Cell roster" table:
  - the fifteen primaries under F at sweep 4;
  - E7.hi, in its secondary role;
  - D13.1, D13.2, D14.1 and D14.2, in their secondary role.

  No cell is added or dropped, and no cell's role changes. [roadmap 637–639: "every capability cell … no cell
  recorded UNMET"; `compliance.md:578-591`]
- **FR-002 — Same instrument.** Every gate figure is read from `Vorago_PresetPilot_PrimaryProbe`
  (`preset_pilot_test.cpp:845`), with its scoring unchanged:
  - the stored-seed take;
  - twins over M1…M3 to 340 s, on the preset's own timeline;
  - the C-7.2 descriptor, through `preset_test_support.h` only;
  - route cells on their four route arms;
  - the D9.1 cell on the S-9 attack window.
  - **(plan ruling P2)** a third measurement route exists for the ladders only: `VORAGO_PILOT_LEVER`
    applies the plan §2.9 setters through a test-only engine tweak after the preset is loaded, so a candidate
    value is a probe run; it changes nothing the gate reads — a gate figure is taken with no lever env set,
    from the compiled constants, and the probe REQUIREs the tweak held on every capture.

  The probe may gain **reporting** only (FR-004). No bar, window, take count or distance changes, with one
  stated exception: the D9.1 / D8.2 measured-reach window, which applies under the ruled option (b) (FR-016).
  [roadmap 638, 647–649]
- **FR-003 — Before-record.** Before any lever lands, with nothing else running, the base tree is recorded in two
  steps. All logs are checked into `specs/vorago-phase13c-capability-audibility/artifacts/`.
  - **(i) Primaries on the unmodified binary.** Every roster primary is probed on `64f57e1a` exactly as compiled
    (no probe change). Each reading must agree with its sweep-4 row (table above) within the **reproduction
    tolerance** `|d_base − d_sweep4| ≤ max(0.01, 0.005·d_sweep4)`, and likewise for `attribBase` / `d_Sus`.
    Bit-exact agreement is not required: a rebuild can move a render by about 1e-4 through `/fp:fast` COMDAT
    selection, so the tolerance is a measured one, not print precision. The plan reads one roster cell twice on
    the same binary and records that repeat-run spread; if the spread exceeds the tolerance, or a reading falls
    outside it, that is a stop-and-surface item. The tolerance is never widened to absorb a miss.
  - **(ii) Secondaries on the reporting binary.** After FR-004's reporting lands (no production change), the
    secondary cells (E7.hi, D13.x, D14.x, and the FR-030b set) are read on that binary. The same run re-reads the
    roster primaries, and each must stay within the reproduction tolerance of its step (i) value. This shows the
    reporting change did not move the gate figures.
  - **(iii) The regression baselines,** on the step (i) binary: `VoragoMacro_SweepAxes` and `VoragoMacro_NoZipper`
    (`[long]`, `vorago_macro_test.cpp:1211`, `:1514`), each with its full assertion list logged, so FR-031's
    "passes on the base tree" set is captured. The 13b Gate 1 and Gate 2 tables on the base tree (FR-032), since
    Phase 14's two make-ups landed after 13b closed.

  [roadmap 650: "every before/after descriptor is recorded"]
- **FR-004 — Secondary-cell readout.**
  - For a **named secondary** of the preset, the probe prints the verdict terms: `d`, the bar 1.5, the side or
    state predicate, and the conjunct. This way E7.hi, D13.x and D14.x are measured by the same instrument as the
    primaries.
  - The plan chooses the selection mechanism (for example, an environment option).
  - The readout stays hidden (`[.probe]`) and is never a per-push gate.
  - The probe also gains these reporting-only renders, each honouring the run's `VORAGO_PILOT_OVERRIDE`
    exactly as the gate renders do (`preset_pilot_test.cpp:856-886`), so they measure the state that passed the
    gate and not the compiled def:
    - the four level arms on **all K = 4 takes** (today only the stored take's arms are printed, `:949-959`);
    - a **44.1 kHz** stored-take render scored for arm 1 exactly as the sweep helper scores its 44.1 kHz arm
      (`detail::scoreRateArm1` over `[0, A + kGestureAfterAttackSeconds]`, `preset_test_support.h:2840-2851`,
      `:3193-3194`). The sweep helper itself is not used for this reading: it renders the compiled def and does
      not apply overrides.

  [FR-001; roadmap 647]
- **FR-005 — Default-surface record.** The default surface is rendered before and after, and recorded on both
  instruments that already describe it:
  - the 13b probe at the default surface: t0, `t0on`, M1 RMS;
  - the Phase 14 pilot's P0 row: stored-seed descriptor, M1 RMS.

  [roadmap 650, 658–659]

### B. Levers (one per feature, each measured before it is ruled)

- **FR-010 — Measure first.**
  - No lever is adopted until the probe has read the target cell with it, and the reading is in a checked-in log.
    The reading uses `VORAGO_PILOT_OVERRIDE` where a parameter reaches the lever, or a rebuilt binary where the
    lever is a constant.
  - Where this spec states a mechanism read from code (Overview; FR-011), the plan confirms it with an existing
    or added hidden instrument before it chooses the lever. One example: `VoragoVoice_BloomCountsProbe`
    reporting, for each spawned child, whether its slot is below `getActivePartialCount()`.

  [roadmap 639–640: "each measured before it is ruled"]
- **FR-010b — Fixed lever order.** Levers are measured and ruled in this fixed order, each one measured on the
  tree with every earlier lever in this order already landed: **bloom** (FR-011) → **routes** (FR-012) →
  **ghost** (FR-013) → **S4** (FR-017) → **attack** (FR-016) → **life modulators** (FR-017b) → **macros**
  (FR-014, with Life ruled last within that group). If measuring a later lever on that tree drops an earlier,
  already-ruled cell below its bar, that earlier cell's lever is re-opened and re-ruled before work continues
  to the next lever in the order. This does not change OQ-1: every lever and its value is still ruled by the
  user on its own logged probe readings. [roadmap 639–640]
- **FR-011 — Bloom (S6, E1; also feeds M3 and M10).** The lever acts on how much of the bloom reaches the output,
  through the roadmap's two candidates:
  - the **child gain** (`BloomEngine::setChildGain`). The voice never sets it today, so it stays at 0.35.
  - **parent selection and richness independence**: children attach to sounding parents and are audible at any
    richness. On this tree the child slots `[8, 14)` lie above `N(r)` for r below about 0.626.

  Whatever ships must meet two conditions, at every richness in [0, 1]:
  - **children sound:** each live child produces output, measured as the cloud's sounding amplitude on the
    child's slot (`HarmonicCloud::getPartialCurrentAmplitude`, `harmonic_cloud.h:959`) being above the silent
    threshold, which requires its slot to be `< getActivePartialCount()`;
  - **parents are preserved, relative to parent 0:** each parent partial the user's richness sounds keeps its
    ratio to parent 0. Measured per slot `i < min(N(r), kMinParentSlots)`, the sounding amplitude of slot `i`
    relative to slot 0, with the bloom on, is within ±0.5 dB of that same ratio with the bloom off, at the same
    richness (outside the instants a child is latched onto that parent, which the instrument reports
    separately). The shared normaliser's absolute drop on every parent, driven by the audible children, is
    **accepted, not compensated**: no Vorago-side code corrects for it, and the child gain is free to rise to
    whatever value the lever rules. The absolute drop at the ruled child gain, per richness point, is measured
    and logged in the lever table (FR-040).

  **The arithmetic that makes this non-trivial on the current tree.** `HarmonicCloud` (read-only, Seraphis-shared)
  sets `N(r) = clamp(round(64^r), 1, 64)` and zeroes every slot `≥ N(r)` (`harmonic_cloud.h:1462-1474`):
  N(0.0) = 1, N(0.35) = round(4.29) = 4, N(0.40) = round(5.28) = 5, N(0.70) = round(18.4) = 18, N(1.0) = 64.
  The child slots on this tree are `[8, 14)`. So at r = 0.0, 0.35 and 0.40 every sounding slot is a parent slot,
  and no placement inside the cloud's own active count can satisfy both conditions. The conditions are met by
  decoupling the cloud's active count from the user's richness inside Vorago-owned code: **the lever is always
  on** — every voice drives the cloud's active count to `N ≥ kMinCloudCapacity` (14) unconditionally, regardless
  of bloom depth, liveness or richness (there is no switching path), and reproduces the user's richness rolloff,
  including the zeroing of parents `≥ N(r_user)`, through the spectral target it already hands the cloud,
  `vorago_voice.h:2356-2367`. The small permanent CPU cost this adds on every voice — including low-richness
  presets with the bloom at 0 — is measured and recorded as part of FR-033/SC-014's CPU delta. If this mechanism
  cannot be built within FR-019's edit set, the low-richness half of this FR is a stop-and-surface item on
  `harmonic_cloud.h`; it is not silently dropped and the slot test is not weakened to "or the shipped
  equivalent". Per FR-018/Q5, bloom is one feature: the shipped lever is one coherent change set combining this
  always-on decoupling with the child gain above, tabled as a single lever-table row (FR-040).

  [roadmap 640–641: "children attach to sounding parents; richness independence"]
- **FR-012 — Ecosystem routes (E1, E3, E4, E5; also feeds E7.hi, D13.x and M10).**
  - The lever is the size of 13b's route levers: the spans and input gains at `vorago_voice.h:1607-1666`, plus
    the Partial and Ghost lanes at `:2124-2128`.
  - The levers are scaled **so one route alone clears the bar on a preset that exposes it**, measured on its
    route arms with the attributability clause (FR-021).
  - The 13b invariants still hold:
    - every lever offset is exactly zero when its lane is zero (13b FR-017);
    - the levers ignore the schedulers (13b FR-019);
    - the coupling lever drives only the shipped ring pair (13b FR-015, Q8).
  - **(ruling B-5)** The L2 size ladder cleared no cell and the destination swings bound every route below
    the bar; the sizes stay. E1's route gains a second destination after the L3 / L4 lifts (P3's extension,
    adopted): the Partial lane drives the bloom's spawn rate and child gain between the preset's base and a
    ruled ceiling, exactly zero at lane zero, scheduler-blind. E cells are re-read after L4 and again after
    the extension.
  - **(ruling B-11)** The extension was measured and declined (no rung moves E1; S6 falls under its bar);
    the Partial lane's destinations stay mutation and bloom depth. E1 is an FR-027 surfacing item.

  [roadmap 641–642]
- **FR-013 — Ghost (S9, E5).**
  - The roadmap names two candidates, the second for "where the make-up alone is short":
    - the ghost tap's **level**: `kGhostTapMakeupDb`, today 12 dB against a 24 dB setter ceiling;
    - its **burst density**: `atmos_.setDensity(0.30f)` (`vorago_engine.h:375`) and the `ghostPeak_ * ghost`
      gating (`:1545`).
  - For the shipped value, the `VoragoEngine_GhostLevelProbe` sizing figures are recorded as they were for S-7
    (`compliance.md:497`): ghost alone vs drone, loudest second, seconds sounding.

  - **(ruling B-6)** Make-up 21 dB; density unchanged at rest and driven by the Ghost eco lane as the route's
    second destination (`VoragoEngine::kGhostDensitySpan`, `VoragoVoice::getGhostEcoLane()`), zero at lane
    zero, scheduler-blind; pinned by `VoragoEngine_GhostDensityRoute`.

  [roadmap 643–644]
- **FR-014 — Macro rows (M2 Age, M4 Movement, M5 Gravity, M9 Fog, M10 Life, M12 Mass; M3 Density).**
  - The lever is the macro's rows in `VoragoMacroMatrix::kRows`. A row that moves its preset by less than the
    bar is either **widened** (its `amount`) or **given a target that moves** (a new row on an existing
    `VoragoMacroTarget`).
  - Every compile-time row predicate still holds (`vorago_macro_matrix.h:1215-1226`, for example
    `everyRowSharesOneBasePerTarget` and `noRowUsesSteppedCurve`).
  - At value 0 every macro still reads exactly its neutral. This is the FR-066 identity, which follows from the
    arithmetic (`:1170-1177`).
  - M10 Life's three rows target the ecosystem depth, the event rate and the bloom spawn rate (`:630-658`).
    Per FR-010b's fixed order, its lever is ruled **last**, after bloom, routes, ghost, S4, attack and the life
    modulators all land, and is measured on that tree.
  - The roadmap's candidate list does not name M3. FR-010 chooses its lever from the same row mechanism.
  - Per FR-018/Q5, each macro here — M2, M3, M4, M5, M9, M10 and M12 — is its own feature: its row-widening or
    new-target choice is ruled and tabled separately, one lever-table row per macro, even though this FR groups
    them for exposition. M5's row change and FR-015's arm-3 fix are tabled together as the single M5 row.

  - **(ruling B-13)** M9 ships rung 2 (Fog→CavernFog 0.70, new Fog→SmearDecoherence +0.40: 4.4115), M12
    rung 1 (Mass→SubToneLevelOffsetDb +6: 5.7413), M3 the shipped rows (4.1136). M2, M4, M5 and M10 keep
    their shipped rows and are surfaced under FR-027 with their ladders.

  [roadmap 642–643]
- **FR-015 — Gravity's self-growth (M5, arm 3).**
  - At the Gravity extreme, Stone Gravity grows 14–16 dB across its sustain, whatever its level, sub or
    resonance mix (`compliance.md:569`).
  - The M5 lever MUST leave Stone Gravity's arm 3 **and** arm 1 green on **all K = 4 takes** and arm 1 green at
    **44.1 kHz**, read through FR-004's all-take and 44.1 kHz reporting with the gate's override string. Arm 3 is
    RMS(`[H − 60, H]`) − RMS(`Sus`) within [−18, +12] dB (`kLateLoDb` / `kLateHiDb`); arm 1 is peak ≤ 0.9661 and
    every 10 s RMS ≤ −6 dBFS. Before: arm 3 +15.7 dB (take 0), arm 1 hi −5.92 dBFS (take 3), 44.1 kHz hi
    −5.97 dBFS (`compliance.md:589`). These are known reds of this cell, so they are gated here rather than handed
    to Phase 14's confirming sweep.
  - **Cause before ruling (FR-010).** The instrument is the probe's arm-3 line on Stone Gravity at the Gravity
    extreme, with each Gravity row ablated in turn (`ResonanceGravity`, `ResonanceOctaveLock`,
    `vorago_macro_matrix.h:439-455`) and, separately, the breathing lane's contribution to resonance gravity
    removed (`breathGravityLane_`, `vorago_voice.h:2231-2232`). The required evidence is a logged table of arm 3
    (late-sustain − Sus, dB) per ablation, naming the row or lane whose removal brings arm 3 inside
    [−18, +12] dB. If no single ablation does, the table is surfaced with the lever proposal.
  - Per FR-018/Q5, this fix and FR-014's M5 row change are one coherent M5 lever: both land together and are
    tabled as the single M5 lever-table row, not a second feature.

  - **(ruling B-13)** Met on the 13c tree without a row change: the cause table reads arm 3 in [−18, +12] dB
    on every arm and every take (`m5_cause.log`); the row stays the shipped ResonanceGravity + OctaveLock pair.

  [roadmap 647–649: "with the preset's four level arms green"]
- **FR-016 — Attack cell (D9.1).**
  - The lever is the **registered envelope's audible attack**. The D9.1 comparison window is Phase 14 ruling
    S-9's: `audibleAttackSeconds`, which for Standard mode is stage 0's time.
  - Today the reversion twin reaches RMS(Sus) − 6 dB at 6 s, against 4 s for Sudden Chasm
    (`compliance.md:518`). The cause is that stage 0 takes 20 s to reach level 1.0 (`vorago_voice.h:322-326`).
  - **OQ-2 is ruled: option (b).** The voice reshapes stage 0's curve so the registered 20 s stage-0 time is
    audibly ~20 s (the measured first reach of RMS(Sus) − 6 dB tracks the registered time), without moving
    `kDefaultStageTimesMs`, `kDefaultStageLevels` or any other stored default. Before this lever is ruled,
    option (a) (lengthening the registered stage-0 default) is also measured on the same instrument, per
    FR-010, so the lever table carries both candidates' readings side by side.
  - **What the instrument compares under each option.** The reversion twin `P_rev` sets stages 0–3 to
    `VoragoVoice::kDefaultStageTimesMs` (`twinOverrides`, `preset_test_support.h:2227-2233`), and the window end
    `W_end = max(A_P, A_rev) + 5` is sized from `audibleAttackSeconds`, which for Standard mode reads the
    registered `stage0TimeMs` (`:846-853`). So:
    - **(a) defaults change (measured only, not shipped):** `P_rev` follows the new defaults (the twin reads the
      constant), so the comparator moves with the lever. As the default attack approaches Sudden Chasm's,
      `d_att` falls, so option (a) works against its own gate. Before measuring, the plan states the expected
      direction of `d_att` from the printed reach times (`printReach`, `preset_pilot_test.cpp:549-552`) of the
      base `P_rev` and the candidate default, against today's 6 s vs 4 s (`compliance.md:518`). The prediction
      and the reading both go into the lever table, so the gap this candidate does or does not close to Sudden
      Chasm is visible.
    - **(b) voice mapping changes (shipped):** the registered times stay, so `audibleAttackSeconds` would no
      longer match the product's audible attack. Under (b), the S-9 window is sized from the **measured** first
      reach of RMS(Sus) − 6 dB (`kAttackReachBelowSusDb`, `preset_test_support.h:1740`; the reach the probe
      already prints), for both P and P_rev, in place of the registered stage-0 time. This is the **one** stated
      exception to FR-002's "no window change", and it applies, because (b) ships, to the two cells that use the
      attack window: D9.1 and D8.2. D8.2 shares `attackWindowEndSeconds` (`:2385-2405`); its preset is in
      Growth mode, but its twin reverts to Standard mode, so its `A_rev` also reads stage 0. D8.2 must still
      verify under FR-030 on the window actually used. The plan records both window ends (registered and
      measured) in the D9.1 and D8.2 rows.

  - **(ruling B-10)** Option (b) ships as `kAttackStageCurve = Linear` with `kAttackShapePower = 4`
    (u⁴ from the voice's own stage-0 sample count); the registered defaults do not move. Measured reach 17 s
    on the probe (19.0 s at voice level), inside the confirmed [0.75 · T0, T0] window; D9.1 8.1243, D8.2
    7.6879. Option (a) measured and declined (`l5_attack_optionA.log`).

  [roadmap 644–645]
- **FR-017 — Feedback ecology (S4).**
  - The roadmap's candidate list does not name S4.
  - On this tree the loops are wake-gated, and the ecology wet path sits about 30 dB down by voicing
    (roadmap 276).
  - FR-010 chooses the lever from the roadmap's two problem classes: a level make-up or a lever size.
  - The preset's arm 2 (non-silence) MUST be green in the same printout. This excludes the sweep-3 failure mode,
    a pass earned by a silent render (`compliance.md:544-548`).

  - **(rulings B-8 / B-9)** The component ceiling was raised to 36 dB (append-only) after 24 dB read 3.60; the
    make-up is ruled at 30 dB (d 4.66). Feedback Mire's dynamics are kept; 36 dB (5.72) was declined as a wash.

  [roadmap 634–639]
- **FR-017b — Life modulators (D14.1 breathing, D14.2 tidal).**
  - Both cells verify by a depth ablation to 0 with no S conjunct (`kLifeBreathingDepthId`,
    `kLifeTidalDepthId`, `tools/vorago_preset_defs.h:254-257`), so no route, bloom or ghost lever reaches them,
    and the macro-reset ablation of FR-014 does not exercise them.
  - The lever is the depth-to-destination scaling of the two modulators in `vorago_voice.h`: the breathing lane
    into resonance gravity (`breathGravityLane_ = breath_.getCurrentValue()`, applied as
    `resonance_.setGravity(clamp(gravityBase_ + breathGravityLane_, -1, 1))`, `:2231-2232`) and the tidal lane
    into the engine's fog (`tidalFogDepth_`, `:2233`, read by `VoragoEngine` as
    `fog = max(fog, getTidalFogDepth())`, `vorago_engine.h:1528`). The plan may also propose a second
    destination for either lane. `breathing_modulator.h` and `tidal_modulator.h` are Seraphis-shared and stay
    untouched (FR-019).
  - Measured with FR-004's secondary readout on the host presets, Slow Bloom (D14.1) and Drifting Strata (D14.2).
  - Depth 0 still gives exactly zero lane contribution, so the reversion twin remains a true "off" state.

  [roadmap 637–639: "every capability cell … no cell recorded UNMET"; FR-001, FR-023]
  - **(ruling B-12)** `kBreathGravityLaneGain = 3.0` (D14.1 2.6558 verified, S6 4.3820); the tidal lane's gain
    stays 1.0 and the voice's tide rate becomes a lever, `VoragoVoice::kTidalRate = 0.8` (was the literal 0.25;
    layer periods 93 / 132 / 161 s). D14.2 is an FR-027 surfaced item: its destination, the cavern fog, reads at
    most 0.31 at any rate or gain, 0.19 with the space mix at 1.0.

- **FR-018 — One lever per feature, recorded.**
  - Each feature gets one ruled lever. One lever may serve several cells of the same feature; for example, the
    bloom lever serves both S6 and E1.
  - **A lever is one coherent change set, not one mechanism.** It may combine several of the named candidates
    for its feature when a single mechanism does not cover it — for example bloom's lever combines the
    active-count decoupling with the child gain (FR-011), and Gravity's lever combines the macro-row change
    with the arm-3 self-growth fix (FR-014, FR-015). Each feature still gets exactly **one** lever-table row,
    with every mechanism in its change set itemized under that row's "lever" column (FR-040).
  - Each macro named in FR-014 (M2, M3, M4, M5, M9, M10, M12) is counted as its own feature for this rule, with
    its own lever-table row and its own ruling, even though FR-014 groups them in one requirement for
    exposition.
  - Every candidate measured, adopted or not, becomes a row of the lever table (FR-040).

  [roadmap 639–640, 658]
- **FR-019 — Edit set.**
  - Production edits are confined to Vorago-owned headers: `vorago_voice.h`, `vorago_engine.h`,
    `vorago_macro_matrix.h`. `bloom_engine.h` may receive an append-only, default-inert addition, and only if a
    needed setter does not exist.
  - A Seraphis-consumed header may be edited only after a stop-and-surface ruling. The edit must be append-only
    and default-inert, and `seraphis_tests` must stay green with no test edited. The Seraphis-consumed headers are
    `harmonic_cloud.h`, `atmosphere_engine.h`, `continuous_body.h`, `aether_reverb.h`, `entropy_processor.h`, the
    life modulators and `seraphis_*.h`.

  [roadmap 739–741]

### C. The gate

- **FR-020 — Gate, ablation and macro primaries (S4, S6, S9, M2, M3, M4, M5, M9, M10, M12).** On the final tree,
  the probe on the cell's showcase preset reads `d ≥ 4.0`. [roadmap 647]
- **FR-021 — Gate, route primaries (E1, E3, E4, E5).** Both conditions hold:
  - `d(R_k, R_k0) ≥ 4.0`;
  - `d(R_k, R_k0) ≥ d(R_0, R_00) + 1.5` (the attributability clause).

  [roadmap 647–648]
- **FR-022 — Gate, attack primary (D9.1).** On the S-9 window, both conditions hold:
  - `d_att ≥ 4.0`;
  - `d_att ≥ d_Sus + 1.5`.

  [roadmap 648–649]
- **FR-023 — Gate, secondaries (E7.hi, D13.1, D13.2, D14.1, D14.2).** Each cell verifies on its host preset at
  the secondary bar, by its Phase 14 verification kind (`verifiedAt(…, ClaimRole::Secondary)`,
  `preset_test_support.h:988`). This includes its state predicate and its conjunct. [FR-001]
- **FR-024 — Level arms in the same printout.** A gate counts only if the same probe run prints all four arms of
  the stored take as green:
  - arm 1: peak ≤ 0.9661 and every 10 s RMS ≤ −6 dBFS;
  - arm 2: every 10 s RMS over `[A, H]` ≥ −60 dBFS;
  - arm 3: within [−18, +12] dB;
  - arm 4: the tail criterion.

  [roadmap 648–649]
- **FR-024b — 44.1 kHz arm 1.** Each roster preset's arm 1 (finite, peak ≤ 0.9661, every 10 s RMS ≤ −6 dBFS) is
  green at 44.1 kHz on the stored take, read in the same probe run and with the same override string as its gate
  (FR-004's 44.1 kHz render). This is the sweep's existing 44.1 kHz arm (`preset_test_support.h:3176`,
  `:3193-3194`; Stone Gravity's red, `compliance.md:589`). Arms 2–4 at 44.1 kHz are not gated: no instrument in
  the harness computes them, and the roadmap's gate is the stored take at the sweep rate.
- **FR-024c — Not a limiter pass.** For each roster gate, the same probe run is repeated with the master gain
  lowered by 6 dB (the `kMasterGainId` setting appended to the gate's override string; master gain sits before
  the output limiter, `plugins/vorago/src/processor/processor.cpp:1159-1166`, and the twins inherit it). The
  cell must still reach its bar (FR-020 – FR-023) on that reading. This makes E-2's "a pass that leans on the
  limiter is not a pass" measurable.
- **FR-025 — Override sets are part of the gate record.**
  - Where a gate reads with `VORAGO_PILOT_OVERRIDE`, the exact override string goes into the lever table and the
    compliance row. That string is the hand-off Phase 14 adopts at T048.
  - A gate read with no override records "as compiled".
  - **The override scope is restricted.** On a roster preset, a gate's `VORAGO_PILOT_OVERRIDE` string may change
    only the cell's own controls — the ablated parameter, the cell's macro, or the route depth it tests — plus
    level trims (master gain, section mixes). It must not touch any other preset's controls, and it must not
    repair a different cell. The 27 sweep-4-verified primaries (FR-030) and the FR-030b secondary/default-state
    set are read **as compiled**, with no override. A lever that needs a repair override on one of them to keep
    it passing is not adopted as is.

  [roadmap 654–655]
- **FR-026 — Final-tree re-measure.** Once all levers are in, in FR-010b's fixed order (including any re-opened
  levers), every gate is re-run once on the final binary, with nothing else running. The compliance row cites
  that log line, never a mid-phase reading. [roadmap 649: "re-measured on the final tree"]
- **FR-027 — No UNMET.**
  - If no measured lever brings a cell to its gate, the cell becomes a stop-and-surface item for the user, with
    the readings.
  - It is never recorded UNMET as an outcome (Phase 14 pause ruling, `compliance.md:558-562`).
  - No bar is relaxed.

  [roadmap 639, 656]

### D. No regression

- **FR-030 — Verified cells stay verified.**
  - Each of the 27 primaries verified at sweep 4 is probed on the final tree, **as compiled, with no
    `VORAGO_PILOT_OVERRIDE`** (FR-025's scope restriction). Each must still reach its bar with its arms green. A
    lever that needs a repair override on one of them to keep it passing is not adopted as is.
  - The set is **derived, not hand-typed**: `requiredPrimaryCells()` (`tools/vorago_preset_defs.h:297-322`, 42
    cells) minus the 15 roster primaries. That gives S1, S2, S3, S5, S7, S8, S10, M1, M6, M7, **M8**, M11, E2,
    D1.1–D1.5, D1.8–D1.11, D3.1–D3.4 and D8.2, which is 27, matching the sweep-4 aggregate's "verified
    primaries: 27" list (`specs/vorago-phase14-presets-release/artifacts/sweep4_aggregate.log`, the
    `CoverageComplete` block; M8 Weight on Weighted Deep is in it). The compliance prose at
    `compliance.md:581-582` lists only 26 because it omits M8; that is a transcription error in the Phase 14
    record, and this spec's list follows the aggregate log.
  - A lever that pushes one of them under its bar is not adopted as is.
  - Each cell's before value is its sweep-4 row.

  [roadmap 654–656]
- **FR-030b — Secondary and default-state verifications stay verified.** Phase 14's coverage criterion is every
  C-2.1 cell verified (Phase 14 spec SC-008), which includes cells verified only as secondaries or on the
  default surface. A global lever (E-1) can push one of those under 1.5 and so create a new unverified cell.
  - **The set** is read from the sweep-4 coverage matrix (`sweep4_aggregate.log`, the `CoverageComplete`
    matrix, legend "S verified secondary", plus the default-surface column `d`). It is every cell that is
    **not** one of the 42 required primaries and is verified at sweep 4 as a claimed secondary (`S`) on a host
    preset or in the measured default-state set. It includes at least E6.hi (verified secondary of Colony
    Pulse), the re-homed noise models D4.7 Blue, D4.8 Violet and D4.9 Grey (`compliance.md:583`), and the
    measured default-state cells. At sweep 4 the measured default-state set is D1.6, D1.7, D2, D5.3, D8.1,
    D9.2, D10.2 and D12.2; the recorded constant also lists D7.1, which the sweep-4 measurement already lacks
    (the `CoverageComplete` "measured default-state set … recorded …" message). D7.1 is therefore recorded as
    unverified **before** this phase and surfaced in the before-record, not attributed to a 13c lever. The plan
    writes the full enumerated list into the before-record.
  - **The reading:** each host-preset secondary gives `verifiedAt(…, ClaimRole::Secondary)` true on its host on
    the final tree, read through FR-004's secondary readout. Each default-state cell is read on the default
    pseudo-preset with `Vorago_PresetSweep_AblationVerifiesClaims` (the T043 instrument, Phase 14
    spec.md:1579-1588).
  - A lever that de-verifies one of them is not adopted as is.
  - A table records each cell's sweep-4 (before) and final (after) values.

  [roadmap 637–639, 654–656]
- **FR-031 — Phase 10 bounds.** These stay green, with no threshold relaxed:
  - **(ruling B-14)** The Phase 10 axes are restored by the default ghost peak 0 (`kGhostDefaultPeakLevel`),
    not by a fixture change: `l7_sc011_ruled.log` 61 / 62, the one red the base tree's own Movement rho; every T004-passing assertion green.
  - `VoragoMacro_NoZipper` (zipper bound 1.5×);
  - `VoragoMacro_SweepAxes` (every assertion that passes on the base tree still passes);
  - the Phase 10 overnight and boundedness soaks;
  - the CPU ceiling (FR-033).

  [roadmap 650–651]
- **FR-032 — 13b's gates.**
  - 13b's Gate 1 is re-measured on the final tree with `Vorago_EcosystemRuleProbe`. The gate is: ecosystem off vs
    on ≥ 0.5 of the six-seed off-reseed median, at the default surface and at Life max.
  - **13b's Gate 2 knobs keep counting.** On the final tree, `Vorago_EcosystemRuleProbe` runs its 14-knob table
    at the default surface and at Life max. Every knob counted on 13b's shipped tree must still count on the same
    surface: d/t0 ≥ 0.5, not a colony kill, not off-like (the probe's `counted` rule). Those knobs are
    **syncRate** on the default surface (d/t0 0.534) and **selfAffinity** (0.650) and **syncRate** (0.528) at
    Life max (13b `compliance.md:43`, `:70`, `:134`; `final2_table_default.log:56`,
    `final2_table_lifemax.log:58,63`). Phase 14 Q2 registered its rule parameters from these tables, and they
    underlie E6.hi and E7.hi, while FR-012 rescales the route levers and FR-014 rescales Life.
  - The base-tree tables are re-run in FR-003 (iii), because Phase 14's noise-bus and ghost-tap make-ups landed
    after 13b closed. If a knob that counted at 13b no longer counts on the base tree, that is a before-record
    stop-and-surface item, not a 13c regression.
    **(ruling B-1)** Surfaced and ruled on 2026-10-01: the knobs stay a hard final-tree target, restored through
    FR-012 and FR-014; the base-tree tables are the before.
  - 13b's routing and lever tests stay green: `VoragoVoice_EcosystemLever*`, `VoragoVoice_WakeCombineRule` and
    `VoragoVoice_AgentReductionRule`.

  [roadmap 651]
- **FR-033 — CPU.** These all pass, measured with `node tools/run-cpu-tests.js` and nothing else running:
  - `VoragoEngine_CpuBudget` clause (i): engine + `kCavernMeasuredNsPerBlock` ≤ `kReferenceNs`;
  - `VoragoEngine_CpuBudget` clause (ii): engine ≤ `kEngineBaselineNsAtPoly4` × 1.5;
  - `Vorago_ProcessorCpu` SC-014: P/D ≤ 1.05.

  The before figure (base tree) and the after figure are recorded in ns and %. [roadmap 651, 659: "CPU delta"]
  **(ruling B-2)** Clause (i) is judged on an idle machine where the pre-13c base binary passes it, by an
  alternating pinned A/B of `VoragoEngine_CpuBudget` (base binary vs final binary, two rounds, 60 s settles)
  in addition to the lanes; the before is the 2026-10-01 A/B (`cpu_base_ab_*.log`). Nothing is relaxed.
- **FR-034 — Suites.**
  - The Phase 2–14 per-push suites pass: `dsp_*_tests`, `vorago_tests`, `seraphis_tests`, `shared_tests`. The
    `[long]` soaks pass too.
  - A pre-existing test may encode the old voicing **as data**. Before such a test is re-measured, it is listed
    with its reason (the 13b FR-031 shape). It is never edited silently.

  [roadmap 651–652]
- **FR-035 — Fingerprints re-harvested in their consuming binary.**
  - Any render fingerprint that moves is re-harvested. This includes at least `VoragoEngine_GhostExtensionWiring`
    clause (a) (`atmosphere_ghost_fixtures.h`, currently on its seventh harvest).
  - The re-harvest happens once, on the final tree, inside the binary that consumes the fingerprint.
  - It runs twice, the two runs must produce byte-identical literals, and it adds a new PROVENANCE block.

  [roadmap 652]
- **FR-036 — Cross-cutting gates.**
  - zero warnings;
  - `node tools/check-portability.js` clean, followed by `wsl --shutdown`;
  - clang-tidy `dsp` and `vorago`: 0 findings;
  - pluginval strictness 5 on `Vorago.vst3`.

  [roadmap 651]
- **FR-037 — Seraphis untouched**, unless FR-019's ruling applies. `git diff --name-only 64f57e1a..HEAD` lists no
  `plugins/seraphis/**` path and no Seraphis-consumed header. [roadmap 739–741]
- **FR-038 — ODR.** Before any new class, struct or constant name is written, it is swept with
  `grep -rn "<Name>" dsp/ plugins/ tools/`, and the sweep is recorded in the plan. [roadmap 724]

### E. Evidence

- **FR-040 — Lever table.** Compliance carries one row per feature (FR-018), with these columns:
  - feature;
  - cells served;
  - lever (when the feature's lever is a combined change set per FR-018/Q5 — bloom's decoupling + child gain,
    Gravity's row change + arm-3 fix — every mechanism in the set is itemized here, still as one row);
  - before value;
  - after value;
  - every candidate measured (value → reading, log);
  - the ruled value;
  - the override string (FR-025, scope-restricted per FR-025/FR-030).

  [roadmap 658]
- **FR-041 — Default-render change documented.** The change is recorded as intentional voicing, with FR-005's
  before and after figures. [roadmap 650, 658–659]
- **FR-042 — Phase 14 hand-off.** The final compliance:
  - lists, for each showcase preset, the override string its gate used, so Phase 14's T048 re-author is a
    transcription;
  - records any change to the descriptor-visible default surface that Phase 14's E0 and P0 rows depend on.

  [roadmap 654–656]

---

## Success Criteria

| ID | Metric | Threshold | How measured (test sketch) |
|---|---|---|---|
| **SC-001** | before-record fidelity | (i) on the unmodified `64f57e1a` binary, each roster primary's reading (d, and `attribBase` / `d_Sus` where printed) agrees with its sweep-4 row within `max(0.01, 0.005·d_sweep4)`, and one cell's repeat-run spread on the same binary is recorded and lies within that tolerance; (ii) on the FR-004 reporting binary, every primary re-read stays within the same tolerance of its (i) value, and the secondaries' before values are recorded | `VORAGO_PILOT_PRESET=<name> vorago_tests.exe "Vorago_PresetPilot_PrimaryProbe"` per roster preset, nothing else running; logs `artifacts/before_<cell>.log` (i) and `artifacts/before2_<cell>.log` (ii) |
| **SC-002** | S4, S6, S9, M2, M3, M4, M5, M9, M10, M12 gate | `d ≥ 4.0`, and the four arms green in the same printout | the same probe on the final tree; the row cites the verdict line and the `take:` arm line |
| **SC-003** | E1, E3, E4, E5 gate | `d(R_k, R_k0) ≥ 4.0` and `≥ attribBase + 1.5`; arms green | the probe's `route arms:` line, verdict line and arm line |
| **SC-004** | D9.1 gate | `d_att ≥ 4.0` and `≥ d_Sus + 1.5`; arms green; for both renders, the time to reach RMS(Sus) − 6 dB is printed. The row states that option (b) (stage-0 curve reshape) shipped, option (a)'s measured-only comparison reading, what `P_rev` is under the shipped option, and both window ends (registered `audibleAttackSeconds` and measured first reach); the gate uses the measured-reach window (FR-016) | the probe's `attack window` line and its `P` / `P_rev` reach lines |
| **SC-005** | M5 arms 1 and 3 | arm 3 (late-sustain − Sus) within [−18, +12] dB **and** arm 1 (peak ≤ 0.9661, every 10 s RMS ≤ −6 dBFS) on all K = 4 takes, and arm 1 at 44.1 kHz, all with the gate's override string (before: arm 3 +15.7 dB take 0; arm 1 hi −5.92 dBFS take 3; 44.1 kHz hi −5.97 dBFS); plus the FR-015 cause table (arm 3 per Gravity-row / breathing-lane ablation) logged before the lever is ruled | the probe's all-take arm lines and 44.1 kHz arm-1 line (FR-004) on Stone Gravity; `artifacts/m5_cause.log` |
| **SC-006** | secondaries | E7.hi, D13.1, D13.2, D14.1 and D14.2 each give `verifiedAt(Secondary)` true, with `d ≥ 1.5`, state ok and conjunct ok; D14.x through the FR-017b lever | the probe's secondary readout (FR-004) on Colony Pulse, Teeming, Slow Bloom and Drifting Strata |
| **SC-007** | no regression of verified primaries | all 27 sweep-4-verified primaries, taken as `requiredPrimaryCells()` minus the 15 roster cells (M8 included), reach their bar with arms green on the final tree | the probe per preset; a table of sweep-4 d vs final d, its roster generated from `requiredPrimaryCells()` |
| **SC-007b** | no regression of secondary / default-state verifications | every cell in the FR-030b set gives `verifiedAt(…, ClaimRole::Secondary)` true on its host preset (or verifies on the default pseudo-preset) on the final tree; D7.1, already unverified at sweep 4, is reported separately | the probe's FR-004 secondary readout per host; `Vorago_PresetSweep_AblationVerifiesClaims` on the default pseudo-preset; a before (sweep 4) / after table |
| **SC-008** | bloom audibility mechanism | with the shipped bloom lever, at richness 0.0, 0.35, 0.40, 0.70 and 1.0 (user N(r) = 1, 4, 5, 18, 64): (a) every live child's slot is `< getActivePartialCount()` and its sounding amplitude (`getPartialCurrentAmplitude`) is above the silent threshold `kSilentParentAmplitude` = 1e-5; (b) each parent slot `i < min(N(r), 8)`'s amplitude **relative to slot 0** (bloom on) is within ±0.5 dB of that same ratio (bloom off) at the same richness, outside its child-latch instants; the shared normaliser's absolute per-parent drop at the ruled child gain is measured and logged, not compensated (FR-011). If (a) and (b) cannot both hold at r ≤ 0.40 within FR-019, FR-011's stop-and-surface applies; the criterion is not relaxed | `VoragoVoice_BloomCountsProbe` extended (hidden) to print per-child slot, active count and per-slot amplitude, bloom on vs off; plus a per-push sentinel `VoragoVoice_BloomChildrenAudible` asserting (a) and (b) |
| **SC-009** | lever neutrality (13b invariant) | at ecosystem depth 0, every 13b lever destination reads exactly its base | `VoragoVoice_EcosystemLeverNeutral` green with no edit, or re-measured under FR-034's surfaced list |
| **SC-010** | macro neutrality and predicates | with every macro at 0, every target equals its base exactly; every `static_assert` row predicate compiles | the neutral-identity tests in `vorago_macro_test.cpp` green; the build |
| **SC-011** | Phase 10 bounds | `VoragoMacro_NoZipper` ≤ 1.5×; `VoragoMacro_SweepAxes`: every assertion that passed in the FR-003 (iii) base-tree log still passes | `[long]` runs, base-tree and final logs cited |
| **SC-012** | 13b Gate 1 | GATE1M ratio ≥ 0.5 at the default surface and at Life max (before: 0.656 / 0.638, roadmap 602–603) | `Vorago_EcosystemRuleProbe` with `VORAGO_PROBE_KNOBS=-` and the true-off reference, on both surfaces |
| **SC-012b** | 13b Gate 2 knobs | on the final tree, syncRate counts on the default surface and selfAffinity and syncRate count at Life max: d/t0 ≥ 0.5, no kill, not off-like (before at 13b: 0.534; 0.650 and 0.528, `final2_table_{default,lifemax}.log`; base-tree values from FR-003 (iii) recorded alongside) | `Vorago_EcosystemRuleProbe` 14-knob table at both surfaces, `artifacts/gate2_table_{default,lifemax}.log` |
| **SC-013** | boundedness | worst case named explicitly: six voices held, all twelve macros at 1 with Gravity run at **both** 0 and 1 (bipolar, E-6), bloom depth and spawn rate at max with the shipped child gain and placement, ghost peak level 1.0 with the shipped ghost-tap make-up and density, ecosystem depth 1 with the eco lanes injected at 1, at 44.1, 48 and 96 kHz: every sample finite by bit pattern, \|out\| ≤ 0.9661, allocation footprint unchanged after prepare; the Phase 10 soak and the plugin `soak_test.cpp` green | a new or extended bounded test for that state (the `VoragoEngine_EcosystemLeverBounded` pattern, `vorago_ecosystem_lever_test.cpp:1742-1790`, which today applies only Life = 1), plus that existing test and the soaks green |
| **SC-014** | CPU | `VoragoEngine_CpuBudget` clauses (i) and (ii) pass; `Vorago_ProcessorCpu` P/D ≤ 1.05; the delta against the base tree recorded in ns and % | `node tools/run-cpu-tests.js dsp_systems_tests`, then `vorago_tests`, nothing else running, machine cooled; the WARN lines cited; clause (i) by the alternating pinned A/B vs the base binary on an idle box (ruling B-2), `cpu_final_ab_{base,final}_run{1,2}.log` |
| **SC-015** | determinism | the same seed twice matches within `render_fingerprint.h` tolerances; `VoragoEngine_SlotSeedReproducibility` green | per-push |
| **SC-016** | fingerprint re-harvest | every moved fingerprint is re-harvested in its consuming binary: two byte-identical runs and a green verify run | the `VoragoEngine_GhostExtensionWiring` printer runs; md5 of the literals |
| **SC-017** | regression suites | 100 % pass, `[long]` included; test files edited only under FR-034's surfaced list | full logs |
| **SC-018** | default render recorded | before and after: the 13b probe's t0, t0on and M1 RMS; the Phase 14 P0 descriptor and M1 RMS; output finite, peak ≤ 0.9661 | the FR-005 logs |
| **SC-019** | surface unchanged | registered parameter count, IDs, types and ranges unchanged; `kCurrentStateVersion == 3`; v2 and v3 round-trips green. No registered default value moves: OQ-2 is ruled option (b) (FR-016), which reshapes stage 0's curve, not the stored defaults | `param_table_test`, `state_v2_test`, `state_v3_test` |
| **SC-020** | sample-rate robustness (FR-024b) | each roster preset's arm 1 (finite, peak ≤ 0.9661, every 10 s RMS ≤ −6 dBFS) green at 44.1 kHz on the stored take, with the gate's override string | the probe's 44.1 kHz arm-1 line (FR-004), same run as the gate |
| **SC-021** | cross-platform | zero warnings; check-portability clean; clang-tidy `dsp` + `vorago` 0/0; pluginval 5 clean | tool logs |
| **SC-022** | Seraphis untouched | the diff lists no Seraphis path and no Seraphis-consumed header (or FR-019's ruling is cited) | `git diff --name-only` in compliance |
| **SC-023** | not a limiter pass (FR-024c) | each roster cell still reaches its bar with the master gain lowered by 6 dB on top of its gate override string | the probe re-run per roster preset; the override string and verdict line cited |

---

## Edge cases

- **E-1 A global lever moves every preset.** Bloom, ghost, route-lever and macro-row changes reach all 42 presets,
  not only the showcase. FR-030 and SC-007 guard against this. A lever that rescues one cell and sinks a verified
  one is not adopted as is.
- **E-2 Louder is not more distinct.** The descriptor is level-invariant (C-7.2).
  - 13b measured that a lane gain of 3 "becomes a fixed boost the level-normalised descriptor largely cancels"
    (`vorago_voice.h:1661-1664`).
  - Spore Drift read lower when it was louder, because the dust drove the limiter (`compliance.md:442-443`).
  - A lever's reading is taken with arm 1 green. Arm 1 alone does not exclude a limiter-held pass: its peak bound
    (`kSweepPeakCeiling = 0.9661`, `preset_test_support.h:1730`) equals the output ceiling (`kOutputCeilingDb =
    -0.3 dB`), and a clamped render passes it (Resonant Shaft batch 10, "peak 0.9661 (momentary)" with S2 PASS, `compliance.md:569`). So a pass
    that leans on the limiter is excluded by FR-024c / SC-023: the cell must still reach its bar with the master
    gain 6 dB lower.
- **E-3 Silence is not distinct either.** Feedback Mire's sweep-3 "pass" was a silent render compared against a
  loud twin (`compliance.md:544-548`). Requiring arm 2 in the same printout (FR-024) excludes this.
- **E-4 Attributability.** For a route cell, a lever that raises the route-independent residue `d(R_0, R_00)` as
  much as `d(R_k, R_k0)` does not pass (FR-021). Bloom Colony's before reading already has `d = attribBase`
  exactly (0.0568).
- **E-5 Life depends on other features.** Life's rows drive the ecosystem depth, the event rate and the bloom
  spawn rate. Its gate means something only after every other lever in FR-010b's fixed order has landed (bloom,
  routes, ghost, S4, attack, life modulators), per the ordering in FR-010b and FR-014.
- **E-6 Gravity is bipolar.**
  - `contributionOf` maps Gravity as `(m − 0.5)·2` with its sign (`vorago_macro_matrix.h:1160-1166`), so a
    widened Gravity row acts on both halves of the knob.
  - The M5 ablation resets Gravity to the registered 0.5.
- **E-7 Zipper.**
  - Movement is the axis on which 13b measured a break of Phase 10's zipper bound, with a 6 st wander lever
    (`vorago_voice.h:1619-1624`). Widening Movement pushes the wander rates toward 1 Hz.
  - SC-011 binds every row widening.
- **E-8 Child slots and capacity.** A bloom lever might raise the capacity, move the child slots or change the
  parent hand-off. Any of these must keep:
  - `BloomEngine::processChunk`'s precondition of at least 64 writable floats (`bloom_engine.h:467-481`);
  - the four acceptance conditions of `setSpectralTarget` (`vorago_voice.h:2319-2340`).
- **E-9 Attack span vs timeline.**
  - C-6's `A` stays the stage-time sum. It sets the Sus placement and the D9 state predicate.
  - Only the S-9 audible attack sets the comparison window.
  - OQ-2 is ruled option (b): the registered default does not change, so the default surface's `A` and Sus
    content are untouched by this lever; only the voice's stage-0 curve shape moves, which moves the audible
    attack without moving `A` or the Sus placement.
- **E-10 Seed determinism.**
  - Probe gates are stored-seed readings (FR-002; roadmap 647). This phase sets **no** cross-seed distance gate:
    the roadmap gates the stored take, and cross-seed behaviour is Phase 14's K-take protocol at the confirming
    sweep.
  - **Record only:** the final-tree record prints, for each roster primary, `d` on each of the K = 4 takes
    alongside the gated stored-take `d`, so a lever that only works on the stored colony is visible to the user
    when it is ruled. The all-take **arms** are printed too (FR-004); they are gated only for Stone Gravity
    (FR-015), and informative elsewhere. Sweep 4 found a level arm red on one seed only for Resonant Shaft
    seed 4 (`compliance.md:590-591`, a Phase 14 resume item) and for Stone Gravity take 3 (gated here).
- **E-11 Sample-rate changes.**
  - Levers run per control chunk or per sample, with ramp times in seconds.
  - The bloom's capacity floor and active count do not depend on the sample rate.
  - FR-024b / SC-020 check arm 1 at 44.1 kHz.
- **E-12 RT safety.** Every lever is a constant, a row, or a setter call on the control path. Nothing allocates,
  locks or throws on the audio thread, and no new pool is introduced.
- **E-13 Non-finite input.** Every setter used already rejects non-finite values. Examples:
  `BloomEngine::setChildGain` (`bloom_engine.h:561-566`) and `VoragoEngine::setGhostTapMakeupDb`
  (`vorago_engine.h:1004-1008`).
- **E-14 Fingerprint churn.** Each mid-phase lever moves the ghost fingerprint, so a harvest taken before the
  final tree is wasted. FR-035 therefore re-harvests once.

---

## Open questions (only where the roadmap defers to this spec)

- **OQ-1 — Which lever ships for each feature, and at what value.** The roadmap lists candidates and requires each
  one to be "measured before it is ruled" (roadmap 639–640). The Clarifications session fixed the *mechanism*
  for bloom (always-on decoupling plus child gain, FR-011) and the *order and grouping* in which every feature
  is ruled (FR-010b, FR-018), but not the numeric values themselves. The plan still proposes the measurement
  order within that fixed sequence and the candidate values for each feature:
    - bloom: the ruled child gain value (the decoupling mechanism itself is fixed, see FR-011);
    - routes: which spans or input gains;
    - ghost: level or density;
    - each macro: which rows, and whether to widen them or add a target;
    - S4 and M3, which the roadmap does not name.
  - The user rules each lever's value on its logged readings, per FR-010b's fixed order.
- ~~OQ-2~~ — **Settled by the Clarifications session** (Q4): option (b) ships — the voice reshapes stage 0's
  curve; the registered defaults do not move. See FR-016, SC-004, SC-019 and the Clarifications log below.

---

## Traceability

| Roadmap statement (lines) | FR / SC |
|---|---|
| Depends on 13b levers and the Phase 14 harness (617–618) | FR-002, FR-012, FR-032 |
| Premise: inaudible cells and their readings (621–636) | Cell roster, FR-001, FR-003, FR-004, SC-001 |
| Goal: every cell heard at its showcase preset, F = 4.0, no UNMET (637–639) | FR-017b, FR-020 – FR-023, FR-027, SC-002 – SC-006 |
| Candidates: bloom child gain, parent selection (640–641) | FR-011, SC-008 |
| Candidates: route lever strengths (641–642) | FR-012, SC-003 |
| Candidates: macro row amounts (642–643) | FR-014, FR-015, SC-002, SC-005 |
| Candidates: ghost level, burst density (643–644) | FR-013, SC-002 (S9), SC-003 (E5) |
| Candidates: registered envelope's audible attack (644–645) | FR-016 (OQ-2 ruled option (b)), SC-004 |
| Gate per cell with arms, re-measured on the final tree (647–649) | FR-020 – FR-026 (incl. FR-024b, FR-024c), SC-002 – SC-005, SC-020, SC-023 |
| Default render may change and is recorded; Phase 10 bounds, 13b gates, suites, pluginval, tidy, portability, [long] (650–652) | FR-005, FR-030, FR-030b, FR-031 – FR-036, FR-041, SC-007, SC-007b, SC-009 – SC-012b, SC-013 – SC-019, SC-021 |
| Fingerprints re-harvested inside their consuming binary (652) | FR-035, SC-016 |
| Phase 14 resumes at T048 on the new levers; sweep 4 is the confirming run, no primary UNMET (654–656) | Non-goals, FR-015 (M5 known reds), FR-025, FR-030, FR-030b, FR-042, SC-005, SC-007, SC-007b |
| Success criteria: gates, lever table, default-render change, no regression, CPU delta (658–659) | FR-040, FR-041, SC-002 – SC-007, SC-014, SC-017, SC-018 |
| Cross-cutting: RT safety, boundedness, ODR, CPU as FR, no bit-exact goldens, portability, shared-component rule (717–741) | E-12, SC-013, FR-038, FR-033, SC-015, FR-036, FR-019, FR-037 |

---

## Review notes

Second-pass review (2026-10-01). All fourteen issues were applied, none rejected. Where an issue offered a
choice, this records which way it was resolved:

- **13b Gate 2 (FR-032 / SC-012b).** A Gate 2 clause was added. Its before values are both the 13b shipped
  tables and a base-tree re-run (FR-003 iii), because Phase 14's make-ups landed after 13b closed.
- **Secondaries and default-state cells (FR-030b / SC-007b).** Added. The sweep-4 aggregate shows D7.1 already
  missing from the measured default-state set, so D7.1 is reported as a before-record item and not charged to a
  13c lever.
- **M5 reds (FR-015 / SC-005).** These are extended into the gate (arms 1 and 3 on all K takes, arm 1 at
  44.1 kHz) rather than deferred. They are known reds of a roster cell, and the confirming sweep is meant to find
  none.
- **SC-020 (FR-024b).** A backing FR was added. It is limited to arm 1, which is the only thing the harness's
  44.1 kHz arm measures. It is read through a probe render that honours the gate's override string, because the
  sweep helper renders the compiled def. Arms 2–4 at 44.1 kHz are not gated, since the roadmap's gate does not
  define them.
- **SC-008 blocker.** Resolved by option (a). The metrics are now sounding amplitude per child slot and
  per-slot parent preservation, and the N(r) arithmetic is written into FR-011. The richness points are kept at
  0.0, 0.35 and 0.40, not moved to r ≥ 0.626, because the roadmap asks for "richness independence"
  (roadmap 640–641). Moving them would drop that requirement. The geometric conflict is instead resolved by
  requiring a Vorago-side decoupling of the cloud's active count, with a stop-and-surface on
  `harmonic_cloud.h` if none is found.
- **FR-030 count.** M8 was added, and the set is now derived from `requiredPrimaryCells()`. The 26-item list at
  `compliance.md:581-582` is noted as a transcription error. The sweep-4 aggregate's own list has M8.
- **OQ-2 instrument.** The effect on `P_rev` and `W_end` is now stated for each option. Option (b) has one
  explicit FR-002 exception, a measured-reach window, which also covers D8.2 because its twin reverts to
  Standard mode.
- **SC-001.** Equality at print precision is replaced by a measured reproduction tolerance. The before-record is
  split into an unmodified-binary step and a reporting-binary step.
- **SC-013.** The worst-case state is now named explicitly, and a new or extended bounded test is required.
- **D14.x.** FR-017b was added: a breathing and tidal depth-to-destination lever.
- **E-10.** This was the one issue resolved by downgrading. The cross-seed distance stays record-only, because
  the roadmap's gate is the stored take (roadmap 647) and cross-seed behaviour belongs to Phase 14's K-take
  protocol. No threshold is invented here.
- **E-2.** This is now enforced by FR-024c / SC-023, a re-read with master gain 6 dB lower. Master gain sits
  before the limiter (`processor.cpp:1159-1166`).
- **FR-015 cause and SweepAxes baseline.** The FR now names its instrument and evidence (an ablation table of
  Gravity rows and the breathing lane). The SweepAxes and NoZipper base-tree logs are added to FR-003 (iii).

---

## Clarifications

### Session 2026-10-01

- **Q1 — Bloom parent preservation vs the cloud's constant-RMS normaliser.** → Decision: parents are measured
  **relatively** — each parent keeps its ratio to parent 0 within ±0.5 dB; the shared normaliser's drop from
  audible children is accepted and recorded, not compensated; the child gain is free to rise. [FR-011, SC-008]
- **Q2 — When the voice decouples the cloud's active count from the user's richness.** → Decision: **always
  on** — every voice runs the cloud at `N ≥ kMinCloudCapacity` (14) unconditionally; slots at or above the
  richness-derived count are zeroed through the spectral target; there is no switching path; the small
  permanent CPU cost is recorded. [FR-011, FR-033, SC-014]
- **Q3 — What a gate's `VORAGO_PILOT_OVERRIDE` may change.** → Decision: on a roster preset, only the cell's own
  controls (the ablated parameter, macro or route depth) plus level trims (master gain, section mixes); the 27
  presets verified at sweep 4 are read as compiled, with no override — a lever that needs a repair override on
  a verified preset is not adopted. [FR-025, FR-030]
- **Q4 — OQ-2: which attack lever.** → Decision: option (b) — the voice reshapes stage 0's curve so the
  registered 20 s stage-0 time is audibly ~20 s, without moving any stored default; option (a) is still
  measured as a comparison candidate before ruling, per FR-010; the measured-reach window exception applies to
  D9.1 and D8.2. OQ-2 is settled. [FR-002, FR-016, SC-004, SC-019]
- **Q5 — "One lever per feature" when a feature needs two mechanisms.** → Decision: a lever is one coherent
  change set per feature and may combine the named candidates (bloom: active-count decoupling + child gain;
  Gravity: macro-row change + the arm-3 self-growth fix); one lever-table row per feature; each macro (M2, M3,
  M4, M5, M9, M10, M12) is its own feature with its own row and ruling. [FR-018, FR-014, FR-011, FR-015, FR-040]
- **Q6 — Ruling order and re-opening when a later global lever moves an earlier cell.** → Decision: fixed
  order — bloom → routes → ghost → S4 → attack → life modulators → macros (Life last) — each lever measured on
  the tree with all earlier levers in; an earlier cell that drops below its bar re-opens its lever before work
  continues. OQ-1 stays as written: every lever and its value is still ruled by the user on its own logged
  probe readings. [FR-010b]

### Plan stage (2026-10-01) — plan §10 items, ruled

- **P1 — Q2 reading.** → Confirmed: the cloud's active count is floored at 14 unconditionally; the bloom's
  spectral target is engaged whenever the user's richness count is below the cloud's or a child lives (no
  switching path, the default surface's code path unchanged where no child lives, the four
  `hasSpectralTarget()` edge assertions kept). [FR-011, SC-008, SC-014]
- **P2 — Measurement seams.** → **Adopted** as a third measurement route (plan §2.9): engine fan-outs
  `setBloomChildGain` / `setEcologyWetMakeupDb` / `setGhostDensity` with getters, the voice forwards, the
  test-only `PresetHost::engineForTweak()` and `RenderSpec::engineTweak`, and `VORAGO_PILOT_LEVER` parsed by
  the probe TU. Each setter ships with the ruled value as its default (the `setGhostTapMakeupDb` shape) and
  exists so a ladder rung is a probe run, not a rebuild. **No gate reading is ever taken with a lever env set**
  — FR-026's final re-measure reads the compiled constants; the probe REQUIREs `tweakHeld` on every capture.
  FR-002 and FR-010 read accordingly (below). Tasks: Group 3b. [FR-002, FR-010, FR-019]
- **P3 — E1 / E5 route terms.** → Confirmed: `kPartialLaneGain` and `kGhostLaneGain` SIZE the existing
  Partial and Ghost writes; 13b's `kLeverInputGain` and its `static_assert` (Partial = Ghost = 1) are
  untouched; the spawn-rate destination stays an unadopted scope extension, measured only if the size ladder
  leaves E1 short. [FR-012]
- **P4 — Bloom child-gain ladder.** → Confirmed: 0.35 / 0.50 / 0.70 / 1.00, each rung re-read at a −6 dB
  master trim; 1.0 short of S6 or E1 is a stop-and-surface item. [FR-010, FR-011]
- **P5 — 13b's `VoragoVoice_EcosystemLaneShapingFidelity`.** → Confirmed on the FR-034 surfaced list,
  re-specced only if a Partial or Ghost lane gain other than 1 is ruled. [FR-034]
- **P6 — Attack tracking floor.** → Confirmed: the measured `P_rev` reach lies in `[0.75 · T0, T0]` =
  [15, 20] s for the registered 20 s stage 0; the stage-0 phase power is chosen on that window, measured
  against option (a) first. [FR-016, SC-004]
- **Fallbacks and values.** The life-lane second destinations and the macro new-target rungs stay proposals,
  measured only when a first rung is short; every lever's value is ruled by the user on its logged readings
  (OQ-1). The M5 lever waits on the measured cause table.

### Build stage (2026-10-01) — before-record rulings (Group 1, T005 / T006)

- **B-1 — 13b's Gate-2 knobs on the base tree (FR-032 / SC-012b).** T005 found none of 13b's three Gate-2 knobs
  counts on the base tree: syncRate on the default surface d 2.1606 → 1.0589 (d/t0 0.260), selfAffinity and
  syncRate at Life max 0.650 / 0.528 → 0.286 / 0.121 (`gate2_table_default_base.log:97`,
  `gate2_table_lifemax_base.log:99,140`). The per-knob colony means are IDENTICAL to 13b's `final2_table_*.log`
  rows (e.g. syncRate M1 colony 0.478464 on both), so the simulation is unchanged: the +30 dB noise bed and
  +12 dB ghost tap of Phase 14 dilute what the ecosystem knobs change in the descriptor. Gate 1 passes
  (0.614 / 0.578). → **Ruled: SC-012b stays a hard final-tree target.** The base-tree 0-of-14 tables are the
  recorded before; the route lever (FR-012, `kPartialLaneGain` / `kGhostLaneGain`) and the Life lever (FR-014)
  are the levers that feed these knobs; T058's final tables must show all three counting, else that is a
  stop-and-surface item at T058. No re-scope. [FR-032, SC-012b]
- **B-2 — CPU clause (i) red on the unchanged base tree (FR-033 / SC-014).** T006's lane and two isolated
  pinned re-runs read `VoragoEngine_CpuBudget` clause (i) RED (engine 3.37e6–3.49e6, + Cavern 3.50e6–3.61e6
  against 3.2e6; `cpu_base_dsp.log:3419,3459`). The lane also ran under a runaway `node -e` process (95 % of a
  core, killed 17:33) and the box carries a VS Code WSL session, Docker and Defender (8–13 % load). An
  alternating pinned A/B against the pre-Phase-14 binary `05d04f66` (`cpu_base_ab_{05d04f66,base}_run{1,2}.log`):
  05d04f66 + Cavern 3.219e6 / 3.487e6, base 3.360e6 / 3.401e6 — both red, overlapping; the same 05d04f66
  binary read 2.909e6 on 2026-09-27 and Phase 14's T049 read arm D 2.920e6 on 2026-09-30. The machine, not the
  code. → **Ruled: clause (i) stays absolute, nothing relaxed.** Today's A/B is the recorded before. T060's
  final CPU lane repeats the alternating pinned A/B against the base binary on an idle machine (WSL, Docker and
  the VS Code remote session closed, after a reboot if needed); clause (i) is judged on a box where the base
  binary passes it, and 13c's delta vs base is reported in ns and % either way. If the base binary cannot be
  brought to pass, that is a stop-and-surface item at T060. T002–T005 are deterministic renders (T003's repeat
  spread 0.0000) and are not re-run. [FR-033, SC-014]
- **B-3 — Bloom child-gain ceiling (FR-010, FR-011, T026 stop-and-surface).** The ladder on the L1 tree
  (floor in; `l1_bloom_<v>_S6.log`, `_E1.log`, `_minus6.log`, `l1_sentinel_<v>.log`): Slow Bloom S6 primary d
  1.4453 / 2.1524 / 2.9515 / 3.8440 at child gain 0.35 / 0.50 / 0.70 / 1.00 (bar 4.0; every level arm and the
  44.1 kHz arm pass; the −6 dB re-reads match to 3 dp); Bloom Colony E1 0.1567 / 0.2636 / 0.4448 / 0.7528
  (attribBase 0.1204 → 0.5982); the latched parent's drop ≤ 0.20 dB at 1.00; the cloud floor's CPU at r 0.40
  is inside run-to-run noise (`cpu_floor_{base,l1}{,_run2}.log`). The component clamp at 1.00 does not clear
  S6. Children per event cannot add level: the probe shows all six child slots full at every richness
  (`bloom_mechanism_l1.log`: live 6, rejected 14). → **Ruled: raise the ceiling.** Append-only
  `BloomEngine::kMaxChildGain = 2.0f` (FR-019), the clamp becomes [0, 2]; the ladder continues at 1.2 / 1.5 /
  2.0 and the value is ruled on those readings (B-4). [FR-010, FR-011, FR-019]
- **B-4 — Bloom child gain, the value (FR-010b step 1, T027).** With the ceiling at 2.0: S6 d 4.3091 at 1.20,
  4.8405 at 1.50, 5.4501 at 2.00 (all arms pass, −6 dB re-reads match; `l1_bloom_{1.20,1.50,2.00}_S6*.log`);
  E1 0.9469 / 1.1989 / 1.4978; latched-parent drop 0.29 / 0.44 / 0.75 dB (`l1_sentinel_*.log`). → **Ruled:
  `VoragoVoice::kBloomChildGain = 1.5f`** (margin 0.84 over the bar for the sweep's seeded takes and twins; a
  child 3.5 dB above its parent before tilt; parent drop 0.44 dB). Compiled in and re-read with no lever env
  (`l1_ruled_S6.log`, `l1_ruled_E1.log`, `l1_sentinel_ruled.log`, `l1_ruled_suite.log`). [FR-010, FR-011]
- **B-5 — Route levers (FR-012, T031 / T032; 2026-10-02).** The L2 ladder (`l2_ladder_summary.txt`, 13 rungs,
  one rebuild each, 23:45–05:24) cleared nothing: E1 Bloom Colony d(R_k, R_k0) 1.1989 at `kPartialLaneGain`
  1.0 / 1.5 / 2.0 / 3.0 (identical: the Partial lane already saturates bloom depth and mutation at 1.0; at
  2.0 it pins Slow Bloom's ablation arm, S6 reads 0.0000); E3 Swarm Breath 0.6496 → 1.3495 → 1.7309 at
  `kNoiseLevelLeverSpanDb` 12 / 15 / 18 (the +12 dB clamp; S6 falls to 3.8912 at 18); E4 Feeding Loops
  1.7688 at `kLoopGainLeverSpan` 0.24 / 0.30 and `kCouplingLeverSpan` 0.38 / 0.44 (renders identical to 4 dp);
  E5 Haunted Colony 1.5818 / 1.5882 at `kGhostLaneGain` 1.5 / 2.0. The swing probe
  (`Vorago_PresetPilot_SwingProbe`, `l2_swing.log`: d(P, P_swing) with the destination at the lever maximum)
  bounds every route: Bloom Colony bloom depth 0.5 → 1.0 + mutation 0.85 → 1.0 d 0.0000; Feeding Loops loop
  gain 0.54 → 0.84 / 0.90 d 0.0004 / 0.0006; Swarm Breath noise −6 → +6 dB d 1.0906; Haunted Colony ghost
  layer 1.0 → 0 d 1.1010. No lane size can clear 4.0 while the destinations move the preset that little.
  → **Ruled: the five route constants stay at their shipped values** (`kPartialLaneGain` 1.0,
  `kGhostLaneGain` 1.0, `kNoiseLevelLeverSpanDb` 12, `kLoopGainLeverSpan` 0.18, `kCouplingLeverSpan` 0.30;
  the T029 re-spec is not applied). The destinations are lifted first — L3 ghost (E5's) and L4 feedback
  ecology (E4's) — and E1 / E3 / E4 / E5 with E7.hi, D13.1 and D13.2 are re-read after L4 (T068). P3's
  partial-lane destination extension (bloom spawn rate and child gain) is **adopted** for E1 and laddered
  after that re-read (T069). An E cell still short of 4.0 while its destination's full swing scores ~1.1 is
  surfaced as a bar question (FR-027), never relaxed silently. [FR-012, FR-021, FR-027]
- **B-6 — Ghost lever (FR-013, T035 / T036; 2026-10-02).** Lever-seam ladder (`l3_ghost_<dB>_{S9,E5}{,_minus6}.log`,
  `l3_density_<n>_*.log`, `l3_ghost_level_probe.log`). Make-up at density 0.30: Choir of Absence S9 d 1.8349
  (12 dB) → 2.4284 → 3.2996 → 4.1022 (21 dB) → 4.6989 (24); Haunted Colony E5 1.1920 → 1.6729 → 2.2776 →
  2.2835 → 2.3183 (saturates). At 21 dB the Choir's stored take sits on the limiter (hi −5.99 dB) and its −6 dB
  re-read is 4.0381 with every arm green; at 24 dB it clips at 44.1 kHz even at −6 dB. Density at 21 dB: E5
  2.0830 / 2.4587 / 3.6273 / 4.0218 / 5.6118 / 2.5635 / 4.1135 / 2.4543 at 0.45 / 0.60 / 1.0 / 1.25 / 1.5 / 1.75 /
  2.0 / 3.0 (not monotonic); S9 4.06–4.24 throughout. At 21 dB + 1.5 the four takes read S9 4.2370 / 4.2970 /
  5.0490 / 4.6607 and E5 5.6118 / 7.7136 / 3.5904 / 2.1762. Sizing at 21 dB (ghost 1.0, 340 s, `l3_ghost_level_probe.log`):
  tap RMS −16.99 dBFS against the drone's −19.03, loudest second −12.31 dBFS, 120 of 120 seconds sounding.
  → **Ruled: `kGhostTapMakeupDb = 21`** (`kGhostTapMakeupGain = 11.220185`); **`kGhostDensity` stays 0.30**, a
  ghost stays an event at rest; the Choir takes a level trim at the Phase 14 re-author. **The ghost route gains
  density as its second destination** (T070): the colony's raw Ghost eco lane drives the applied density
  `ghostDensityBase_ + kGhostDensitySpan × max(lane)` over the rendering voices — exactly the base at lane 0
  (13b FR-017), scheduler-blind (13b FR-019), the `setGhostDensity` seam now setting the base. The span is
  laddered by rebuild (0.7 / 1.2 / 1.7, i.e. density 1.0 / 1.5 / 2.0 at lane 1) and ruled on E5 with S9 beside
  it; T033's re-spec is not applied. [FR-013, FR-019]
- **B-7 — Ghost density span, the value (T070; 2026-10-02).** Rebuild ladder at the compiled 21 dB
  (`l3_span_ladder_summary.txt`, `l3_span_<v>_{E5,S9}{,_minus6}.log`): Haunted Colony E5 stored seed 2.7928 /
  3.7997 / 3.6205 at span 0.7 / 1.2 / 1.7 (all attributable, arms green); Choir of Absence S9 4.0743 / 4.1357 /
  4.2435. Four takes at 1.2 (`l3_span_1.2_{E5,S9}_takes4.log`): E5 3.7997 / 6.0424 / 3.0041 / 4.6243 (mean
  4.37), S9 4.1357 / 4.3961 / 5.0019 / 4.7054. The spread is the ghost grains' own randomness, so no span clears
  the stored seed with margin while another seed clears it by 2. → **Ruled: `kGhostDensitySpan = 1.2`**
  (density 1.5 at full lane, 0.30 at rest); **Haunted Colony re-seeds at the Phase 14 re-author** (seed index
  13 reads 6.04, 15 reads 4.62 at this span), recorded with the four-take spread. Choir of Absence takes a level
  trim there (its stored take sits on the limiter at 21 dB). Re-read as compiled: `l3_ruled_S9.log`,
  `l3_ruled_E5.log`, `l3_ruled_verify.log`, `l3_ruled_suite.log`. [FR-013, FR-021]
- **B-8 — Feedback-ecology wet ceiling (FR-017, T039 stop-and-surface; 2026-10-02).** Lever-seam ladder on
  Feedback Mire S4 (`l4_ecology_<dB>{,_minus6}.log`): d 1.7072 (0 dB) → 2.8171 (6) → 2.8372 (12) → 2.9627 (18)
  → 3.6021 (24, the component ceiling), every arm green, −6 dB re-reads identical; Feeding Loops E4 1.65–1.77
  throughout (`l4_ecology_<dB>_E4.log`). → **Ruled: raise the ceiling.** Append-only
  `FeedbackEcology::kMaxWetGainDb` 24 → 36 (FR-019; `FeedbackEcology_ConstantsTable` and
  `_ControlSurfaceClamps` pin the new value, `l4_ceiling_clamp_test.log`); the ladder continues at 30 / 36.
  [FR-017, FR-019]
- **B-9 — Ecology wet make-up, the value (FR-010b step 4; 2026-10-02).** With the ceiling at 36: S4 4.6618 (30 dB)
  / 5.7175 (36 dB), −6 dB re-reads 4.6626 / 5.7183, arms green; E4 2.9243 / 2.6508. At 36 dB the ecology
  dominates Feedback Mire (quiet section −40 → −28 dB, late sustain +4.6 → −0.4 dB). → **Ruled:
  `VoragoVoice::kEcologyWetMakeupDb = 30`** (margin 0.66 on a deterministic layer; Feedback Mire keeps its
  dynamics). Re-read as compiled with the R step: `l4_ruled_{S4,S6,S9,E5,E4,E1}.log`, `l4_ruled_suite.log`.
  [FR-017, FR-024c]
- **B-10 — Attack lever (FR-016, T046 / T047; 2026-10-02).** Rebuild-per-rung ladder of `kAttackShapePower`
  on the boundary-fixed Linear stage 0 (`l5_attack_n<k>.log`, `l5_attack_tests_n<k>.log`,
  `l5_attack_ladder_summary.txt`), Sudden Chasm D9.1 alone, no lever env; bar 4.0, attributable iff
  d_att ≥ d_Sus + 1.5; the probe's P_rev reach against [15, 20] s. Before (Exponential): reach 6 s, d_att
  2.7661. n = 1: reach 8 s, 4.2491 / 4.3288 (not attributable; T042 red at 8.0 s). n = 2: 13 s, 5.9340 /
  4.3308 (clears by 0.10, reach short of the floor). **n = 4: 17 s, 8.1243 / 4.3329.** n = 5: 19 s,
  10.1966 / 4.3338. n = 6: 20 s, 9.1264 / 4.3377 (on T0). n = 7: 21 s, past the capture (W_end 25 s), no
  reading. Option (a) scratch (Exponential stage 0 at 40 s, prediction written first: reach 6 → ~11 s;
  `l5_attack_optionA.log`): reach 13 s, d_att 4.5618 against d_Sus 5.3442 — FAIL, the comparator moves with
  the lever as FR-016 predicted; reverted, never shipped. Growth Ring D8.2 host at n = 6: 7.3957
  (`l5_d82.log`). → **Ruled: `VoragoVoice::kAttackShapePower = 4`** (plan §2.5's rule: the smallest rung
  that clears both clauses, 2 s above the floor, 3 s under T0); **the 0.75 · T0 tracking floor is confirmed**
  (plan §10.5 item 5). R step as compiled (`l5_ruled_*.log`): D9.1 8.1243 (W_end 25 / 22), D8.2 7.6879
  (65 / 35), S6 4.1966, S9 4.1365, S4 4.6629, E1 1.1991, E3 1.0941, E4 2.9238, E5 3.7994; attack tests
  green (reach 19.0 s at 8 kHz); per-push systems 1421 / 1424 (`l5_ruled_suite.log`), the three reds the
  ledgered entry 3 fingerprint and the T040 A1 / A16 re-specs. [FR-016, SC-004, SC-019]
- **B-11 — E1 partial-lane destination extension, declined (FR-012, T069; 2026-10-02).** P3's extension (the
  raw Partial lane adds `kPartialSpawnSpanHz` to the bloom's spawn rate and `kPartialChildGainSpan` to its
  child gain, clamped at the component ceilings, pinned by `VoragoVoice_PartialLaneBloomExtension`) was built
  and laddered by rebuild on the B-10 tree (`l2x_ladder_summary.txt`, four rungs 15:22–17:23): Bloom Colony
  E1 d(R_k, R_k0) 0.9312 / 1.1421 / 0.9312 / 1.1421 at spawn 0.02 / 0.02 / 0.04 / 0.04 Hz × child gain
  0.25 / 0.5 / 0.25 / 0.5 (base 1.1991 at 0 / 0; the spawn axis moves nothing to 4 dp, the gain axis moves
  it below the base); Slow Bloom S6 4.1966 → 2.4953 / 2.4982 / 1.2710 / 1.2668 (the lane-driven spawns crowd
  the preset's own blooms out of the pool); E7.hi 1.05–1.26, D13.1 0.07, D13.2 0.24–0.36, Teeming S7
  6.74–7.60 verified. → **Ruled: declined and removed** — the spans, the bloom bases and the test are reverted;
  the voice seams are bare forwards again; the ladder stays on record. E1 joins E3, E4 and E5 on the
  FR-027 surfacing list with its swing (B-5: full destination swing d 0.0000) and these readings. Per-push
  systems on the reverted tree: 1421 / 1424 (the same three ledgered reds: entry 3 fingerprint, A1, A16) (`l2x_declined_suite.log`). [FR-012, FR-027, FR-030]
- **B-12 — Life modulator lanes (FR-017b, T050; 2026-10-02).** Rebuild ladder (`l6_ladder_summary.txt`,
  `l6_life_{breath,tidal}_<g>.log`, each host alone with the secondary readout). **Breathing**, Slow Bloom
  D14.1 (bar 1.5) with S6 and Drifting Strata's M4 beside it: gain 1.0 → 0.6556 / S6 4.1966 / M4 1.9490 (the
  base tree read 1.0687); 1.5 → 0.4820 / 3.7906 / 1.3462; 2.0 → 1.5908 verified / 3.5884 / 1.0393; 3.0 →
  2.6558 verified / 4.3820 / 0.9090. → **Ruled: `kBreathGravityLaneGain = 3.0`**, the only rung that
  verifies D14.1 and keeps Slow Bloom over its bar; the lane is published unclamped, so FR-034 entry 5
  (`VoragoVoice_LifeModulatorLanes`) is applied: extremes gain × 0.30, the gravity sum clamps. **Tidal**,
  Drifting Strata D14.2: 0.0052 / 0.0077 / 0.0101 / 0.0150 at gain 1.0 / 1.5 / 2.0 / 3.0 — a top-out by
  construction: the voice's tide rate was a literal 0.25 (no preset parameter; layer periods 267 / 378 / 462 s)
  and the lane is max(0, tide), so the fog hardly moves inside the 60 s window in either arm; 0.0028 even with
  the space mix overridden to 1.0 (`l6_tidal_spacemix_override.log`); the fog's static full swing is d 0.0878
  (`l6_premise.log`). The user ruled a **scope extension**: the tide rate becomes a lever
  (`VoragoVoice::kTidalRate`, inside the modulator's ≥ 30 s period floor), laddered by rebuild
  (`l6_tide_ladder_summary.txt`, `l6_tide_<r>.log`): 0.6 → 0.2621, 0.8 → 0.3131, 1.0 → 0.2341; 0.8 with
  the space mix at 1.0 → 0.1904 (`l6_tide_0.8_spacemix_override.log`). → **Ruled: `kTidalRate = 0.8`,
  `kTidalFogLaneGain` stays 1.0; D14.2 is surfaced under FR-027** with these readings — the cavern fog is
  the ceiling, no bar is relaxed, nothing is recorded UNMET. Premise bisect (`l6_premise.log`): Drifting
  Strata's M4 primary fell 3.0007 → 1.9490 across Groups 6–11 (3.0651 with the ghost make-up back at 12 dB,
  2.2905 with child gain 0.35, 1.9557 with ecology make-up 0, 3.4226 with all three) — a Group 13 / Phase 14
  item; the breath gain costs it a further point (0.9090). R step as compiled (`l6_ruled_*.log`): per-push
  systems 1424 / 1425 (the ledgered entry 3 fingerprint the only red) (`l6_ruled_suite.log`). [FR-017b, FR-027, FR-030, FR-034]
  - **S4 under the breath gain (2026-10-03).** The R step read Feedback Mire S4 3.9832 (from 4.6629). Bisect by
    rebuild (`l6_s4_bisect_summary.txt`): 3.9445 at breath 3.0 / tide 0.25, 4.6719 at breath 1.0 / tide 0.8 — the
    breath gain, not the tide. Feedback Mire sets no breathing depth and inherits the plugin default 0.30, so
    the gain pumps its resonance gravity by ±0.9. Gain 2.5 (`l6_life_breath_2.5.log`, `l6_s4_breath25.log`):
    D14.1 2.0911 verified but S6 3.9998 and S4 3.9729 — no help. With the preset's breathing depth at 0
    (`l6_s4_breath3_depth0_override.log`) S4 reads 4.7751. → **Ruled: gain 3.0 stays; Feedback Mire's def sets
    `kLifeBreathingDepthId` to 0** (`tools/vorago_preset_defs.h`, a Phase 14 preset edit pulled forward and
    handed over for the preset files); S4 re-read as compiled, no override: `l6_ruled_S4_defs.log`. The swell
    fallback stays unmeasured. [FR-017b, FR-030]
- **B-13 — Macro rows (FR-014, FR-015, T051–T054; 2026-10-03).** Rebuild ladders, each showcase preset alone,
  cumulative rungs from plan §2.7, `VoragoMacro_NeutralIsIdentity` green on every build
  (`l7_<macro>_r<k>.log`, `l7_T052_ladder_summary.txt`, `l7_M2_ladder_summary.txt`,
  `l7_gravity_ladder_summary.txt`, `l7_life_ladder_summary.txt`, `m5_cause.log`). **M3** Density, Crowded
  Dark: 4.1136 at the shipped rows on the 13c tree (the bloom lift carried it; base tree 3.92); noise +12 dB
  3.7086, + Density→EcologyMix 3.7124. → **Ruled: shipped rows.** **M9** Fog, Fogbound: cavern fog 0.70
  3.1581; + new Fog→SmearDecoherence (base 0.20, +0.40) 4.4115; + smear 0.80 4.4498. → **Ruled: rung 2**
  (`kNumRows` 51). **M12** Mass, Monolith: sub-tone offset +6 dB 5.7413; + Mass→BodyDamping 5.7430; + body
  resonance 0.29 5.7774. → **Ruled: rung 1.** **M5** Gravity, Stone Gravity, 4 takes: the cause table (A0 as
  stored, A1 OctaveLock off, A2 ResonanceGravity row off on a scratch rebuild, A3 breathing off, A4 neutral +
  breathing off) reads arm 3 between +0.04 and +6.35 dB on every arm and take — the +15.67 dB self-growth of
  the base tree is gone on the 13c tree as stored (A0 +1.12 dB; with the three lever seams back at their
  pre-13c values +10.49 dB, `l7_gravity_cause_seams.log`, so the seams carry about 5 dB and the attack / life
  changes the rest); FR-015's clause is met without a row change. Distance: 3.1864 as stored (takes 1.77 /
  2.57 / 0.85), + Gravity→CloudSpectralTiltDb −3 3.1393, + Gravity→ResonanceMix +0.25 3.1864; A2 shows the
  ResonanceGravity row carries most of it (0.8268 without it). → **Ruled: shipped rows; M5 surfaced (FR-027).**
  **M2** Age, Erosion: 2.7945 as stored; tilt −7 2.7865; + damping 0.70 2.7900; + Age→CloudMutation 2.8273.
  → **Ruled: shipped rows; surfaced.** **M4** Movement, Drifting Strata: 1.9490 at breath gain 1.0 and 0.9090
  at the ruled 3.0 (its Movement→BreathingDepth row now swings the gravity lane three times harder; the
  ghost make-up and child gain mask it, B-12 bisect 3.07 / 2.29); damper 0.60 0.8741, + Movement→TidalDepth
  0.8533, + Movement→CloudMutation 0.8599. → **Ruled: shipped rows; surfaced; Drifting Strata handed to
  Phase 14.** **M10** Life, Teeming, last: as compiled E7.hi 1.3429 / D13.1 0.8560 / D13.2 0.1942 / S7 10.1021
  verified, Gate 1 at Life max ratio 1.196; spawn amount to the 0.05 ceiling 1.2428 / 0.0816 / 0.2261 / 9.6161
  (1.809); + Life→BreathingDepth 0.8294 / 0.1976 / 0.2168 / 8.8314 (1.136). → **Ruled: shipped rows; E7.hi,
  D13.1, D13.2 surfaced (FR-027).** **SC-011 finding:** `VoragoMacro_SweepAxes` reads 12 failed assertions on
  the 13c tree at the shipped rows (`l7_macro_tests_restored.log`; the base tree failed 2): Darkness 6.70 →
  0.73 dB, Entropy 22.36 → −0.14, Pressure 4.26 → 2.60, Age, Fog and Movement rho under 0.9. Bisect by rebuild
  over the ghost make-up, breath gain, child gain and ecology make-up (`l7_sweepaxes_bisect_summary.txt`);
  ruled separately (B-14). [FR-014, FR-015, FR-027, SC-011]
- **B-14 — SC-011 regression: the default ghost peak (2026-10-04).** On the 13c tree at the shipped macro rows
  `VoragoMacro_SweepAxes` lost 10 of its 36 T004-passing assertions (`l7_macro_tests_restored.log`: Darkness
  6.70 → 0.73 dB, Entropy 22.36 → −0.14, Pressure 4.26 → 2.60, Age / Fog / Movement rho under 0.9). Four
  bisect rounds by rebuild (`l7_sweepaxes_bisect_summary.txt`, `l7_sweepaxes_<arm>.log`): ghost make-up 12 dB
  alone recovers 4; + density span 0 recovers 2 more; + tide 0.25 one more; + breath 1.0 / child gain 0.35 /
  ecology 0 the last three (the all-reverted arm returns the base tree's two reds exactly); the breath gain,
  child gain and ecology make-up alone change nothing. The cause is the default surface itself: the engine
  (`kGhostBurstPeak`) and the plugin parameter both defaulted the ghost peak to 0.60, which under the 21 dB
  make-up swamps every spectral axis the Phase 10 macros are measured on; 36 of 42 presets set their own peak
  (31 at ≤ 0.30). Lowering only the default: 0.20 → 5 reds (3 binding), 0.10 → 1 (Movement endpoint 0.173),
  **0.0 → 1 red, the base tree's own Movement rho; every T004-passing assertion green.** → **Ruled:
  `VoragoEngine::kGhostDefaultPeakLevel = 0.0`** for the prepared level and the Fog→GhostPeakLevel row base,
  the plugin default (`ghost_params.h`) 0.0; `kGhostBurstPeak` stays 0.60 so the SC-027 trigger thresholds
  do not move; the six presets that inherited the default (Tectonic Floor, Drifting Strata, Entropic Hum,
  Teeming, Endless Descent, Steam Vent) pin `kGhostPeakLevelId` 0.60 in their defs, so no preset render
  moves (handed to Phase 14). Re-specs (FR-034): `VoragoEngine_GhostConfiguration` reads the default from
  `kGhostDefaultPeakLevel`; `ghost_params_test` expects 0.0. As compiled: SC-011 61 / 62, the one red the base tree's own Movement rho; every T004-passing assertion green
  (`l7_sc011_ruled.log`); per-push systems 1422 / 1425 before the fixture re-specs (reds: entry 3 fingerprint; the two gate-open fixtures, re-specced in l7_b14_respecs.log) (`l7_b14_suite.log`); per-push vorago lane 117 / 119 before the two default re-specs (param_table, ghost_params), green after (l7_b14_respecs.log)
  (`l7_b14_vorago_lane.log`); factory tree regenerated, only Machines/Feedback Mire.vstpreset changed (B-12); preset lane 41 / 41 (l7_b14_preset_lane.log). Declined: re-specifying SweepAxes with the ghost off in
  its fixture (the product default would keep a ghost 31 presets voice far lower) and re-opening B-6 (S9 4.10 →
  1.83). [SC-011, FR-031, FR-034]
- **B-15 — FR-030: verified cells masked by the 13c lifts (2026-10-05).** F2 (`final_verified27.log`) read 21 of 27
  sweep-4 primaries PASS; six fell with arms green: S1 Wind Through Basalt 3.0600, S2 Resonant Shaft 2.6851, S3
  Smeared Horizon 3.2882, M1 Lightless 1.7830, E2 Singing Colony 3.7914, D3.3 Spore Drift 1.8135, and with them the
  FR-030b secondaries whose conjunct is one of those primaries, plus E6.hi 1.3700 and S1-as-secondary 1.4403
  (`final_secondaries_030b.log`). Bisect (`f2_regress_bisect_summary.txt`): child gain 0.35 alone restores S3 4.56
  and E2 4.50 and lifts M1 to 3.79; ghost 12 dB lifts S1 to 3.96; ecology 0 changes nothing; with breath gain 1.0
  and all three seams reverted S1 / M1 / D3.3 / S2 read 4.97 / 4.06 / 4.10 / 4.04; the attack power is not a cause.
  Per-preset overrides (`f2_preset_fix_summary.txt`): breathing 0 fixes S1 (4.9774); bloom off fixes S3 (4.0128);
  nothing reaches S2 (best 3.29), M1 (2.56), E2 (3.79) or D3.3 (3.98). Child gain vs both sides
  (`f2_childgain_summary.txt`): Slow Bloom S6 3.93 / 3.35 / 2.74 at 1.2 / 1.0 / 0.8 while the masked cells move
  little; no value serves both. → **Ruled: the levers stay; Wind Through Basalt sets breathing depth 0 and Smeared
  Horizon sets bloom depth and spawn rate 0 in their defs** (as compiled: `final_b15_S1.log` 4.9774,
  `final_b15_S3.log` 4.0128, secondaries verified; tree regenerated, preset lane 41 / 41 `b15_preset_lane.log`);
  **S2, M1, E2 and D3.3 are surfaced (FR-027) and handed to the Phase 14 re-author**, which re-voices them against
  the 13c engine. Not adopted as is in the FR-030 sense: the cost is recorded, not hidden. [FR-030, FR-030b, FR-027]
- **B-16 — SC-012b at Life max (2026-10-05).** `gate2_table_lifemax.log`: syncRate d 1.8915 (d/t0 0.413) and
  selfAffinity 0.0163, both INAUDIBLE; the default surface passes (syncRate counted, d/t0 0.876,
  `gate2_table_default.log`; GATE2 PASS). The Life-max t0 rose to 4.5842 (base 4.1311) because the lifted levers
  louden the true-off twin. → **Ruled: surfaced under FR-027**; no further 13c ladder; handed to the next
  ecosystem-audibility pass. [SC-012b, FR-032, FR-027]
- **B-17 — `FeedbackEcology_CpuBudget` on the T060 lane (2026-10-05).** Red in the full CPU lane
  (`cpu_final_dsp.log`: 180130 vs 160000) and again alone, pinned, after a 10-minute idle
  (`cpu_rerun_FeedbackEcology_CpuBudget.log`: nsRef 110572.8, x1.5 = 165859.2). The interleaved A/B, alone, pinned,
  after a 15-minute idle (`cpu_ab_rerun_summary.txt`): base `64f57e1a` nsRef 119395.6 (x1.5 = 179093.4, red) and
  final 111763.6 (x1.5 = 167645.4, red). The base binary misses by more than the final. The 13c diff to
  `feedback_ecology.h` is the `kMaxWetGainDb` constant only (B-8), so the timed path is unchanged. The test's
  pre-authorised levers 1-3 are already in the tree; levers 4 (resonator → SVF bandpass) and 5 (numLoops 6 → 5)
  are user decisions. → **Ruled: surfaced, no DSP change in 13c.** Re-run once in T062's CPU lane and handed to
  Phase 14 as an open SC-004 (Phase 5) item. The same A/B cleared `Vorago_ProcessorCpu`: final P/D 0.9757
  (green), base 1.0581 (red), so its earlier red was the machine. [SC-014, FR-033]
- **B-18 — Choir of Absence arm 1 on the sweep (2026-10-06).** The T062 sweep on the 13c tree
  (`final_long_vorago_sweep_shard_0.log`, `final_sweep_record_diff.txt`): `Vorago_PresetSweep_LongRender` take 3
  arm 1 hi −5.58 dB against the −6.0 dB runaway bar (sweep 4: −7.85); the F1 all-take readout shows the same take
  (`final_S9.log:13`); the stored take's arms are green and the S9 gate passes (4.3383). The preset's S9 went 0.34 →
  4.34 under the ghost lift. First ruled "trim and re-measure": `kMasterGainId` 0.42 (−1.5 dB) moved take 3 only to
  −5.66 dB (`b18_choir_longrender.log`; every take peaks at 0.9661, so the output limiter sets the hi RMS, not the
  master gain) with S9 4.4473 (`b18_choir_s9.log`). The ghost-peak ladder at master unity (`b18_ghostpeak_{0.8,0.7,
  0.6}.log`, `b18_ladder_summary.txt`): 0.8 → S9 3.8746 FAIL, take 3 −5.86; 0.7 → 3.5742, −6.02; 0.6 → 3.2135, −6.19 —
  lowering the ghost loses the gate before it clears the arm. The one measured state passing both is master −6 dB
  (`final_minus6_S9.log:9,14`: S9 4.4586, take 3 −6.16, 0.16 dB inside the bar). → **Ruled: the trim is reverted
  (the def is the sweep-4 state) and the take-3 arm-1 red is surfaced to Phase 14** with these readings. [FR-024,
  FR-027, SC-002, SC-020]
- **B-19 — E3, E4, E5 under FR-027 (2026-10-06).** On the final tree, as compiled (F1): E3 Swarm Breath 1.1652
  (attrib 0.6422; `final_E3.log:9`), E4 Feeding Loops 2.9182 (attrib 0.9851; `final_E4.log:9`), E5 Haunted Colony
  3.9093 (attrib 0.8873; `final_E5.log:9`). The L2 size ladder (`l2_ladder_summary.txt`: noise span 15 / 18 dB
  1.35 / 1.73; loop 0.24 / 0.30 and coupling 0.38 / 0.44 all 1.77; ghost lane 1.5 / 2.0 1.58 / 1.59), the L3 density
  span (0.7 / 1.2 / 1.7: 2.79 / 3.80 / 3.62) and the L4 ecology lift (E4 2.92) brought none of them to 4.0.
  → **Ruled: surfaced, all three, no further 13c ladder**; handed to Phase 14 with E1 (B-11). [FR-012, FR-021, FR-027]
- **B-20 — FR-030b de-verified secondaries (2026-10-06).** `final_secondaries_030b.log`: E6.hi Colony Pulse 1.3700
  (:12; at sweep 4 its side predicate was false, so it was never rendered — the restored Gate 2 exposed it), Spore
  Drift D3.3 1.4396 conjunct FAIL (:63), Erosion D4.4–D4.6 conjunct FAIL (:64–66; S1 copy 1.44), Fogbound D4.8 /
  D4.10–D4.12 via S1 1.4403 (:80–84), Cathedral Void D10.1 conjunct FAIL (:201; the sweep aggregate still marks it
  S, `final_long_vorago_sweep_aggregate.log:164`). The B-15 bisect names the same three levers.
  → **Ruled: the levers stay; the five are surfaced to Phase 14** with their sweep-4 and final values; no preset or
  DSP change in 13c. [FR-030b, SC-007b, FR-027]
