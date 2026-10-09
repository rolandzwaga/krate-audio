# Implementation Plan: Vorago Phase 12 — Full Parameter Surface & State

**Spec:** `specs/vorago-phase12-parameters/spec.md` (reviewed; Clarifications Q1–Q8 ruled 2026-09-24)
**Roadmap:** `specs/Vorago-roadmap.md` Part B → Phase 12 (lines 550–556); OQ-6/OQ-7 (lines 638–642);
Phase 10 status block (lines 447–455); Cross-Cutting Constraints (lines 598–622)
**Template:** Seraphis Phase 9 (`specs/seraphis-phase9-parameters/`) — pattern only; no Seraphis file changes
**Branch / base:** `feat/vorago-phase1-events-modulation` at `f149cced`
**Status:** PLAN — no code written. Every file:line below was read in this planning session.

---

## 0. What this plan decides (read first)

1. **DSP (Layer 3, three Vorago-only headers, additive):** a per-target base override on
   `VoragoMacroMatrix`; a trivially-copyable `VoragoVoiceParams` POD + `applyVoiceParams()` on
   `VoragoEngine`; seven `VoragoVoice` forwarders with early-outs and two per-slot shadows; per-tone sub
   level bases and two ghost setters on the engine that survive re-prepare. No algorithm is written.
2. **FR-060 (`kRows` retune):** Mass gets one concrete new row (`Mass → SubToneLevelOffsetDb`, +3 dB,
   Linear). Gravity and Pressure are **predicted unreachable through the 39 existing targets** by the
   Phase 10 measurements themselves (§2.6; `vorago_macro_test.cpp:1306-1315` records the stone-end floor
   0.209, the air-end best 0.266, saturation at its ceiling moving crest only 0.4 dB, and names the levers —
   ratio table, a saturator drive row — neither admissible under ruling (b), FR-007 or `Count == 39`).
   **SC-021 is therefore BLOCKED ON A SPEC AMENDMENT for the Gravity and Pressure axes — not merely
   "open":** no design element of this plan can satisfy FR-060's "Gravity MUST gain a new row" together
   with SC-021's "all twelve axes pass" as the spec currently encodes ruling (b). The §2.6 measurement
   probe (`[.probe]`, driven through the new base-override API; needs only D-1) is **prerequisite gate
   P-0**: it runs **before tasks.md generates any Gravity/Pressure SC-021 closure task**, and its measured
   table goes to the user so the spec's Q1 / FR-060 / SC-021 can be amended to the remedy the user picks
   (§8 OQ-1). If the probe instead finds an admissible row set, the block lifts and that set lands as §2.6
   step 3 specifies. Every other Phase 12 deliverable (including Mass) is independent of this outcome.
3. **Plugin:** 94 new IDs (108 total, 106 persisted) in 14 new pack headers + the extended global pack;
   one checked-in route table (`routeOf`) with compile-time completeness; on-change push cadence with a
   VP generation counter and per-field trackers; `pushAllSurfaces()` from `setupProcessing()` and (by
   release-store request) from `setState()`; state v2 = strict extension of v1 (428 bytes);
   `Vorago::SustainLatch`; 16-entry seed table; `IMidiMapping` (CC64, channel pressure).
4. **Tapers:** exact mapping functions and every ε are stated in §3.3 (Q4 ruled (b)). One spec gap is
   closed here: ID 1114 (cavern damper rate) is a zero-floored `[0, 1]` "rate" that C-6 marks `log`
   without an ε — the plan gives it the offset-log form (ε = 0.01) and records it (§8).

---

## 1. Verified facts this plan builds on (read this session)

### 1.1 The matrix (`dsp/include/krate/dsp/systems/vorago_macro_matrix.h`)

| Fact | Where |
|---|---|
| `VoragoMacroTarget`: 24 Voice (`CloudRichness` … `TidalDepth`), 8 Engine (`SubToneLevelOffsetDb` … `OutputSaturation`), 7 Cavern (`CavernSize` … `CavernWidth`), `Count` | `:123-167` |
| `VoragoCavernTargets` defaults size 0.50, darkness 0.80, decay 20.0, fog 0.30, damperDepth 0.35, mix 1.00, width 1.00 | `:182-190` |
| `VoragoMacroValues` neutrals: all 0 except `gravity = 0.5f` | `:198-211` |
| `struct VoragoMacroRow { macro, owner, target, float base, float amount, ModCurve curve }` | `:214-237` |
| `kNumRows = 46` (comment formula `3+3+4+5+1+5+3+4+7+3+4+4`) | `:254-257` |
| `kRows` table; Gravity = ONE row (`ResonanceGravity`, base 0, amount +1.0, Linear) | `:296`, `:420-425` |
| Pressure rows: `EcologyMix` +0.35 SCurve, `EcologyLoopGain` +0.16, `OutputSaturation` +0.35 | `:464-481` |
| Mass rows: `BodyResonance` +0.28, `BodyMix` 0 (claim), `ResonanceMix` −0.30 SCurve, `SubTrackingAmount` 0 (claim) | `:652-692` |
| Weight row `SubToneLevelOffsetDb` base 0.0, amount +9.0 | `:487-492` |
| Header banner "NOT PROVIDED, DELIBERATELY: setTargetBase / resetTargetBases / getTargetBase" | `:58-62` |
| `setMacro` rejects non-finite via `isFiniteBits` (→ `detail::isFinite`), clamps [0,1] | `:832-878`, `:1044` |
| `setMacros(const VoragoMacroValues&)` — one `setMacro` per knob | `:915-928` |
| `getMacros()` | `:930` |
| `apply(VoragoEngine&) const` — returns on `!engine.isPrepared()`; 8 Engine setters; voice loop `i < engine.getPolyphony()` via `engine.voices_[i]` | `:954-1013` |
| `computeCavernTargets() const` | `:1026-1037` |
| `contributionOf` (Gravity bipolar: `amount * curve(|g|) * sign(g)`, `g = (m − 0.5)·2`) | `:1057-1065` |
| `evaluateAll()` seeds `value[i] = row.base` once per target, adds each contribution | `:1074-1086` |
| `values_` is the only state | `:1089` |
| six `static_assert`s incl. `everyRowSharesOneBasePerTarget`, `Count == 39`, `kNumMacros == 12` | `:1097-1117` |

### 1.2 The engine (`dsp/include/krate/dsp/systems/vorago_engine.h`)

| Fact | Where |
|---|---|
| `VoragoEngineConfig`: `atmosGhostReverseProbability = 0.0f`, `atmosGhostEventTriggers = false` | `:131-132` |
| `kMaxVoices = 6`, `kDefaultPolyphony = 4`, `kControlChunkSamples = 64`, `kMaxBlockSamples = 2048` | `:175-182` |
| `kOutputSaturation = 0.12f`, `kOutputDriveDb = 0.0f`, `kOutputCeilingDb = -0.3f` | `:201-203` |
| `kGhostBurstPeak = 0.60f`, `kGhostTriggerRise/Fall` | `:213-219` |
| `prepare()` step 2 writes `ghostEventTriggers_ = cfg.atmosGhostEventTriggers; ghostTriggerHigh_ = false;` | `:290-291` |
| `prepare()` step 3 re-prepares ALL `kMaxVoices` (voice step 5 re-installs every voice default) | `:294-297` |
| `atmos_.setGrainReverseProbability(cfg.atmosGhostReverseProbability)` in prepare | `:323` |
| `sub_.prepare(...); applySubToneLevels(); sub_.setTrackingAmount(subTracking_); sub_.reset();` | `:348-351` |
| `silence()` is NOT an audio-thread operation | `:437-456` |
| `setPolyphony(n)` clamps [1, kMaxVoices] | `:507-521` |
| `setSeed(seed)` re-derives every slot seed + atmos seed | `:544-550` |
| Envelope fan-outs over all `kMaxVoices`; slot-0 getters | `:719-756` |
| `setSubToneLevelOffsetDb` — rejects non-finite, **early-out on unchanged** (ramp re-arm reason), NO clamp | `:764-780` |
| `setSubTrackingAmount` clamp [0,1]; `setSmearAmount` writes `smearBase_` [0,1]; `setSmearDecoherence` [0,1]; `setSmearTilt` [-1,1]; `setGhostPeakLevel` [0,1]; `setAtmosBlur` [0,1]; `setOutputSaturation` [0,1] | `:783-858` |
| `getVoiceState(i)`, const `getVoice(i)`, `getNonFiniteRecoveryCount()` | `:1065-1083` |
| const `atmosphere()`, `subharmonic()`, `smear()` | `:1085-1087` |
| `friend class VoragoMacroMatrix` | `:1090` |
| `applySubToneLevels()` writes `kDefaultToneLevelDb[t] + subToneOffsetDb_` | `:1154-1158` |
| Ghost trigger block: `if (ghostEventTriggers_) { gated = ghostPeak_ * ghost; rise → triggerGrain(); fall → disarm }` | `:1316-1324` |
| Fields `ghostEventTriggers_`, `ghostTriggerHigh_`, `subToneOffsetDb_`, `atmosBlur_`, `outputSaturation_` | `:1568-1574` |

### 1.3 The voice (`dsp/include/krate/dsp/systems/vorago_voice.h`)

| Fact | Where |
|---|---|
| `VoragoVoiceConfig`: `numNoiseSources = 4`, `numResonancePeaks = 12`, `numEcologyLoops = 6`, `maxCombDelayMs = 50` | `:187-212` |
| Envelope constants: `kDefaultStageTimesMs{20000,30000,45000,60000,0,0}`, `kDefaultReleaseMs 45000`, `kEnvelopeMaxStageTimeMs 120000`, `kDefaultGrowthDurationSeconds = kGrowthMaxDurationSeconds = 120` | `:322-333` |
| Prepare step 5: richness 0.70, tilt −4, mutation 0.15, inharm 0.015, `cloud_.setSpectralGravity(0.10f)` ("NOT a Gravity-macro target (Q4)"), drift 8, **stereo spread 0.45** | `:545-551` |
| Noise models slots 0–3 `FilteredWind, GranularDust, Direct, MetallicHiss`; level −18; wander 0.03; wake 0.35 | `:557-564` |
| `resonance_.setAnchorMode(Hybrid)`; per-peak level −9, `setFreqWander(p, 1.5f)`; gravity 0; mix 0.45; wander 0.03 | `:567-577` |
| Ecology mix/loop gain = component defaults; coupling 0.12 ring | `:581-591` |
| Bodies `StoneChamber` / `SteelTank`; resonance 0.70; damping 0.25; mix 1.00; blend 0.35 | `:621-632` |
| Envelope re-authored on every prepare (shadows invalidated, stages re-applied) | `:654-683` |
| `setSeed` — on a prepared voice runs `applySeeds(); assignAgentSlots();` immediately | `:961-967` |
| `setEnvelopeMode` early-outs; `setEnvelopeStageTimeMs` rejects non-finite → `applyStage`; `setEnvelopeReleaseMs`; `setGrowthDurationSeconds` → `growth_.setDuration` clamps `[1, kGrowthMaxDurationSeconds]` | `:1025-1064` |
| Forwarder rule: owner clamps; a non-finite guard is added only where the owner substitutes a default | `:1087-1109` |
| `setStereoSpread(float)` bare forward | `:1144` |
| `setNoiseLevelDb` early-out comparing to the owner's clamp `[NoiseGenerator::kMinLevelDb, kMaxLevelDb]` | `:1148-1169` |
| `setEcologyMix` early-out; `setBodyBlend` early-out; `setBodyDamping/Resonance/Mix` guards | `:1216-1282` |
| `setBodyMaterialA/B(ContinuousBody::BodyMaterial)`, getters | `:1284-1291` |
| `setEcosystemDepth` fills all four; `setEcosystemDepthFor` | `:1309-1342` |
| `setEventRateScale` clamp [0.1, 10] | `:1353-1358` |
| `setBloomDepth` early-out; `setBloomSpawnRateHz` bare | `:1363-1381` |
| Const accessors `cloud() noise() resonance() ecology() bloom() ecosystem() bodyA() bodyB()` | `:1415-1422` |

### 1.4 Components the forwarders reach

| Fact | Where |
|---|---|
| `HarmonicCloud`: richness [0,1], inharm [0, `kMaxInharmonicity` 0.1], tilt [`kMinTiltDbPerOct` −12, +12], mutation [0,1], `setSpectralGravity` [−1,1] (rejects NaN/Inf, early-outs on unchanged, phase-continuous), drift [0, `kMaxDriftCents` 50], stereo spread [0,1]; `getSpectralGravity()` | `harmonic_cloud.h:191-214, 412-494, 501-505, 535-539` |
| `NoiseGenerator::kMinLevelDb = −96`, `kMaxLevelDb = +12` | `noise_generator.h:104-105` |
| `NoiseType` 13 enumerators `White=0 … VinylRumble=10, ModulationNoise=11, RadioStatic=12` | `noise_generator.h:44-58` |
| `NoiseOrganism::setSourceModel`/`setSourceNoiseType` → `requestSourceState` which is a **full no-op on an unchanged goal** (no duck) | `noise_organism.h:463-485, 1703-1728` |
| `setCombTuning(slot, hz, spread)` sanitises each arg to a DEFAULT (60 / 0.35), clamps hz `[kMinResonatorFrequency, maxResonatorHz()]` | `noise_organism.h:568-577` |
| `setCombFeedback` sanitises to `kDefaultCombFeedback`, clamps `[0, kCombFeedbackCap 0.9]`, **sets `combFeedbackUserSet = true`** (latch); prepare clears it | `noise_organism.h:588-596, 1871`; constants `:165-170` |
| `effectiveNoiseType()`: FilteredWind→Brown, GranularDust→Velvet, MetallicHiss→Blue/Violet, Direct→requested (ModulationNoise→TapeHiss) | `noise_organism.h:1226-1240` |
| slot initialisers `requestedType = Brown`, `combFundamental = 60`, `combSpread = 0.35` | `noise_organism.h:1137-1147` |
| `NoiseOrganism::setWanderRate` clamps `[StochasticFilter::kMinChangeRate, kMaxChangeRate] = [0.01, 100]` Hz after sanitising to a default | `noise_organism.h:775-795` |
| `kMinResonatorFrequency = 20`, `kMaxResonatorFrequencyRatio = 0.45` | `resonator_bank.h:42, 45` |
| `ResonanceDriftNetwork::setAnchorMode` — no early-out (always marks anchors dirty); `setGravity` clamp [−1,1]; `setMix` [0,1]; `setWanderRate` `[kMinWanderRateHz 0.002, kMaxWanderRateHz 1.0]`; Hybrid anchor law `log2 f = free + g·(keyed − free)` | `resonance_drift_network.h:147-149, 587-590, 633-637, 715-717, 796-800, 1404-1452` |
| `FeedbackEcology::setLoopFilterMode` — STEPPED, validates enumerator, no early-out; `kMinLoopGain 0`, `kMaxLoopGain 0.90`; `setMix` [0,1] | `feedback_ecology.h:264-266, 1028-1045, 1111-1115, 1314-1318` |
| `ContinuousBody::BodyMaterial` 11 enumerators (`Glass=0 … Ice=4, StoneChamber=5, SteelTank=6, WoodenHull, CathedralColumn, CavernWall, GlassSphere=10`); resonance/damping/mix [0,1] with default substitution | `continuous_body.h:84-100, 142-159, 1440-1494` |
| `BloomEngine::kMaxSpawnRateHz = 0.05`, `kDefaultSpawnRateHz = 1/240`; `setSpawnRateHz` rejects non-finite, clamps [0, 0.05] | `bloom_engine.h:280-281, 543-547` |
| `MultiStageEnvelope::setStage`/`setReleaseTime` clamp `[0, maxStageTimeMs_]` | `multi_stage_envelope.h:166-170, 206-208` |
| `GrowthEnvelope::kMinDuration = 1.0` | `growth_envelope.h:97` |
| `BreathingModulator::setDepth/setIrregularity`, `TidalModulator::setDepth` clamp [0,1] | `breathing_modulator.h:177-185`, `tidal_modulator.h:209-210` |
| `CavernVerb` setters, each `clamp(isFinite(v) ? v : kDefault*, …)`: size/darkness/density/dim/breath/fog/early level/absorption/send/damper depth/damper rate/width/mix in [0,1]; decay `[0.5, 60]`; early size `[80, min(600, maxEarlySeconds·1000)]`; `setFreeze(bool)`; `setSeed` → `reseed()` (allocation-free: seeds + `damper_[i].reset()`) | `cavern_verb.h:207-265, 613-760, 953-959` |
| `CavernVerb::kDefault*`: density 0.75, dim 0.50, breath 0.50, early level 0.80, absorption 0.60, send 0.70, early size 220 ms, damper rate 0.15, `kDefaultMaxEarlySeconds 0.30` | `cavern_verb.h:221, 247-265` |
| `logMapFromNormalized(n, mn, mx) = clamp(mn·(mx/mn)^clamp(n,0,1), mn, mx)` and its inverse | `plugins/shared/src/ui/parameter_helpers.h:80-88` |
| `createDropdownParameterWithDefault` sets only the current value (P-1 trap) | `parameter_helpers.h:47-70` |
| `detail::isFinite(float)` and `detail::isFinite(double)` (fast-math-immune) | `dsp/include/krate/dsp/core/db_utils.h:118, 125` |

### 1.5 Plugin code being extended

| Fact | Where |
|---|---|
| `kCurrentStateVersion = 1`; reserved map comment; IDs 0, 1, 100–111; `kGlobalParamRangeEnd = 100`, `kMacroParamRangeEnd = 200` | `plugins/vorago/src/plugin_ids.h:20, 46-74` |
| `GlobalParams { masterGain{1.0f}; polyphony{4}; }`, six-function contract, 8-byte save | `parameters/global_params.h:31-159` |
| `MacroParams` twelve atomics (`gravity{0.5f}`), `macroField(p, i)`, 48-byte save | `parameters/macro_params.h:33-211` |
| `kEngineSeed = kCavernSeed = 1u`; `makeVoragoEngineConfig`, `makeVoragoCavernConfig` (sets `cfg.seed = kCavernSeed`); `applyCavernTargets` (7 setters) | `engine/vorago_engine_config.h:30-73` |
| `Processor` members: `engine_`, `cavern_` (unique_ptr), `macros_` by value, packs, `masterGain_`, `snapGainPending_`, `lastPushedPolyphony_`, `eventOrder_[1024]`; `static_assert(sizeof(Processor) < 64 KiB)` | `processor/processor.h:98-115` |
| `setupProcessing`: `setSeed(kEngineSeed)` → `prepare` ×2 → polyphony push → gain configure/snap arm | `processor/processor.cpp:118-146` |
| `setActive(false)` → `engine_->silence(); cavern_->reset()` | `processor.cpp:153-167` |
| `process()`: `processParameterChanges` → guards → `pushGlobalParams()` → `macros_.apply(*engine_)` → `applyCavernTargets(...)` → event-sliced loop | `processor.cpp:170-278` |
| `setState` (v ≤ 1, global then macros), `getState` | `processor.cpp:292-320` |
| `processParameterChanges` takes the LAST point per queue, dispatches by band | `processor.cpp:323-349` |
| `pushGlobalParams` (edge-triggered polyphony; gain snap/setTarget) | `processor.cpp:352-367` |
| `dispatchEvent` handles note on/off only; velocity 0 → noteOff | `processor.cpp:404-428` |
| Controller: `EditControllerEx1 + VST3EditorDelegate`, header comment "NO IMidiMapping" (`:9`); `initialize` registers 2 packs; `setComponentState` mirrors | `controller/controller.h:9, 25`, `controller.cpp:83-128` |
| `IMidiMapping` precedent: `DEFINE_INTERFACES … DEF_INTERFACE(Steinberg::Vst::IMidiMapping) … END_DEFINE_INTERFACES(EditController)`; bus-0 check | `plugins/disrumpo/src/controller/controller.h:218-224`, `controller.cpp:707-726` |
| Seraphis precedent: `setTargetBase`/`resetTargetBases`/`getTargetBase` with `hasOverride_`/`baseOverride_`; `evaluateAll` seeds `hasOverride_[i] ? baseOverride_[i] : row.base` | `seraphis_macro_matrix.h:872-900, 953, 970` |
| Seraphis `applyVoiceParams` loops `v < kMaxVoices` (orphan-tail reason) | `seraphis_engine.h:704-723` |
| Seraphis `routeOf` switch; force-push consumed after the not-ready path, above `pushGlobalParams()`; one release store in `setState` | `plugins/seraphis/src/processor/processor.cpp:152-215, 1359-1371, 1885-1890` |
| Seraphis curated seed table rationale (index 0 pinned to 1u; table re-picked, gate never lowered) | `plugins/seraphis/src/parameters/dropdown_mappings.h:53-93` |

### 1.6 Test infrastructure

| Fact | Where |
|---|---|
| `vorago_tests` source list is ENUMERATED; `-fno-fast-math` list for NaN-injecting TUs; `processor_cpu_test.cpp` deliberately excluded | `plugins/vorago/tests/CMakeLists.txt:7-89` |
| `ProcessorFixture` (`prepare`, `processBlock(n, ev, pc)`, `renderScript`, `capturedL/R`), `MultiParamChanges` / `MultiPointParamValueQueue`, `peakOf`, `rmsOf`, `maxAbsDiff`, `rmsDiff`, `allFinite` | `plugins/vorago/tests/vorago_test_fixture.h:55-390` |
| Phase 11 tests pinning 14 params / 60-byte v1 / version 2 = "future" | `unit/param_denorm_test.cpp:131, 225, 231`; `unit/state_roundtrip_test.cpp:217, 240-396` |
| Phase 11 `MacrosAreInert` section (SC-023) | `integration/param_flow_test.cpp:162-195` |
| Editor-lifecycle harness counts the placeholder uidesc's 14 controls (uidesc untouched → stays valid) | `unit/controller/editor_lifecycle_test.cpp:179-198` |
| `dsp_systems_tests` Vorago list + fast-math exclusions | `dsp/tests/CMakeLists.txt:492-536, ~900-1025` |
| SC-008 sweep: `kMacroNote 36` (C1), 3 seeds `{101, 202, 303}`, 5 points, 60 s at 48 kHz, window [10 s, 60 s], polyphony 1, fast-attack fixture, macros applied after `noteOn`; `meanOctaveOffset` reads `resonance().getPeakCurrentFrequency` | `dsp/tests/unit/systems/vorago_macro_test.cpp:544-649, 723-869, 1211-1330` |
| Shared fixtures `bandEnergyDb`, `crestFactorDb`, `spearmanRho`, `applyFastAttack`, `makeEngine`, `renderEngine` | `tests/test_helpers/vorago_fixtures.h:245-750` |
| `AllocationDetector` / `AllocationScope` | `tests/test_helpers/allocation_detector.h:48-180` |
| Phase 10 measured SC-008 failures: Gravity endpoint 0.18014 (≥ 0.3), Pressure rho −0.567 / 0.0736 dB (≥ 0.9 / 3 dB), Mass rho −1 / −0.446 dB (≥ 0.9 / 0.25) | `specs/vorago-phase10-voice-engine/compliance.md:20` |

---

## 2. DSP additions (Layer 3, `Krate::DSP`)

All three headers are Layer 3 and already include only Layers 0–3 (`lint-layers.js` unchanged). No new
include. Every new method is `noexcept`, allocation-, lock-, exception- and I/O-free.

### 2.1 D-1 `VoragoMacroMatrix` base override (FR-001, FR-002) — `systems/vorago_macro_matrix.h`

Replace the `:58-62` banner with the new contract paragraph (why the override exists, that
`everyRowSharesOneBasePerTarget` still guarantees one literal per target, NO HEADROOM RESCALING). Add:

```cpp
public:
    /// FR-001. Out-of-range target or non-finite base: silent no-op (isFiniteBits, never std::isnan).
    void setTargetBase(VoragoMacroTarget target, float base) noexcept {
        const auto i = static_cast<std::size_t>(target);
        if (i >= kNumTargets || !isFiniteBits(base)) { return; }
        baseOverride_[i] = base;
        hasOverride_[i] = true;
    }
    void resetTargetBases() noexcept { hasOverride_.fill(false); baseOverride_.fill(0.0f); }
    [[nodiscard]] float getTargetBase(VoragoMacroTarget target) const noexcept {
        const auto i = static_cast<std::size_t>(target);
        if (i >= kNumTargets) { return 0.0f; }
        return hasOverride_[i] ? baseOverride_[i] : literalBaseFor(target);
    }
private:
    /// The kRows base for `target` (first row on it; everyTargetIsClaimed guarantees one exists).
    [[nodiscard]] static constexpr float literalBaseFor(VoragoMacroTarget t) noexcept {
        for (const VoragoMacroRow& row : kRows) { if (row.target == t) { return row.base; } }
        return 0.0f;  // unreachable (static_assert'ed claim)
    }
    std::array<float, kNumTargets> baseOverride_{};
    std::array<bool,  kNumTargets> hasOverride_{};
```

`evaluateAll()` (`:1074-1086`) changes exactly one line:
`value[i] = hasOverride_[i] ? baseOverride_[i] : row.base;` (the Seraphis `:953` form). With no
override set this is the literal, so `apply()`/`computeCavernTargets()` are bit-identical (FR-002 — the
seeded expression is the same float, the accumulation order is unchanged). All six `static_assert`s stay.

**No clamping at the matrix**: the destination setter clamps the summed value (the header's own
"summation first, clamp at the destination" rule, `:952-953`). The plugin registers each MB parameter's
plain range as the destination's clamp (§3.2), so the override itself is always in-range.

**Semantics note (C-2, recorded):** the new state makes `getTargetBase` a run-time quantity; Phase 10's
SC-009 clause 1 is stated as holding **with no override set** (FR-002). `VoragoMacro_NeutralIsIdentity`
and the other `vorago_macro_test.cpp` cases never call the setter, so they are unaffected and stay
unedited (FR-007).

### 2.2 D-2 `VoragoVoiceParams` + `applyVoiceParams` (FR-003) — `systems/vorago_engine.h`

Declared after `VoragoEngineConfig` (`:141`). Every initializer is the voice's prepare-step-5 value
(§1.3); no field names a `VoragoMacroTarget` (the `seraphis_engine.h:110-112` rule, restated in the
doc-comment).

```cpp
struct VoragoVoiceParams {
    static constexpr std::size_t kNumNoiseSlots = NoiseOrganism::kMaxSources;  // 4
    static constexpr std::size_t kNumLoops      = FeedbackEcology::kMaxLoops;  // 6

    float stereoSpread         = 0.45f;  // vorago_voice.h:551
    float cloudSpectralGravity = 0.10f;  // :549
    ContinuousBody::BodyMaterial bodyMaterialA = ContinuousBody::BodyMaterial::StoneChamber;  // :621
    ContinuousBody::BodyMaterial bodyMaterialB = ContinuousBody::BodyMaterial::SteelTank;     // :622
    std::array<NoiseOrganismModel, kNumNoiseSlots> noiseModel{
        NoiseOrganismModel::FilteredWind, NoiseOrganismModel::GranularDust,
        NoiseOrganismModel::Direct,       NoiseOrganismModel::MetallicHiss};                    // :557-561
    std::array<NoiseType, kNumNoiseSlots> noiseType{
        NoiseType::Brown, NoiseType::Brown, NoiseType::Brown, NoiseType::Brown};             // noise_organism.h:1138
    std::array<float, kNumNoiseSlots> noiseCombFundamentalHz{60.0f, 60.0f, 60.0f, 60.0f};    // :1145
    std::array<float, kNumNoiseSlots> noiseCombSpread{0.35f, 0.35f, 0.35f, 0.35f};           // :1146
    std::array<float, kNumNoiseSlots> noiseCombFeedback{
        NoiseOrganism::kDefaultCombFeedback, NoiseOrganism::kDefaultCombFeedback,
        NoiseOrganism::kDefaultCombFeedback, NoiseOrganism::kMetallicCombFeedback};          // C-4
    ResonanceDriftNetwork::AnchorMode resonanceAnchorMode = ResonanceDriftNetwork::AnchorMode::Hybrid;
    std::array<FeedbackEcology::FilterMode, kNumLoops> ecologyLoopFilterMode{
        FeedbackEcology::FilterMode::Lowpass, /* ×6 */ };

    /// 2 + 2 + 4×5 + 1 + 6 = 31 scalar values (FR-003).
    static constexpr std::size_t kFieldCount = 31;
};
static_assert(std::is_trivially_copyable_v<VoragoVoiceParams>);
static_assert(VoragoVoiceParams::kNumNoiseSlots == 4 && VoragoVoiceParams::kNumLoops == 6);
```

```cpp
/// FR-003. THE BOUND IS kMaxVoices, NOT getPolyphony() (seraphis_engine.h:704-713 reason: orphan tails
/// keep rendering; a slot handed out after a polyphony increase must already be configured).
void applyVoiceParams(const VoragoVoiceParams& p) noexcept {
    for (std::size_t v = 0; v < kMaxVoices; ++v) {
        VoragoVoice& voice = voices_[v];
        voice.setStereoSpread(p.stereoSpread);                 // vorago_voice.h:1144
        voice.setCloudSpectralGravity(p.cloudSpectralGravity); // D-3
        voice.setBodyMaterialA(p.bodyMaterialA);               // :1284
        voice.setBodyMaterialB(p.bodyMaterialB);               // :1285
        for (std::size_t s = 0; s < VoragoVoiceParams::kNumNoiseSlots; ++s) {
            voice.setNoiseSourceModel(s, p.noiseModel[s]);
            voice.setNoiseSourceType(s, p.noiseType[s]);
            voice.setNoiseCombTuning(s, p.noiseCombFundamentalHz[s], p.noiseCombSpread[s]);
            voice.setNoiseCombFeedback(s, p.noiseCombFeedback[s]);
        }
        voice.setResonanceAnchorMode(p.resonanceAnchorMode);
        for (std::size_t l = 0; l < VoragoVoiceParams::kNumLoops; ++l) {
            voice.setEcologyLoopFilterMode(l, p.ecologyLoopFilterMode[l]);
        }
    }
}
```

On an unprepared engine this is harmless (each setter stores); `VoragoVoice::prepare()` re-installs
every default anyway (§1.3 `:538-683`), which is why the plugin re-pushes after every prepare (§4.6).

### 2.3 D-3 `VoragoVoice` forwarders (FR-004) — `systems/vorago_voice.h`

Placed in the macro-writable surface block after `setStereoSpread` (`:1144`) / before `// Sub-component
read access` (`:1411`). Early-out rule (FR-004 / SC-023): a repeated identical broadcast must arm no duck,
restart no ramp, and not re-mark anchors dirty.

| Forwarder | Body | Early-out / guard source |
|---|---|---|
| `void setCloudSpectralGravity(float g) noexcept` | `cloud_.setSpectralGravity(g);` | owner rejects NaN/Inf and early-outs on unchanged (`harmonic_cloud.h:476-490`) → bare forward |
| `void setNoiseSourceModel(std::size_t slot, NoiseOrganismModel m) noexcept` | `noise_.setSourceModel(slot, m);` | owner: invalid slot no-op, unchanged goal = full no-op, no duck (`noise_organism.h:1720-1728`) → bare forward |
| `void setNoiseSourceType(std::size_t slot, NoiseType t) noexcept` | `noise_.setSourceNoiseType(slot, t);` | same owner rule → bare forward |
| `void setNoiseCombTuning(std::size_t slot, float hz, float spread) noexcept` | guard + shadow compare, then `noise_.setCombTuning(slot, hz, spread);` | owner **substitutes defaults** on NaN (`:573-575`) → voice rejects if either arg non-finite (the `:1093-1107` rule); early-out if `combTuningSet_[slot] && hz == combHzReq_[slot] && spread == combSpreadReq_[slot]` (request-side shadow, because the owner's getter reports a sample-rate-clamped value) |
| `void setNoiseCombFeedback(std::size_t slot, float fb) noexcept` | guard + latch-aware compare, then `noise_.setCombFeedback(slot, fb);` | owner substitutes default on NaN (`:594`) → reject non-finite; early-out only if `combFeedbackLatched_[slot] && std::clamp(fb, 0.0f, NoiseOrganism::kCombFeedbackCap) == noise_.getCombFeedback(slot)`. **Latch-aware on purpose:** a value-only early-out would skip the FIRST push at the default (0.55/0.75 already running), leave the organism's latch unset, and let a later model change silently re-derive feedback away from the parameter — contradicting C-4 |
| `void setResonanceAnchorMode(ResonanceDriftNetwork::AnchorMode m) noexcept` | `if (m == resonance_.getAnchorMode()) return; resonance_.setAnchorMode(m);` | owner has no early-out (`resonance_drift_network.h:587-590`) |
| `void setEcologyLoopFilterMode(std::size_t loop, FeedbackEcology::FilterMode m) noexcept` | `if (loop >= FeedbackEcology::kMaxLoops) return; if (m == ecology_.getLoopFilterMode(loop)) return; ecology_.setLoopFilterMode(loop, m);` | owner validates the enumerator, has no early-out (`feedback_ecology.h:1035-1045`) |

New private members (per voice, ~40 B; `kVoiceSizeBound` has the headroom — its static_assert is the
check):

```cpp
std::array<bool,  NoiseOrganism::kMaxSources> combTuningSet_{};
std::array<float, NoiseOrganism::kMaxSources> combHzReq_{};
std::array<float, NoiseOrganism::kMaxSources> combSpreadReq_{};
std::array<bool,  NoiseOrganism::kMaxSources> combFeedbackLatched_{};
```

`prepare()` step 5 clears all four (`fill(false)` / `fill(0)`) directly after `noise_.setNumSources`
(`:557`), because `NoiseOrganism::prepare()` clears the organism's own latch (`noise_organism.h:1871`) — a
stale voice-side "latched" flag would otherwise suppress the first post-prepare push. `setNoiseCombFeedback`
sets `combFeedbackLatched_[slot] = true` after forwarding. No voice getter is added (read-backs use the
existing const accessors, spec *New components*).

### 2.4 D-4 Per-tone sub level base (FR-005) — `systems/vorago_engine.h`

```cpp
/// FR-005. Per-tone BASE under the shared macro offset: level(t) = subToneBaseDb_[t] + subToneOffsetDb_.
/// Out-of-range tone / non-finite dB: no-op. UNCHANGED VALUE: early-out (the :766-775 ramp re-arm reason).
/// Writes ONLY tone t, so the other two tones' in-flight ramps are not re-armed.
void setSubToneLevelDb(std::size_t tone, float dB) noexcept {
    if (tone >= SubharmonicEngine::kNumTones || !detail::isFinite(dB)) { return; }
    if (dB == subToneBaseDb_[tone]) { return; }
    subToneBaseDb_[tone] = dB;
    sub_.setToneLevelDb(tone, subToneBaseDb_[tone] + subToneOffsetDb_);
}
[[nodiscard]] float getSubToneLevelDb(std::size_t tone) const noexcept {
    return (tone < SubharmonicEngine::kNumTones) ? subToneBaseDb_[tone] : 0.0f;
}
// field, initialised to the constant so applySubToneLevels() computes the identical float sum:
std::array<float, SubharmonicEngine::kNumTones> subToneBaseDb_ = SubharmonicEngine::kDefaultToneLevelDb;
```

`applySubToneLevels()` (`:1154-1158`) reads `subToneBaseDb_[t]` instead of the constant. Because prepare
step 5b calls `applySubToneLevels()` (`:349`) from engine fields, a set base survives re-prepare (SC-022 (1)).
The component clamps the sum to `[kMinToneLevelDb −60, kMaxToneLevelDb +6]` (reject-never-clamp at the
engine, clamp at the owner — the existing offset setter's shape).

### 2.5 D-5 Ghost setters that survive re-prepare (FR-006) — `systems/vorago_engine.h`

The conflict to resolve: `prepare()` currently re-installs both ghost values from the **config**
(`:290`, `:323`), which would erase a setter value on re-prepare (SC-022 (1)). Resolution: a set-flag per
value; `prepare()` uses the config only while the setter has never been called. Phase 10a tests never call
the setters, so their config-driven behaviour is unchanged.

```cpp
void setGhostReverseProbability(float p) noexcept {
    if (!detail::isFinite(p)) { return; }                 // atmosphere maps NaN→0; we reject instead (FR-071)
    ghostReverseProbability_ = std::clamp(p, 0.0f, 1.0f);
    ghostReverseSet_ = true;
    atmos_.setGrainReverseProbability(ghostReverseProbability_);  // birth-time read (atmosphere_engine.h:947-955)
}
[[nodiscard]] float getGhostReverseProbability() const noexcept { return atmos_.getGrainReverseProbability(); }

void setGhostEventTriggers(bool on) noexcept {
    if (ghostEventTriggers_ && !on) { ghostTriggerHigh_ = false; }   // on→off disarms (SC-022 (2))
    ghostEventTriggers_ = on;
    ghostTriggersSet_ = true;
}
[[nodiscard]] bool getGhostEventTriggers() const noexcept { return ghostEventTriggers_; }
[[nodiscard]] bool isGhostTriggerLatchHigh() const noexcept { return ghostTriggerHigh_; }  // test observable
```

`prepare()` edits (the two existing lines, nothing else in that step):
`if (!ghostTriggersSet_) { ghostEventTriggers_ = cfg.atmosGhostEventTriggers; }` (`:290`), and
`if (!ghostReverseSet_) { ghostReverseProbability_ = cfg.atmosGhostReverseProbability; }
atmos_.setGrainReverseProbability(ghostReverseProbability_);` (`:323`). New fields
`float ghostReverseProbability_ = 0.0f; bool ghostReverseSet_ = false; bool ghostTriggersSet_ = false;`
beside `:1568`. `ghostTriggerHigh_ = false` at `:291` stays (a re-prepare always disarms).

### 2.6 D-6 FR-060 — `kRows` retune for Gravity / Pressure / Mass

**Constraints (spec FR-060, SC-021, C-4, FR-007):** only `kRows` in `vorago_macro_matrix.h`; no base
moves; new rows must contribute exactly 0 at every macro neutral (automatic: `applyModCurve(c, 0) == 0`
and Gravity's `g = 0` at 0.5, `:1057-1065`); curves Linear/Exponential/SCurve only
(`noRowUsesSteppedCurve`); a new row on a shared target MUST carry that target's existing base
(`everyRowSharesOneBasePerTarget`); `VoragoMacroTarget::Count == 39` is untouched (a new target is a
"spec amendment", `:1110-1113`). Thresholds are Phase 10's and are never moved.

#### Mass — concrete row (high confidence)

Metric: energy < 80 Hz at C3 (130.8 Hz), `DbHigher`, ≥ 0.25 dB, rho ≥ +0.9. At C3 the three sub tones
(65.4, 32.7, 43.6 Hz) are the band's content. Phase 10 ruled the sub tracking to 1.0 and made
`Mass → SubTrackingAmount` a claim row, so no Mass row touches the sub level directly (§1.1). Add:

```cpp
// FR-060 (Phase 12, OQ-2 ruled (b)): the sub band is Mass's metric (Q-Q), so Mass raises it directly.
// Shares Weight's target and base (0.0 dB). Contributes 0 at Mass = 0 (Linear).
{.macro = VoragoMacro::Mass, .owner = VoragoMacroTargetOwner::Engine,
 .target = VoragoMacroTarget::SubToneLevelOffsetDb, .base = 0.0f, .amount = 3.0f,
 .curve = ModCurve::Linear},
```

Expected: +3 dB on all three tones minus the measured −0.35…−0.45 dB bus thinning ≈ **+2.5 dB**,
monotone (linear in dB per sweep point) → rho +1. The amount is AR-6 tuning; the probe (below) measures it
before landing, and if the measured endpoint is < 0.25 dB the amount is raised (never above +6 dB, which
would put Div2 at −12 dB + Weight travel inside the +6 dB tone clamp), never the threshold.

#### Gravity and Pressure — predicted unreachable; SC-021 BLOCKED on a spec amendment (gate P-0)

**Status (updated 2026-09-24, ruling R-1, spec *Clarifications* plan-stage session): RESOLVED IN
ADVANCE.** The remedy for any axis the probe confirms unreachable is **(iii) one new `VoragoMacroTarget`
per unreachable axis** (additive, default-inert, macro-only, no parameter ID; `Count` 39 → 40/41). Gate
P-0 therefore no longer waits on a user answer: T005 reads the probe table and picks, per axis, either the
admissible row set or the new-target path. The paragraphs below record the prediction that motivated the
ruling. **Probe outcome (2026-09-24, `artifacts/fr060_probe.log`): Mass +3.0 dB passes (2.4343 dB);
Gravity best 0.2024 and Pressure best 1.5030 dB — both path B. The wander-depth and bare-drive targets
named here were rejected on the data; the landed designs are spec B-1 (`ResonanceOctaveLock`, Voice-owned)
and B-2 (`OutputDriveDb` with makeup compensation, Engine-owned, plus the P-a saturation retune).** **Original status: BLOCKED, not open.** As encoded (ruling (b), FR-007's single `kRows` edit, `Count == 39`,
frozen bases, unchanged thresholds), the spec admits no mechanism this plan predicts can close Gravity or
Pressure; Phase 10's own test records why (`vorago_macro_test.cpp:1306-1315`, figures quoted below) and
names the levers (keyed ratio table, a saturator drive row) — both outside ruling (b). The plan therefore
does not claim a design for these two axes; it designs the **measurement** that either lifts the block (an
admissible row set exists after all) or supplies the measured numbers for the amendment.

**Gravity** (metric `meanOctaveOffset` over the resonance network's peak frequencies, `PercentLower`,
≥ 30 %, rho ≤ −0.9; `vorago_macro_test.cpp:723-746`). The peak anchors are
`free + g·(keyed − free)` (`resonance_drift_network.h:1435`); neither the free anchors nor the keyed ratio
table is a macro target. Phase 10 recorded the stone-end floor of the metric at **0.209** in theory
(0.22 measured) and the best air-end figure over notes 24–72 at **0.266** (`vorago_macro_test.cpp:1308-1312`).
Any row that cannot move anchors leaves the endpoint ≤ (0.266 − 0.209)/0.266 = **21.4 %** < 30 %. The only
existing targets that touch the metric at all are lane depths/rates that add noise *around* the anchors:

| Candidate | Row | Mechanism | Prediction |
|---|---|---|---|
| G-a | `Gravity → BreathingDepth`, base 0.30 (shared with Movement), amount −0.30, Linear | bipolar: stone → depth 0 (the breath gravity lane can no longer pull g off +1, `vorago_voice.h:649`), air → 0.60 (pulls g toward 0 half the time) | lowers the stone floor toward 0.209 but also lowers the air figure; net ≤ 21 % |
| G-b | `Gravity → ResonanceWanderRate`, base 0.03 (shared with Movement), amount −0.028, Exponential | stone slower wander, air faster | wander DEPTH (1.5 st, `vorago_voice.h:575`) is not a target, rate alone barely moves a time-average |

**Pressure** (metric crest factor at C1, `DbLower`, ≥ 3 dB; Phase 10: "saturation at its 1.0 ceiling alone
moves it 0.4 dB", `vorago_macro_test.cpp:1313-1315`; output drive `kOutputDriveDb` is a constant, not a
target, `vorago_engine.h:202`):

| Candidate | Row | Prediction |
|---|---|---|
| P-a | `Pressure → OutputSaturation` amount 0.35 → 0.88 (reaches the 1.0 clamp) | 0.4 dB (measured in Phase 10) |
| P-b | `Pressure → SubToneLevelOffsetDb` (base 0.0, shared), amount −12 dB, Linear | removes the sub + fundamental beat that sets the crest floor; ≤ ~1 dB if the subs sit ≥ 18 dB under the fundamental |
| P-c | `Pressure → EcologyLoopGain` amount 0.16 → 0.18 (to `kMaxLoopGain` 0.90) | wet path ~30 dB down by voicing (roadmap line 276-278 note) → negligible |
| P-d | `Pressure → CloudRichness` (base 0.70, shared with Density), amount +0.28 | more partials; crest direction unknown |

**Procedure (binding for the builder):**
1. Land D-1 first (the override API is the probe's instrument).
2. Run `VoragoMacro_Phase12RetuneProbe` (`[.probe]`, §5.2): it reproduces the SC-008 fixture
   (same seeds, points, note, window, fast attack, polyphony 1, macros after `noteOn`) and **emulates a
   candidate row without editing `kRows`** by writing `setTargetBase(target, base + amount·curve(x))` (for
   Gravity: `base + amount·curve(|g|)·sign(g)`) at each sweep point `x`, with the macro itself set to
   `x` so the shipped rows still apply. It prints, per candidate and per combination (G-a+G-b; any P
   subset), mean rho and mean endpoint. Log to `specs/vorago-phase12-parameters/artifacts/`.
3. A candidate set is admissible iff mean rho and endpoint pass Phase 10's thresholds. Land the cheapest
   admissible set as real `kRows` rows, update `kNumRows` and its comment formula (`:254-257`), then run
   `VoragoMacro_SweepAxes` + `VoragoComposed_DepthMacroAxis` (all twelve axes, thresholds unchanged) and
   the SC-001b gate alone.
4. **Stop-and-surface** if Gravity or Pressure has no admissible set: do NOT land a partial retune for that
   axis, do NOT add a target, do NOT edit the resonance ratio table, and do NOT move a threshold. Report the
   probe table to the user with the remedies that lie outside ruling (b) (see §8 open question 1). SC-021
   is **BLOCKED on a spec amendment** for that axis until the user rules and Q1 / FR-060 / SC-021 are
   re-worded to the chosen remedy. Every other Phase 12 deliverable is independent of this outcome and
   proceeds.

**Gate P-0 (binding on tasks.md generation):**
- The first DSP tasks are "T-P0a: D-1 override + `VoragoMacro_TargetBaseOverride` green" and
  "T-P0b: run `VoragoMacro_Phase12RetuneProbe` alone; log the per-candidate / per-combination table
  (mean rho, mean endpoint, per-seed figures) to `specs/vorago-phase12-parameters/artifacts/fr060_probe.log`".
- Then a **blocking** task "T-P0c: present the probe table to the user; record the ruling; amend spec
  Q1 / FR-060 / SC-021 (and this section) to it". Every task that lands a Gravity / Pressure row or closes
  SC-021 for those axes **depends on T-P0c** and is not generated against the current wording — unless
  T-P0b shows an admissible set, in which case T-P0c reduces to reporting it and the step 3 landing
  proceeds under the existing wording.
- Not blocked: the Mass row, SC-021's re-runs of the other axes, and the SC-001b CPU gate.
- Until T-P0c closes, the Phase 12 `compliance.md` rows for Gravity and Pressure read
  **"BLOCKED — spec amendment pending (FR-060 / SC-021 infeasible as encoded)"** with the probe figures;
  never "PASSING", never a silent "open".

---

## 3. Plugin: parameter surface

### 3.1 Files

| File | Change |
|---|---|
| `plugins/vorago/src/plugin_ids.h` | `kCurrentStateVersion = 2`; 94 new enumerators; reserved-map comment (bands 1200–1599 claimed, 1600+ unassigned, macros LIVE); range-end constants for every band |
| `plugins/vorago/src/parameters/param_mapping.h` (NEW) | linear / log / offset-log / discrete mapping functions (§3.3), `Taper` enum, `kVoragoSeedValues`, `kCavernSeedSalt`, noise-type index table |
| `plugins/vorago/src/parameters/global_params.h` | + `seedIndex`, `outputSaturation`, `sustainPedal`, `channelPressure`; v2 extension save/load split out as `saveGlobalParamsV2Ext` / `loadGlobalParamsV2Ext` so the v1 8-byte block is untouched |
| `.../cloud_params.h, noise_params.h, resonance_params.h, ecology_params.h, sub_params.h, smear_params.h, events_params.h, ecosystem_params.h, body_params.h, space_params.h, envelope_params.h, bloom_params.h, ghost_params.h, life_params.h` (NEW ×14) | six-function contract each (FR-011) |
| `plugins/vorago/src/parameters/param_routes.h` (NEW) | `enum class Route`, `kParamRoutes[108]`, `routeOf(id)`, `kMbRoutes[39]`, compile-time completeness asserts |
| `plugins/vorago/src/processor/sustain_latch.h` (NEW) | `Vorago::SustainLatch` |
| `plugins/vorago/src/engine/vorago_engine_config.h` | `makeVoragoCavernConfig(maxBlock, std::uint32_t seed = kCavernSeed)`; `cavernSeedFor(index)` |
| `plugins/vorago/src/processor/processor.{h,cpp}` | routes, trackers, push cadence, sustain, state v2, test seams |
| `plugins/vorago/src/controller/controller.{h,cpp}` | register/format/mirror every pack; `IMidiMapping` |
| `plugins/vorago/CMakeLists.txt` | list the 17 new headers (header list is enumerated, `:20-48`) |
| `plugins/vorago/CLAUDE.md` | FR-052 updates |

All plugin additions are header-only except the two existing `.cpp`s, so the "every new .cpp in BOTH lists"
rule (`tests/CMakeLists.txt:18-21`) is not triggered.

### 3.2 The parameter table (C-5/C-6 transcribed; normative for code and for the SC-018 checked-in table)

Legend — Route: MB / VP / ENG / CV / MAC / Local. Type: R = `Vst::Parameter` (continuous, stepCount 0),
L(n) = `StringListParameter` with n entries (stepCount n−1), T = 2-entry list toggle "Off/On". State: F =
float, I = int32 index. `n₀` = registered default normalized value (computed at registration by the inverse
mapping in double; the decimal here is for reading and for the checked-in table with tolerance 1e-9).
Taper: lin / log / olog(ε) / —. Clamp source column cites the destination setter whose clamp IS the plain
range.

**Global (0–99)** — `kGlobalParamRangeEnd = 100`

| ID | Enum | Title / units | Route | Type | Plain range | Default | n₀ | Taper | State | Clamp source |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `kMasterGainId` (shipped) | Master Gain / dB | Local | R | [0, 2] lin gain | 1.0 | 0.5 | lin | F (v1) | `global_params.h:54` |
| 1 | `kPolyphonyId` (shipped) | Polyphony | ENG | L(6) | 1–6 | 4 | 0.6 | — | I (v1) | `:59` |
| 2 | `kSeedId` | Seed | ENG | L(16) | index 0–15 | 0 | 0.0 | — | I | C-8 |
| 3 | `kOutputSaturationId` | Output Saturation / % | MB `OutputSaturation` | R | [0, 1] | 0.12 | 0.12 | lin | F | `vorago_engine.h:854` |
| 4 | `kSustainPedalId` | Sustain Pedal | Local | R, `kCanAutomate│kIsHidden` | [0, 1], ≥ 0.5 = down | 0 | 0.0 | — | not persisted | C-9 |
| 5 | `kChannelPressureId` | Channel Pressure / % | MAC | R, `kCanAutomate│kIsHidden` | [0, 1] | 0 | 0.0 | lin | not persisted | FR-021 |

**Macros (100–111)** — shipped, types frozen, now LIVE (MAC route).

**Cloud (200–299)** — `kCloudParamRangeEnd = 300`

| ID | Enum | Title / units | Route | Type | Plain range | Default | n₀ | Taper | State | Clamp source |
|---|---|---|---|---|---|---|---|---|---|---|
| 200 | `kCloudRichnessId` | Cloud Richness / % | MB `CloudRichness` | R | [0, 1] | 0.70 | 0.70 | lin | F | `harmonic_cloud.h:416` |
| 201 | `kCloudTiltId` | Cloud Tilt / dB/oct | MB `CloudSpectralTiltDb` | R | [−12, 12] | −4.0 | 0.333333333 | lin | F | `:443` |
| 202 | `kCloudMutationId` | Cloud Mutation / % | MB `CloudMutation` | R | [0, 1] | 0.15 | 0.15 | lin | F | `:452-456` |
| 203 | `kCloudInharmonicityId` | Cloud Inharmonicity | MB `CloudInharmonicity` | R | [0, 0.1] | 0.015 | 0.15 | lin | F | `:430` |
| 204 | `kCloudDriftDepthId` | Cloud Drift Depth / ct | MB `CloudDriftDepthCents` | R | [0, 50] | 8.0 | 0.16 | lin | F | `:505` |
| 205 | `kCloudStereoSpreadId` | Cloud Stereo Spread / % | VP | R | [0, 1] | 0.45 | 0.45 | lin | F | `:539` |
| 206 | `kCloudSpectralGravityId` | Cloud Spectral Gravity | VP | R | [−1, 1] | 0.10 | 0.55 | lin | F | `:478-483` |

**Noise (300–399)** — `kNoiseParamRangeEnd = 400`. Slot IDs `310+s, 320+s, 330+s, 340+s, 350+s`, s = 0..3.

| ID | Enum | Title / units | Route | Type | Plain range | Default | n₀ | Taper | State | Clamp source |
|---|---|---|---|---|---|---|---|---|---|---|
| 300 | `kNoiseLevelId` | Noise Level / dB | MB `NoiseLevelDb` | R | [−96, 12] | −18 | 0.722222222 | lin | F | `noise_generator.h:104-105`, `vorago_voice.h:1160-1161` |
| 301 | `kNoiseWakeId` | Noise Wake / % | MB `NoiseWakeBase` | R | [0, 1] | 0.35 | 0.35 | lin | F | `vorago_voice.h:1180` |
| 302 | `kNoiseWanderRateId` | Noise Wander Rate / Hz | MB `NoiseWanderRate` | R | [0.01, 100] | 0.03 | 0.119280314 | log | F | `noise_organism.h:781-784` |
| 310–313 | `kNoiseSlot{0..3}ModelId` | Noise Slot N Model | VP | L(4) `Direct, Filtered Wind, Granular Dust, Metallic Hiss` (index == `NoiseOrganismModel`) | — | 1 / 2 / 0 / 3 | 1/3, 2/3, 0, 1 | — | I | `noise_organism.h:126-131` |
| 320–323 | `kNoiseSlot{0..3}TypeId` | Noise Slot N Type | VP | L(12) `NoiseType` minus `ModulationNoise` (§3.3.4) | — | Brown (index 5) | 5/11 | — | I | `noise_generator.h:44-58` |
| 330–333 | `kNoiseSlot{0..3}CombFundamentalId` | Noise Slot N Comb Freq / Hz | VP | R | [20, 19845] | 60 | 0.159219747 | log | F | `noise_organism.h:573-574`, `resonator_bank.h:42,45` |
| 340–343 | `kNoiseSlot{0..3}CombSpreadId` | Noise Slot N Comb Spread / % | VP | R | [0, 1] | 0.35 | 0.35 | lin | F | `:575` |
| 350–353 | `kNoiseSlot{0..3}CombFeedbackId` | Noise Slot N Comb Feedback / % | VP | R | [0, 0.9] | 0.55 / 0.55 / 0.55 / 0.75 | 0.611111111 ×3, 0.833333333 | lin | F | `:594`, `:165` |

**Resonance (400–499)** — `kResonanceParamRangeEnd = 500`

| ID | Enum | Title / units | Route | Type | Plain range | Default | n₀ | Taper | State | Clamp source |
|---|---|---|---|---|---|---|---|---|---|---|
| 400 | `kResonanceGravityId` | Resonance Gravity | MB `ResonanceGravity` | R | [−1, 1] | 0.0 | 0.5 | lin | F | `vorago_voice.h:1199` |
| 401 | `kResonanceMixId` | Resonance Mix / % | MB `ResonanceMix` | R | [0, 1] | 0.45 | 0.45 | lin | F | `resonance_drift_network.h:798` |
| 402 | `kResonanceWanderRateId` | Resonance Wander Rate / Hz | MB `ResonanceWanderRate` | R | [0.002, 1] | 0.03 | 0.435755587 | log | F | `:715-717`, `:148-149` |
| 403 | `kResonanceAnchorModeId` | Resonance Anchor | VP | L(3) `Free, Keyed, Hybrid` | — | Hybrid (2) | 1.0 | — | I | `:293` |

**Ecology (500–599)** — `kEcologyParamRangeEnd = 600`

| ID | Enum | Title / units | Route | Type | Plain range | Default | n₀ | Taper | State | Clamp source |
|---|---|---|---|---|---|---|---|---|---|---|
| 500 | `kEcologyMixId` | Ecology Mix / % | MB `EcologyMix` | R | [0, 1] | 0.15 | 0.15 | lin | F | `feedback_ecology.h:1316` |
| 501 | `kEcologyLoopGainId` | Ecology Loop Gain / % | MB `EcologyLoopGain` | R | [0, 0.9] | 0.72 | 0.8 | lin | F | `:1115`, `:264-266` |
| 510–515 | `kEcologyLoop{0..5}FilterModeId` | Ecology Loop N Filter | VP | L(3) `Lowpass, Bandpass, Highpass` | — | Lowpass (0) | 0.0 | — (stepped) | I | `:597`, `:1035-1045` |

**Sub (600–699)** — `kSubParamRangeEnd = 700`

| ID | Enum | Title / units | Route | Type | Plain range | Default | n₀ | Taper | State | Clamp source |
|---|---|---|---|---|---|---|---|---|---|---|
| 600 | `kSubLevelOffsetId` | Sub Level Offset / dB | MB `SubToneLevelOffsetDb` | R | [−24, 24] | 0.0 | 0.5 | lin | F | **no owner clamp** (`vorago_engine.h:764-780`); range chosen by this plan (§8 decision D-P3) |
| 601 | `kSubTrackingId` | Sub Tracking / % | MB `SubTrackingAmount` | R | [0, 1] | 1.0 | 1.0 | lin | F | `:787` |
| 610 | `kSubDiv2LevelId` | Sub ÷2 Level / dB | ENG | R | [−60, 6] | −18 | 0.636363636 | lin | F | `subharmonic_engine.h:210-211, 218` |
| 611 | `kSubDiv4LevelId` | Sub ÷4 Level / dB | ENG | R | [−60, 6] | −24 | 0.545454545 | lin | F | same |
| 612 | `kSubFifthBelowLevelId` | Sub Fifth-Below Level / dB | ENG | R | [−60, 6] | −30 | 0.454545455 | lin | F | same |

**Smear 700–702** (`kSmearParamRangeEnd = 800`): `kSmearAmountId` [0,1] 0.20 (`vorago_engine.h:808`),
`kSmearDecoherenceId` [0,1] 0.20 (`:817`), `kSmearTiltId` [−1,1] 0.0 → n₀ 0.5 (`:826`); all MB (Engine),
R, lin, F.
**Events 800** (`kEventsParamRangeEnd = 900`): `kEventsRateScaleId` "Event Rate" ×, MB `EventRateScale`,
R, [0.1, 10], default 1.0, n₀ **0.5 exactly**, log, F (`vorago_voice.h:1357`).
**Ecosystem 900** (`kEcosystemParamRangeEnd = 1000`): `kEcosystemDepthId` %, MB `EcosystemDepth`, [0,1],
0.85, lin, F (`:1313`).

**Body (1000–1099)** — `kBodyParamRangeEnd = 1100`

| ID | Enum | Route | Type | Plain range | Default | n₀ | Taper | State |
|---|---|---|---|---|---|---|---|---|
| 1000 | `kBodyBlendId` | MB `BodyBlend` | R | [0, 1] | 0.35 | 0.35 | lin | F |
| 1001 | `kBodyDampingId` | MB `BodyDamping` | R | [0, 1] | 0.25 | 0.25 | lin | F |
| 1002 | `kBodyResonanceId` | MB `BodyResonance` | R | [0, 1] | 0.70 | 0.70 | lin | F |
| 1003 | `kBodyMixId` | MB `BodyMix` | R | [0, 1] | 1.00 | 1.0 | lin | F |
| 1004 | `kBodyMaterialAId` | VP | L(11) in `BodyMaterial` declaration order | — | StoneChamber (5) | 0.5 | — | I |
| 1005 | `kBodyMaterialBId` | VP | L(11) | — | SteelTank (6) | 0.6 | — | I |

Clamp sources `continuous_body.h:142-159` (via `vorago_voice.h:1235-1282`); materials `:84-100`.

**Space (1100–1199)** — `kSpaceParamRangeEnd = 1200`; clamp source `cavern_verb.h:613-748`

| ID | Enum | Route | Type | Plain range | Default | n₀ | Taper | State |
|---|---|---|---|---|---|---|---|---|
| 1100 | `kSpaceSizeId` | MB `CavernSize` | R | [0, 1] | 0.50 | 0.5 | lin | F |
| 1101 | `kSpaceDarknessId` | MB `CavernDarkness` | R | [0, 1] | 0.80 | 0.8 | lin | F |
| 1102 | `kSpaceDecayId` (s) | MB `CavernDecaySeconds` | R | [0.5, 60] | 20.0 | 0.770524453 | log | F |
| 1103 | `kSpaceFogId` | MB `CavernFog` | R | [0, 1] | 0.30 | 0.3 | lin | F |
| 1104 | `kSpaceDamperDepthId` | MB `CavernDamperDepth` | R | [0, 1] | 0.35 | 0.35 | lin | F |
| 1105 | `kSpaceMixId` | MB `CavernMix` | R | [0, 1] | 1.00 | 1.0 | lin | F |
| 1106 | `kSpaceWidthId` | MB `CavernWidth` | R | [0, 1] | 1.00 | 1.0 | lin | F |
| 1107 | `kSpaceDensityId` | CV `setDensity` | R | [0, 1] | 0.75 | 0.75 | lin | F |
| 1108 | `kSpaceDimensionalityId` | CV `setDimensionality` | R | [0, 1] | 0.50 | 0.5 | lin | F |
| 1109 | `kSpaceBreathId` | CV `setBreath` | R | [0, 1] | 0.50 | 0.5 | lin | F |
| 1110 | `kSpaceEarlySizeId` (ms) | CV `setEarlySizeMs` | R | [80, 300] (= `kEarlySizeMinMs`, `kDefaultMaxEarlySeconds·1000` — the prepared config's ceiling, `cavern_verb.h:665-667`) | 220 | 0.765346277 | log | F |
| 1111 | `kSpaceEarlyLevelId` | CV `setEarlyLevel` | R | [0, 1] | 0.80 | 0.8 | lin | F |
| 1112 | `kSpaceEarlyAbsorptionId` | CV `setEarlyAbsorption` | R | [0, 1] | 0.60 | 0.6 | lin | F |
| 1113 | `kSpaceEarlySendId` | CV `setEarlySend` | R | [0, 1] | 0.70 | 0.7 | lin | F |
| 1114 | `kSpaceDamperRateId` | CV `setDamperRate` | R | [0, 1] | 0.15 | 0.600761933 | olog(0.01) | F |
| 1115 | `kSpaceFreezeId` | CV `setFreeze` | T | Off/On | Off | 0.0 | — | I |

**Envelope (1200–1299) NEW** — `kEnvelopeParamRangeEnd = 1300`

| ID | Enum | Route | Type | Plain range | Default | n₀ | Taper | State |
|---|---|---|---|---|---|---|---|---|
| 1200 | `kEnvelopeModeId` | ENG `setEnvelopeMode` | L(2) `Standard, Growth` | — | Standard | 0.0 | — (stepped) | I |
| 1201 | `kEnvelopeStage0TimeId` (ms) | ENG `setEnvelopeStageTimeMs(0, ·)` | R | [0, 120000] | 20000 | 0.809284413 | olog(10 ms) | F |
| 1202 | `kEnvelopeStage1TimeId` | ENG stage 1 | R | same | 30000 | 0.852434578 | olog(10 ms) | F |
| 1203 | `kEnvelopeStage2TimeId` | ENG stage 2 | R | same | 45000 | 0.895590654 | olog(10 ms) | F |
| 1204 | `kEnvelopeStage3TimeId` | ENG stage 3 | R | same | 60000 | 0.926212855 | olog(10 ms) | F |
| 1205 | `kEnvelopeReleaseId` | ENG `setEnvelopeReleaseMs` | R | same | 45000 | 0.895590654 | olog(10 ms) | F |
| 1206 | `kEnvelopeGrowthDurationId` (s) | ENG `setGrowthDurationSeconds` | R | [1, 120] (`growth_envelope.h:97`, `vorago_voice.h:333,1063`) | 120 | 1.0 | log (non-zero floor, no ε) | F |

**Bloom (1300–1399) NEW**: `kBloomDepthId` MB `BloomDepth` [0,1] 0.60 lin F (`vorago_voice.h:1371`);
`kBloomSpawnRateId` (Hz) MB `BloomSpawnRateHz` [0, 0.05] default 1/240, n₀ 0.603772849, **olog(1e-4 Hz)**, F
(`bloom_engine.h:547`).
**Ghost (1400–1499) NEW**: `kGhostPeakLevelId` MB `GhostPeakLevel` [0,1] 0.60 lin F; `kGhostBlurId` MB
`AtmosBlur` [0,1] 0.85 lin F; `kGhostReverseProbabilityId` ENG [0,1] 0 lin F; `kGhostEventTriggersId` ENG
T Off, I.
**Life (1500–1599) NEW**: `kLifeBreathingDepthId` 0.30, `kLifeBreathingIrregularityId` 0.30,
`kLifeTidalDepthId` 0.40; MB, [0,1], lin, F (`breathing_modulator.h:177-185`, `tidal_modulator.h:209-210`).

**Totals (asserted at compile time in `param_routes.h`):** 108 IDs; MB 39, VP 31, ENG 14, CV 9, MAC 13
(12 macros + pressure), Local 2 (master gain, sustain). Persisted 106. Every enumerator name was swept
with `grep -rn "\b<name>\b" dsp/ plugins/`: the Vorago names collide only with same-named enumerators in
`namespace Seraphis` (e.g. `kCloudRichnessId`, `kSeedId`) — a different namespace and a different target
(`vorago_tests` never links Seraphis sources), recorded as a near-name hazard: no TU may `using namespace`
both plugin namespaces. The builder re-runs the sweep per name before landing (spec ODR rule).

### 3.3 Mapping functions (`parameters/param_mapping.h`, namespace `Vorago`)

All in `double`, clamp in and out (the `logMapFromNormalized` convention, `parameter_helpers.h:74-88`), and
cast to `float` exactly once at the store.

```cpp
enum class Taper : std::uint8_t { Linear, Log, OffsetLog, Discrete };

[[nodiscard]] inline double linearFromNormalized(double n, double mn, double mx) noexcept {
    return mn + std::clamp(n, 0.0, 1.0) * (mx - mn); }
[[nodiscard]] inline double linearToNormalized(double u, double mn, double mx) noexcept {
    return std::clamp((std::clamp(u, mn, mx) - mn) / (mx - mn), 0.0, 1.0); }
// Log: Krate::Plugins::logMapFromNormalized / logMapToNormalized (parameter_helpers.h:80-88), unchanged.
// Offset-log (Q4, zero-floored ranges): units + ε is log-mapped over [mn + ε, mx + ε].
[[nodiscard]] inline double offsetLogFromNormalized(double n, double mn, double mx, double eps) noexcept {
    return std::clamp(Krate::Plugins::logMapFromNormalized(n, mn + eps, mx + eps) - eps, mn, mx); }
[[nodiscard]] inline double offsetLogToNormalized(double u, double mn, double mx, double eps) noexcept {
    return Krate::Plugins::logMapToNormalized(std::clamp(u, mn, mx) + eps, mn + eps, mx + eps); }
[[nodiscard]] inline int indexFromNormalized(double n, int count) noexcept {        // StringList
    return std::clamp(static_cast<int>(std::clamp(n, 0.0, 1.0) * (count - 1) + 0.5), 0, count - 1); }
[[nodiscard]] inline double indexToNormalized(int i, int count) noexcept {
    return (count > 1) ? static_cast<double>(std::clamp(i, 0, count - 1)) / (count - 1) : 0.0; }
```

#### 3.3.1 Every ε (the plan citation FR-013 asks for)

| ID(s) | Range | Form | ε | n = 0 → | n = 0.5 → | n = 1 → |
|---|---|---|---|---|---|---|
| 1201–1205 | [0, 120000] ms | olog | **10 ms** | 0 ms | 1085.49 ms | 120000 ms |
| 1301 | [0, 0.05] Hz | olog | **1e-4 Hz** | 0 Hz (no spawns) | 0.002138 Hz | 0.05 Hz |
| 1114 | [0, 1] | olog | **0.01** | 0 | 0.090499 | 1 |
| 1206 | [1, 120] s | log | — (floor 1 s) | 1 s | 10.9545 s | 120 s |

ε is chosen so the bottom decade of the knob reaches exactly 0 while the useful musical range (seconds for
envelopes, minutes-per-event for bloom) occupies most of the travel; `log(0)` is never evaluated (the
argument is always ≥ ε > 0). The non-zero-floored log IDs (302, 330–333, 402, 800, 1102, 1110) use plain
`logMapFromNormalized`; their `n = 0.5` values are 1.0 Hz, 630.0 Hz, 0.044721 Hz, 1.0 ×, 5.4772 s,
154.919 ms respectively — these are the SC-018 taper assertions.

#### 3.3.2 Seed table

```cpp
inline constexpr std::array<std::uint32_t, 16> kVoragoSeedValues = {
    0x00000001u, 0x82D2B16Eu, 0xE9705B48u, 0x2F922331u, 0xB7E9DB62u, 0x7EC32EA4u, 0x6F8F6B83u, 0x3EE6AA57u,
    0xD1B7E8C1u, 0x7902AADAu, 0x6B3EBF4Au, 0xAF33581Eu, 0x2673DE0Cu, 0x8F9D4B1Du, 0x95BD2F37u, 0x1DB23302u};
inline constexpr std::uint32_t kCavernSeedSalt = 0x43415645u;  // 'CAVE'
[[nodiscard]] constexpr std::uint32_t cavernSeedFor(int index) noexcept {  // C-8
    return (index == 0) ? kCavernSeed : (kVoragoSeedValues[index] ^ kCavernSeedSalt); }
```

Index 0 is pinned to `1u` (= `kEngineSeed`, `vorago_engine_config.h:30`); entries 1–15 are the first 15
values > 1 of a splitmix64 stream seeded with `0x5641524F474F3132` ("VARAGO12"), generated by a one-off Node
command recorded in the header comment. **The table is a result, not a starting point**: if SC-014 (3)
finds a pair within RMS difference ≤ 1e-3, the offending entry is replaced by the next stream value and
the whole gate re-run; the gate is never lowered (Seraphis `dropdown_mappings.h:62-66` rule). Labels
`"Seed 1" … "Seed 16"`.

#### 3.3.3 Noise-type list (12 entries, `ModulationNoise` excluded)

`kNoiseTypeByIndex[12] = {White, Pink, TapeHiss, VinylCrackle, Asperity, Brown, Blue, Violet, Grey,
Velvet, VinylRumble, RadioStatic}` (indices 0–10 equal the enum values 0–10; index 11 → enum 12).
`noiseTypeToIndex` is the inverse (ModulationNoise → TapeHiss's index 2, matching the organism's own
substitution, `noise_organism.h:1239`). Default Brown = index 5 → n₀ = 5/11. A `static_assert` pins
`kNoiseTypeByIndex[5] == NoiseType::Brown` and that the list has no `ModulationNoise`.

#### 3.3.4 Materials

`BodyMaterial` list index == enum value (Q5 ruled (a)); `static_assert(ContinuousBody::kNumMaterials == 11)`
and `static_cast<int>(StoneChamber) == 5`, `SteelTank == 6` in `body_params.h`.

### 3.4 Pack contract (FR-011) — shape shown for `cloud_params.h`; all 14 identical in form

```cpp
struct CloudParams {                          // PLAIN units; explicit initializers == C-6 defaults
    std::atomic<float> richness{0.70f};  std::atomic<float> tiltDb{-4.0f};
    std::atomic<float> mutation{0.15f};  std::atomic<float> inharmonicity{0.015f};
    std::atomic<float> driftCents{8.0f}; std::atomic<float> stereoSpread{0.45f};
    std::atomic<float> spectralGravity{0.10f};
};
void handleCloudParamChange(CloudParams&, Vst::ParamID, Vst::ParamValue) noexcept;   // denormalize, relaxed store
void registerCloudParams(Vst::ParameterContainer&);                                  // defaults from inverse map
tresult formatCloudParam(Vst::ParamID, Vst::ParamValue, Vst::String128);            // units per §3.2
void saveCloudParams(const CloudParams&, IBStreamer&);                               // ascending ID order
bool loadCloudParams(CloudParams&, IBStreamer&);                                     // EOF-safe, finite, clamp
template <typename F> void loadCloudParamsToController(IBStreamer&, F setParam);    // inverse map
```

Rules applied in every pack (spec C-7, FR-012/013):
- continuous params: `parameters.addParameter(title, units, 0, n₀, kCanAutomate, id)`; discrete:
  `createDropdownParameterWithDefault(...)` **and** `getInfo().defaultNormalizedValue = n₀` (P-1 trap,
  `global_params.h:82-84`);
- `handle…` ignores unregistered in-band IDs (explicit `switch` with `default: break`); the caller has
  already rejected non-finite normalized values and clamped to [0,1] (§4.2);
- `load…` stops at the first failed read (returns false, later fields unchanged), skips non-finite floats
  (`detail::isFinite`), clamps finite floats to the plain range, clamps int indices to `[0, count−1]`;
- discrete values are stored in the atomics as `std::atomic<int>` indices (not enums) so the stream, the
  atomics and the controller share one domain.

---

## 4. Processor design (`plugins/vorago/src/processor/`)

### 4.1 Route table (`param_routes.h`)

```cpp
enum class Route : std::uint8_t { MB, VP, ENG, CV, MAC, Local };
struct ParamRouteEntry { Steinberg::Vst::ParamID id; Route route; };
inline constexpr std::array<ParamRouteEntry, 108> kParamRoutes = {{ /* §3.2, ascending ID */ }};
[[nodiscard]] constexpr std::optional<Route> routeOf(Steinberg::Vst::ParamID id) noexcept;  // linear scan
struct MbRouteEntry { Steinberg::Vst::ParamID id; Krate::DSP::VoragoMacroTarget target; };
inline constexpr std::array<MbRouteEntry, 39> kMbRoutes = {{ ... }};
// compile-time completeness (constexpr predicates + static_assert):
//   strictly ascending unique IDs; count per route == {39, 31, 14, 9, 13, 2};
//   every VoragoMacroTarget appears in kMbRoutes exactly once; every kMbRoutes id has Route::MB.
```

### 4.2 `processParameterChanges` (FR-013, C-9)

Per queue: `id = getParameterId()`. If `id == kSustainPedalId`: copy **every** point `(offset, value)` into
`pedalPoints_` (fixed `std::array<PedalPoint, 128>`; if a host sends more, the surplus points are
coalesced into the last slot so the final pedal state is never lost), then continue. Otherwise take the
last point (unchanged Phase 11 rule); `if (!detail::isFinite(value)) continue;` (double overload,
`db_utils.h:125`); `value = std::clamp(value, 0.0, 1.0)`; dispatch by band (`id < kGlobalParamRangeEnd` →
global … `id < kLifeParamRangeEnd` → life; ≥ 1600 ignored). After the pack handler stored the atomic,
`markDirty(id)`: `switch (routeOf(id))` — VP → `++voiceParamGeneration_`; every other route is compared
against its tracker in the push step, so nothing else is bumped (Seraphis `processor.cpp:3109-3131` shape).

### 4.3 `process()` order (FR-020, C-3)

```
ScopedDenormalMode
processParameterChanges(...)                 // atomics + pedal points (FR-043 before the guards)
shape guards / not-ready silence path        // unchanged Phase 11 order (processor.cpp:177-202)
   -> on every early return: applyPendingPedalPointsImmediately()  (state only; §4.7)
if (latchReleasePending_.exchange(false, acquire)) latch_.releaseAll(noteOffToEngine)     // Q8 (a)
if (forcePushPending_.exchange(false, acquire))    pushAllSurfaces(Scope::PresetLoad)     // FR-022
pushGlobalParams()                            // polyphony (edge), master gain (unchanged)
pushEngParams()                               // seed, envelope ×7, sub tones ×3, ghost ×2 — trackers
pushCavernParams()                            // 9 CV setters — trackers
pushVoiceParams()                             // if voiceParamGeneration_ != lastApplied: build VP, applyVoiceParams
pushMacroBases()                              // kMbRoutes: if value != lastPushedMb_[i] || !mbValid_: setTargetBase
macros_.setMacros(buildMacroVector())         // MAC (FR-021)
macros_.apply(*engine_);  applyCavernTargets(*cavern_, macros_.computeCavernTargets());  ++macroPushCount_
slice loop (notes + pedal points merged, §4.7)
```

**Ordering note (a deliberate, stated refinement of FR-020's numbering):** the two request consumes sit
*after* the not-ready path, not literally first, so a `process()` issued before `setupProcessing()` cannot
consume (and lose) a request; `processParameterChanges` stays before the guards as Phase 11 requires
(parameter-only calls must still latch atomics). The net effect FR-020 asks for — every push before the
first slice, once per `process()` — is unchanged. This is the Seraphis position
(`processor.cpp:1359-1371`).

### 4.4 Trackers and the MB/VP/ENG/CV pushes

- **MB:** `std::array<float, 39> lastPushedMb_{}` + `bool mbValid_ = false`. For route `i`:
  `v = source(i).load(relaxed)`; `if (!mbValid_ || v != lastPushedMb_[i]) { macros_.setTargetBase(t, v);
  lastPushedMb_[i] = v; }`; then `mbValid_ = true`. `source(i)` is a `const std::atomic<float>*` table
  built in the constructor from the pack members (no allocation, no per-block switch).
  `kOutputSaturationId` is simply one of these 39 (C-2: the saturation control goes *through* the matrix).
- **VP:** `std::uint64_t voiceParamGeneration_ = 0, lastAppliedVpGen_ = kGenerationSentinel`. On mismatch:
  fill a local `Krate::DSP::VoragoVoiceParams` with designated-initialiser-free member writes from the
  atomics (index → enum via the §3.3 tables), `engine_->applyVoiceParams(p)`, record the generation.
- **ENG:** individual trackers `lastSeedIndex_`, `lastEnvMode_`, `lastStageMs_[4]`, `lastReleaseMs_`,
  `lastGrowthS_`, `lastSubToneDb_[3]`, `lastGhostReverse_`, `lastGhostTriggers_` (+ one `engValid_` flag).
  Seed push: `engine_->setSeed(kVoragoSeedValues[i]); cavern_->setSeed(cavernSeedFor(i));` (FR-023 — a live
  reseed on a sounding engine is an audible re-organisation, accepted by Q6, allocation-free:
  `vorago_voice.h:961-967`, `cavern_verb.h:953-959`).
- **CV:** `lastCv_[8]` floats + `lastFreeze_` + `cvValid_`.

The trackers compare the **plain** atomic value (bit-exact float compare is the intent: any change pushes,
an unchanged value never re-pushes → SC-023 (2)).

### 4.5 `setupProcessing()` (FR-023, FR-022)

1. seed from the parameter BEFORE prepare: `i = seedIndex`; `engine_->setSeed(kVoragoSeedValues[i])`;
2. `engine_->prepare(sr, makeVoragoEngineConfig(kMaxBlockSamples))`;
   `cavern_->prepare(sr, makeVoragoCavernConfig(kMaxBlockSamples, cavernSeedFor(i)))`;
3. polyphony push (unchanged);
4. master gain configure / snap arm (unchanged);
5. `latch_.clearWithoutRelease()` (no note can sound across a re-prepare);
6. `lastSeedIndex_ = i` (prepare consumed it), then `pushAllSurfaces(Scope::Reprepared)` **directly**
   (audio thread is stopped here) — this re-installs VP fields and envelope ENG values that
   `VoragoVoice::prepare()` just reset to defaults (§1.2 `:294-297`, §1.3 `:654-683`);
7. `prepared_ = true`.

### 4.6 `pushAllSurfaces(Scope)` (FR-022, C-3)

Invalidates: `mbValid_ = false`, `++voiceParamGeneration_` and `lastAppliedVpGen_ = kGenerationSentinel`,
`engValid_ = false` **except the seed tracker**, `cvValid_ = false`. With `Scope::Reprepared` it then runs
the push functions immediately; with `Scope::PresetLoad` (from `process()`) the normal push step that
follows does it.

**Two trackers are deliberately not invalidated (documented deviation from C-3's literal "every
tracker", §8 decision D-P2):**
- **seed** — prepare consumes the seed (step 1 above) and `setState` delivers a changed seed through the
  ordinary value compare; forcing a re-push of an *unchanged* seed would be a live reseed (an audible
  re-organisation, `vorago_voice.h:961-967`) on every preset load and would break SC-023 (2) and SC-014 (2);
- **polyphony** — its tracker compares against `engine_->getPolyphony()`, which survives prepare
  (`vorago_engine.h:286`); forcing it would falsify Phase 11's edge-trigger counter
  (`setPolyphonyCallCountForTest`, `processor.h:63`).

Every other re-push of an unchanged value is inert by construction (§2.3 early-outs, `applyStage`'s
idempotence guard `vorago_voice.h:664-669`, `setSubToneLevelDb` early-out, `CavernVerb` setters writing the
same shadow).

### 4.7 Sustain latch (C-9, FR-030) — `processor/sustain_latch.h`

```cpp
class SustainLatch {                                   // fixed-size, allocation-free, audio thread only
public:
    void noteOn(std::uint8_t n) noexcept { held_.set(n); latched_.reset(n); }
    /// @return true if the caller must send engine noteOff now; false if latched.
    [[nodiscard]] bool noteOff(std::uint8_t n) noexcept {
        held_.reset(n); if (down_) { latched_.set(n); return false; } return true; }
    template <typename Release> void setPedal(bool down, Release&& release) noexcept {
        if (down_ && !down) { for (n : latched_) if (!held_.test(n)) release(n); latched_.reset(); }
        down_ = down; }
    template <typename Release> void releaseAll(Release&& release) noexcept {   // setActive(false), setState
        for (n : latched_) release(n); latched_.reset(); down_ = false; }
    void clearWithoutRelease() noexcept { held_.reset(); latched_.reset(); down_ = false; }
    [[nodiscard]] bool isDown() const noexcept { return down_; }
private:
    std::bitset<128> held_{}, latched_{};  bool down_ = false;
};
```

Re-strike of a latched note clears its mark and dispatches `noteOn` normally; a later pedal-up skips it
while the key is held (SC-012 (3)). A note-on with velocity 0 is routed as a note-off through the latch.

**Merge into the slice loop:** a second cursor over `pedalPoints_[0..numPedal)` (ascending offsets per the
VST3 queue contract; clamped with the existing `clampOffset`). At each slice start, all due note events are
dispatched, then all due pedal points (**equal offsets: notes first, then pedal**, stated and tested by
SC-012 (2)'s distinct-offset cases plus a same-offset case). `sliceEnd = min(nextNoteOffset,
nextPedalOffset, cursor + kMaxBlockSamples, total)`. Down = `value >= 0.5`. The pedal's plain value also
goes to `globalParams_.sustainPedal` (display only).

**Other paths:** `setActive(false)`: `latch_.releaseAll(noteOff)` **before** `engine_->silence()` (immediate;
off the render path, `vorago_engine.h:447`). `setState()`: stores `sustainPedal = 0`, `channelPressure = 0`
and raises `latchReleasePending_.store(true, release)`; it makes **no** engine call (Q8 (a)). Early-return
`process()` paths apply pending pedal points to the latch state only when `prepared_` (a pedal-up there
releases through `engine_->noteOff`, which is valid on a prepared engine with no audio rendered).

### 4.8 MAC route (FR-021, FR-032)

```cpp
VoragoMacroValues m{};  // then each field from its atomic via macroField(p, i)
m.pressure = std::clamp(macroParams_.pressure.load() + globalParams_.channelPressure.load(), 0.0f, 1.0f);
```
With pressure 0 the sum is `knob + 0.0f`, which is exactly `knob` in IEEE arithmetic, so the vector equals
the atomics bit-for-bit (SC-013 (1)). No smoother up front (Q2 ruling); SC-011 decides.

### 4.9 State v2 (C-7, FR-040, FR-045)

```
int32 version (=2)                                                            4
[v1] float masterGain, int32 polyphony                                        8
     12 × float macro                                                        48    (v1 total 60, unchanged)
[v2] int32 seedIndex, float outputSaturation                                  8
     cloud     7F                                                            28
     noise     3F + 4I(model) + 4I(type) + 4F(fund) + 4F(spread) + 4F(fb)    92    (field order = ID order)
     resonance 3F + 1I                                                       16
     ecology   2F + 6I                                                       32
     sub       5F                                                            20
     smear 3F 12 · events 1F 4 · ecosystem 1F 4
     body      4F + 2I                                                       24
     space     15F + 1I(freeze)                                              64
     envelope  1I + 6F                                                       28
     bloom 2F 8 · ghost 3F + 1I 16 · life 3F 12
TOTAL                                                                       428 bytes (kStateV2Bytes)
```

`setState`: read version; `> 2` → `kResultFalse`, nothing changed; `≤ 1` → v1 block only, every Phase 12
field is set to its registered default (spec C-7; compliance fix 2026-09-26 — a v1 load into a dirty
instance resets every v2 field to default: the default-constructed packs' v2 tail is serialized and loaded
through the same chain); `== 2` → v1 block then the chain `loadGlobalParamsV2Ext && loadCloud && … && loadLife`
(short-circuit = EOF-safe). Then `sustainPedal = channelPressure = 0`, raise both requests. `getState`
writes the same order. The controller's `setComponentState` mirrors it with the `…ToController` functions.

### 4.10 Test seams (all `const` or test-only, never read by `process()` logic)

`macroPushCountForTest()` (SC-007), `macrosForTest()` (const `VoragoMacroMatrix&`), `packsForTest()`
accessors, `latchForTest()`, and FR-053's
`namespace detail { struct VoragoMasterGainSmootherBypassProbe; }` declared a `friend` of `Processor` with
a private `bool masterGainSnapProbe_ = false;` read in `pushGlobalParams`:
`if (snapGainPending_ || masterGainSnapProbe_) masterGain_.snapTo(gain);` — inert in the shipped plugin
(nothing but the probe, defined only in the test TU, can set it).

---

## 5. Controller (`plugins/vorago/src/controller/`)

- `class Controller : public EditControllerEx1, public VST3EditorDelegate, public Steinberg::Vst::IMidiMapping`;
  `DEFINE_INTERFACES DEF_INTERFACE(IMidiMapping) END_DEFINE_INTERFACES(EditControllerEx1)` +
  `DELEGATE_REFCOUNT(EditControllerEx1)` (Disrumpo shape, `controller.h:218-224`; verified by build and by
  a `queryInterface(IMidiMapping::iid)` assertion in SC-013 (5)). Header comment `:9` rewritten.
- `getMidiControllerAssignment(busIndex, channel, cc, id)`: `busIndex != 0 → kResultFalse`;
  `cc == kCtrlSustainOnOff (64)` → `kSustainPedalId`; `cc == kAfterTouch (128)` → `kChannelPressureId`;
  else `kResultFalse` (`ivstmidicontrollers.h:52, :104`). No `INoteExpressionController`.
- `initialize`: register global, macro, then the 14 packs in band order (108).
- `getParamStringByValue`: chain of `format…Param`, then the base (StringLists format themselves).
- `setComponentState`: version gate `> kCurrentStateVersion → kResultFalse`; v1/v2 per §4.9.

---

## 6. Test plan

**Conventions for every render criterion:** 48 kHz, 512-sample blocks unless stated, note C2 (36) vel 100,
both channels, output-domain latency 3072 (FR-024). "Same binary" = both arms rendered in one test
process. No bit-exact golden is checked in; `render_fingerprint.h`'s `compareFingerprints` is used only as
a `WARN`-level secondary signal where noted. Non-finite checks use `Krate::DSP::detail::isFinite`. Every
precondition that fails lengthens the script, never loosens a threshold.

### 6.1 Shared plugin-test helper (new): `plugins/vorago/tests/phase11_reference_chain.h`

`renderPhase11ReferenceChain(const RefChainSpec&) → {L, R}`: a heap `VoragoEngine` seeded `kEngineSeed`
then prepared with `makeVoragoEngineConfig(kMaxBlockSamples)`, polyphony 4 (or the spec's value), a heap
`CavernVerb` prepared with `makeVoragoCavernConfig(kMaxBlockSamples)`; per block: optional hook (to
apply a matrix / a cavern setter by hand), `VoragoMacroMatrix{}.apply(engine)` +
`applyCavernTargets(cavern, computeCavernTargets())` once per block, then engine → cavern → gain 1.0 →
`processOutputStage`, with notes at the scripted absolute sample positions split into the same partition.
This is the SC-002 / SC-004 / SC-005 / SC-014 reference.

### 6.2 `dsp_systems_tests` (new TUs; existing Vorago TUs unedited — FR-007)

`dsp/tests/unit/systems/vorago_param_surface_test.cpp` (in the `-fno-fast-math` list; injects NaN):

| SC / FR | TEST_CASE | Strategy |
|---|---|---|
| SC-006, FR-001/002 | `VoragoMacro_TargetBaseOverride` | (1) 8 seeded non-neutral macro vectors (seed 12012): a matrix never touched by `setTargetBase` vs a second one → every Voice/Engine getter on two identically prepared engines and every `computeCavernTargets()` field **exactly equal**; (2) for each of `CloudRichness @ 0.45 + Density`, `NoiseLevelDb @ −24 + Density`, `CavernSize @ 0.30 + Depth`, `OutputSaturation @ 0.05 + Pressure`: 11 macro steps 0..1, getter (or cavern field) == `clamp(override + Σ row contributions)` computed independently from `kRows` + `applyModCurve`, and strictly increasing; (3) `CloudRichness @ 1.0 + Density`: non-decreasing and `getTargetBase == 1.0`; (4) NaN / ±Inf bit patterns (volatile-built) and `static_cast<VoragoMacroTarget>(39)`/`(255)` leave `getTargetBase` unchanged; `resetTargetBases()` restores every literal for all 39 targets |
| SC-019 | `VoragoEngine_ApplyVoiceParamsDefaultIsNoOp` `[long]` | two engines (poly 4, seed 1, note 36 at 0); arm B calls `applyVoiceParams(VoragoVoiceParams{})` after prepare; 60 s render; max-abs ≤ 1e-6 per channel. Short per-push twin (4 s, same bound) untagged |
| SC-019 | `VoragoEngine_NewSettersDefaultInert` `[long]` | as above with `setSubToneLevelDb(t, kDefaultToneLevelDb[t])` ×3, `setGhostReverseProbability(0)`, `setGhostEventTriggers(false)`; 60 s; ≤ 1e-6; 4 s twin |
| SC-022 | `VoragoEngine_NewSettersSurvivePrepare` | (1) set −12/−20/−36 dB, reverse 0.4, triggers on; re-`prepare()` same config; render one block; engine getters AND `subharmonic().getToneLevelDb(t)` (== base + 0 offset) AND `atmosphere().getGrainReverseProbability()` return the set values (exact); (2) triggers on, `setGhostPeakLevel(1.0)`, render until `isGhostTriggerLatchHigh()` (drive the ghost request with a fast-attack held note and `setEventRateScale` via the matrix override so an event fires within ≤ 120 s; assert it did), `setGhostEventTriggers(false)` → latch false after next block; `true` again with request still high → latch true after ≤ one 64-sample chunk |
| SC-023 (1), FR-004/005 | `VoragoEngine_RepeatedBroadcastIsInert` `[long]` | poly 6, six held notes, 10 s; arm A: non-default VP `p` (every field changed) + `setSubToneLevelDb` once; arm B: same calls every 512-sample block; max-abs ≤ 1e-5. Positive control arm C: slot 2 model toggled Direct/FilteredWind every block → RMS diff vs A > 1e-3 |
| FR-004 unit | `VoragoVoice_Phase12Forwarders` | per forwarder: reaches the owner getter; NaN rejected (comb tuning/feedback: owner value unchanged, not the default); comb-feedback latch: first push at 0.75 on slot 3 then model → Direct keeps 0.75 (`getCombFeedback`); anchor/filter early-out observable via unchanged getter and no dirty side-effect (render identity over 1 s, ≤ 1e-6) |

`dsp/tests/unit/systems/vorago_macro_retune_probe_test.cpp`: `VoragoMacro_Phase12RetuneProbe`
`[.probe][vorago]` — hidden, never in CI; the FR-060 instrument described in §2.6 (reuses
`tests/test_helpers/vorago_fixtures.h` helpers; re-implements the 20-line `meanOctaveOffset` locally
because the original is TU-local). Prints only; asserts nothing but finiteness.

SC-021 re-uses the **unedited** `VoragoMacro_SweepAxes` (`vorago_macro_test.cpp:1211`) and
`VoragoComposed_DepthMacroAxis` (`dsp_effects_tests`), and `VoragoEngine_CpuBudget` via
`node tools/run-cpu-tests.js dsp_systems_tests` alone.

### 6.3 `vorago_tests`

| SC / FR | File · TEST_CASE | Strategy |
|---|---|---|
| SC-002 | `integration/param_surface_test.cpp` · `Vorago_Phase12DefaultsMatchPhase11Chain` | fresh processor at registered defaults, note 36 vel 100 at 0, 8 s, 512 blocks vs §6.1 reference; max-abs ≤ 1e-5 both channels; precondition reference peak over [3072, end) ≥ 1e-4 |
| SC-003 | same TU · `Vorago_RegisteredDefaultsMatchEngine` | controller `getParameterInfo(i).defaultNormalizedValue` → pack denormalize → compare to a **reference that no Phase 12 push has touched**. The plugin's own prepared chain is NOT the reference: after `pushAllSurfaces()` it holds whatever the registered default denormalizes to (`setTargetBase(v)` → `getTargetBase == v`, §2.1/§4.4; VP via `applyVoiceParams`; ENG via the trackers), so a wrong default would be read back as itself — and a wrong noise-type default on slots 0/1/3 even renders identically (`noise_organism.h:1226-1240`, C-6), so SC-002 cannot catch it either. References, all built **without constructing a `Processor`**: **MB (39)** → a fresh `VoragoMacroMatrix{}.getTargetBase(t)` (never overridden, so the `kRows` literal via `literalBaseFor`) **and**, for the 32 Voice/Engine targets, the owner getter on the §6.1 reference engine (prepared; only `VoragoMacroMatrix{}.apply`; no `applyVoiceParams`, no Phase 12 setter); **VP (31)** → the matching `VoragoVoiceParams{}` field **and** the §6.1 reference engine's voice getters on every `i < kMaxVoices`; the four type rows 320–323 → the literal `NoiseType::Brown` (C-4, `noise_organism.h:1138`) **and** the Direct probe applied to the reference engine; **ENG (14)** → constants, not chain state: `SubharmonicEngine::kDefaultToneLevelDb[t]` ×3 (`vorago_engine.h:1156`), `VoragoVoice::kDefaultStageTimesMs[0..3]` (`vorago_voice.h:322`), `VoragoVoice::kDefaultReleaseMs` (`:324`), `VoragoVoice::kDefaultGrowthDurationSeconds` (`:332`), envelope mode `Standard`, ghost reverse probability 0 and triggers off, seed index 0 ↔ `kVoragoSeedValues[0] == 1u` (== Phase 11 `kEngineSeed`), polyphony 4 (Phase 11 default, `global_params.h:59`); **freeze** → `!isFrozen()` on the §6.1 reference `CavernVerb`; **MAC (12)** → `VoragoMacroValues{}`. Relative 1e-6 / exact for discrete; prints and asserts row count `== 97`. A second, separately named section (push integrity, not the default proof) reads the same references back from a processor after `setupProcessing` + one `process()` |
| SC-004 | same TU · `Vorago_EveryRouteReachesTheChain` | poly 6, six notes; per ID one change (comb fundamental 440 Hz; others at 0.8 of range, discrete to a non-default index), 4 800 samples, read back per spec (MB on `i < getPolyphony()`, VP on all six slots, ENG via the chain, noise type via the Direct probe, envelope via slot-0 getters and every slot); cavern rows (16) via the 4 s render vs the §6.1 reference with the cavern setter applied by hand: max-abs ≤ 1e-6, non-vacuity RMS vs default-cavern reference > 1e-3 over [3072, end), precondition RMS ≥ 1e-4; freeze also via `isFrozen()`; **re-prepare arm (R-3, FR-022, edge case "Sample-rate changes")** — a separate `SECTION("survives re-prepare")`: set **every persisted ID** non-default (the per-ID pass's values), render one block, then `setActive(false)` → `setupProcessing()` at **44.1 kHz** → `setActive(true)`, re-strike the six notes, render ≥ 100 ms at that rate (4 410 samples) and re-run the **full** SC-004 read-back (helper shared with SC-009); repeat at **96 kHz** (9 600 samples). Only a SECOND `setupProcessing()` exercises `pushAllSurfaces(Scope::Reprepared)` — the first pushes VP regardless because `lastAppliedVpGen_` starts at the sentinel (§4.4) — and it runs `VoragoVoice::prepare()` step 5, which resets every VP field and envelope time (§1.3 `:654-683`), so this arm fails if §4.6 omits the VP or ENG invalidation. The builder confirms that once by local mutation (drop the `++voiceParamGeneration_` / `engValid_ = false` lines, observe the arm fail, restore) and records it in the build log; comb fundamental 440 Hz is below the 44.1 kHz component ceiling |
| SC-005, FR-050 | `integration/param_flow_test.cpp` (existing `MacrosAreInert` section **inverted**, renamed `MacrosAreLive`) + `param_surface_test.cpp` · `Vorago_MacrosDriveTheMatrix` | (1) 13 arms (each macro at 1.0; Gravity also at 0.0): every Voice/Engine target getter equals a second engine driven by a directly constructed `VoragoMacroMatrix` (exact); (2) render vs the §6.1 reference whose hook applies that matrix per block: ≤ 1e-6; (3) all-at-1 vs defaults RMS diff > 1e-3 |
| SC-007 | `param_surface_test.cpp` · `Vorago_MacroPushOncePerProcess` | counter == 1 per `process()` for 0/1/64/1024-event blocks; block-size invariance script per spec (Phase 11 SC-008 (1) script + pedal points at non-multiple offsets, macros 0.7 / Gravity 0.8 + one MB (201 → −6 dB/oct) and one VP (206 → 0.5) at sample 0); partitions {1, 7, 64, 65, 512, 2048, 4096} vs 512: max-abs ≤ 1e-5; precondition peak ≥ 1e-4; `lastSliceCountForTest()` == 2 for an event-free 4096 block (unchanged by any R-5 remedy: remedies step once per `process()` in the pre-slice push step and never add slices; a remedy smoother `snapTo`s on an invalid tracker, so this sample-0 script sees no ramp); `compareFingerprints` WARN |
| SC-008 | `unit/state_v2_test.cpp` (fast-math-off) · `Vorago_StateRoundTripV2` | (1) 106 IDs set via seeded normalized values (seed 12008) → `getState` (428 B) → fresh `setState` → atomics bit-identical (memcmp of float bits), controller normalized within 1e-9, held-note render (4 s) ≤ 1e-5; (2) 60-byte v1 stream built by the v1 writer shape → 14 v1 fields restored, 92 others at registered defaults; (3) truncation at every offset 0..427 → no crash, prefix fields restored, suffix unchanged (two instances: fresh and pre-dirtied); (4) version 3 → `kResultFalse`, no atomic changed; (5) NaN bit pattern in each float field in turn → that field unchanged, the rest loaded; (6) stream length == 428 and a stream with sustain down/pressure 0.7 in the atomics serializes identically to one with both 0 |
| — (Phase 11 updates) | `unit/state_roundtrip_test.cpp`, `unit/param_denorm_test.cpp` | 60 → `kStateV2Bytes` (428), "future version" 2 → `kCurrentStateVersion + 1`; parameter count 14 → 108 (v1 decode arm moves to SC-008 (2)) |
| SC-009 | `param_surface_test.cpp` · `Vorago_SetStateAfterPrepareReachesDsp` | prepared, rendering processor; `setState` of a stream with every persisted ID non-default; one block; every SC-004 read-back holds (shared read-back helper) |
| SC-010 | `unit/param_table_test.cpp` (fast-math-off) · `Vorago_ParamInputHygiene` | unregistered IDs 2..99 unused (e.g. 6, 99), 112, 207, 399, 516, 613, 703, 801, 901, 1006, 1116, 1207, 1302, 1404, 1503, 1599, 1600 → every atomic unchanged (snapshot compare); normalized NaN/±Inf/−0.5/1.5 on each of 108 IDs → atomics finite, inside plain range |
| SC-011 | `integration/continuity_test.cpp` `[long]` · `Vorago_ParameterStepsAreContinuous` | **scope enumerated in §6.4** (85 continuous IDs swept in 64 equal normalized steps min→max; 12 discrete destination-smoothed IDs swept with 64 seeded index changes; 11 IDs clause 4 only — 108 total, checked by a `static_assert` over the SC-011 column of the checked-in `param_table_expected.h`); 1 s warm-up, steps 125 ms apart; statistic `maxDeltaInWindow` (`vorago_fixtures.h:381`) over ±10 ms at `step + 3072`; 64 reference windows midway; bound `max(test) ≤ 1.5 × max(ref)`; clause 4 for every ID (finite, peak ≤ −0.3 dBFS linear); positive controls (a) injected 2× step exceeds bound, (b) `VoragoMasterGainSmootherBypassProbe` engaged → master-gain run fails clause 3. Failing ID → remedy rule (§7 R-5), never a looser bound, never an exemption |
| SC-012 | `integration/sustain_test.cpp` · `Vorago_SustainLatch` | clauses (1)–(5) exactly as the spec, reading `engineForTest()->getVoiceState(slot)` after the block; (2) adds the same-offset case (note-off and pedal-down at 150 → notes-first rule → `Releasing`); (5) 10 000 random events under `AllocationScope` → 0 allocations |
| SC-013 | `integration/channel_pressure_test.cpp` · `Vorago_ChannelPressure` | (1) `macrosForTest().getMacros()` equals atomics exactly; (2)(3)(4) target read-backs equal the knob-only arms (≤ 1e-6); other macros' targets unaffected; (5) `IMidiMapping` via `queryInterface` → 64 → 4, 128 → 5, CC1 → `kResultFalse`, bus 1 → `kResultFalse`; (6) covered by SC-011's scope (ID 5 is in it); (7) **FR-045 reset:** channel pressure 0.7 sent and the pedal down (one note latched), then `setState(v2 stream)` whose Pressure knob is 0.25 → immediately after `setState` both the `sustainPedal` and `channelPressure` atomics read exactly 0 (`packsForTest()`); after one `process()` `macrosForTest().getMacros().pressure == 0.25f` exactly (no stale pressure summed in, FR-021) and the latched note reads `Releasing` (SC-012 (4)) |
| SC-014 | `integration/seed_test.cpp` · `Vorago_SeedParameter` + `Vorago_SeedTableSpread` `[long]` | (2) same index twice ≤ 1e-5; (4) change while silent then note vs fresh instance at that index ≤ 1e-5; (5) change mid-note: 0 allocations, finite, peak ≤ ceiling; spread case: 16 × 30 s held-note renders, all 120 pairs RMS diff > 1e-3 over [3072, end) — failure → re-pick table entry (§3.3.2) |
| SC-015 | `integration/automation_rt_test.cpp` · `Vorago_FullSurfaceAutomationAllocFree` | 2 000 blocks × 512, every registered ID a seeded random normalized value each block (seed 12015), notes + pedal; `AllocationScope` around `process()` → 0; finite; peak ≤ ceiling; `getNonFiniteRecoveryCount() == 0` |
| SC-016 | `integration/processor_cpu_test.cpp` (existing `Vorago_ProcessorCpu` `[.perf][performance]`) | P/D ≤ 1.05 at defaults with all routes wired; second arm (201 + 206 automated every block) WARN-recorded; arm E vs Phase 11's 1.79691e+07 ns recorded; run only via `node tools/run-cpu-tests.js vorago_tests`, alone; **remedy arm (R-5):** if any SC-011 remedy lands, (i) the gated P/D ≤ 1.05 quiescent arm includes every remedy smoother (settled) and fan-out, and (ii) an added arm automates every remedied ID every block (driving its per-block smoother step and, for VP, its per-field fan-out over all `kMaxVoices`) and records its P/D; a figure above 1.05 there is surfaced to the user, never absorbed. If no remedy lands the log records "no R-5 remedy — arm N/A" |
| SC-017 | `param_surface_test.cpp` · `Vorago_LatencyIndependentOfParameters` | 44.1/48/96 kHz × every discrete value × every continuous min/max → `getLatencySamples() == 3072` |
| SC-018 | `unit/param_table_test.cpp` · `Vorago_ParameterInfoTable` + checked-in `unit/param_table_expected.h` | count == 108; per ID title, units, stepCount, flags, default (1e-9) equal the table; 14 Phase 11 rows byte-for-byte unchanged; `getParamStringByValue` non-empty at 0/0.5/1; taper: denormalized n = 0.5 equals §3.3.1's midpoint for log/olog IDs and the linear midpoint otherwise (1e-9 relative); materials index n → enum n for n < 11 |
| SC-020 | `integration/soak_test.cpp` `[long]` · `Vorago_RandomSurfaceSoak` | 5 seeds (12020..12024) × 60 s, notes C2+G2, randomized set re-drawn every 5 s excluding IDs 0, 4, 5, 500, 1003, 1105; finite; peak ≤ ceiling; last-10 s RMS ≥ `R_default − 60 dB` (default render in the same test, precondition ≥ 1e-4) |
| SC-023 (2) | `param_surface_test.cpp` · `Vorago_HostResendsEveryParameter` | 10 s held note; arm B re-sends all 108 IDs at current normalized value every block; ≤ 1e-5 vs arm A |
| SC-001 | gates | build (0 warnings) + suites + pluginval + clang-tidy + lints (§7) |
| FR-052 | review | leaf `CLAUDE.md` diff checked in the compliance pass |

`[long]` tagging follows the root rule (> ~15 s, failure not toolchain-specific): SC-011, SC-014 spread,
SC-019 60 s arms, SC-020, SC-023 (1). SC-008, SC-010 (state/NaN sentinels) are never `[long]`.

### 6.4 SC-011 scope (normative; mirrored as the SC-011 column of `unit/param_table_expected.h`)

Source of each classification: C-6's "Stepped" column (spec §C-6 table) and SC-011's scope sentence
("every continuous, non-stepped ID of C-6 plus master gain"; "stepped IDs (C-6 column 'Stepped', and
seed) carry clause 4 only").

**A. Continuous sweep, clauses 1–4 (85 IDs).** 64 equal steps in **normalized** space from 0 to 1 (so a
log/olog ID steps along its registered taper), 125 ms apart:
0 master gain; 3 output saturation; 5 channel pressure (SC-013 (6)); **100–111 macros** (D-P9);
200–206; 300–302; 330–333; 340–343; 350–353; 400–402; 500–501; 600–601; 610–612; 700–702; 800; 900;
1000–1003; 1100–1114; 1201–1206; 1300–1301; 1400–1401; **1402 ghost reverse probability** (its C-6
Stepped cell "birth-time only" is not "yes": a continuous Range whose change applies at grain birth, so it
is non-stepped and fully measured); 1500–1502.

**B. Discrete step sweep, clauses 1–4 (12 IDs).** Discrete lists whose C-6 Stepped cell is not "yes" but
names a destination mechanism that claims continuity, so they are non-stepped and measured (D-P9):
**403 anchor mode** (C-6: "measured (SC-011)"); 310–313 noise model ("ducked"); 320–323 noise type
("ducked"); 1004–1005 body materials ("crossfaded by `ContinuousBody`"); **1115 freeze** ("latch
(`aether_reverb` 50 ms)"). Geometry is identical to group A — 1 s warm-up at the default, 64 steps
125 ms apart, the same ±10 ms output-domain test windows, the same 64 midway reference windows, the same
1.5× bound — except that step k sets index `kDiscreteStepSeq[k]`: a checked-in seeded (seed 12011)
sequence of 64 indices in `[0, n)` with no index equal to its predecessor, so every step is a real
change (for n = 2 it alternates; for 403 the sequence is required to contain all six ordered pairs of its
3 values). For 320–323 the slot's model (310+k) is set to `Direct` at sample 0, before warm-up, because
the type is audible only on a Direct slot (C-6, `noise_organism.h:1226-1240`); otherwise the sweep would
be vacuous. A group-B ID that fails has no `OnePoleSmoother` form: stop and surface with the figures
(R-5); it is never exempted.

**C. Clause 4 only (11 IDs).** 1 polyphony (a Phase 11 ID absent from C-6 and not master gain, so outside
SC-011's clause 1–3 scope sentence); 2 seed (C-6 "yes", and named by SC-011); **4 sustain pedal** (C-6
Stepped "—": a hidden Local on/off gate, ≥ 0.5 = down, whose audible consequence is the envelope release
that SC-012 measures — not a continuous control); 510–515 loop filter mode (C-6 "yes",
`feedback_ecology.h:1032-1034`); 1200 envelope mode ("yes"); 1403 ghost event triggers ("yes"). Each is
still stepped (group-B sequence form) and asserted finite with peak ≤ ceiling.

Totals: 85 + 12 + 11 = 108 = every registered ID; the `static_assert` in `param_table_expected.h`
checks that each of the 108 rows carries exactly one of A / B / C.

---

## 7. Build integration, gates, risks

### 7.1 CMake

- `dsp/tests/CMakeLists.txt`: add `unit/systems/vorago_param_surface_test.cpp` and
  `unit/systems/vorago_macro_retune_probe_test.cpp` to the enumerated `dsp_systems_tests` list with a Phase 12
  comment block (the `:492-536` style); add `vorago_param_surface_test.cpp` to the `-fno-fast-math
  -fno-finite-math-only` source list (NaN injection). The probe TU stays out of it.
- `plugins/vorago/tests/CMakeLists.txt`: add `unit/state_v2_test.cpp`, `unit/param_table_test.cpp`,
  `integration/param_surface_test.cpp`, `integration/sustain_test.cpp`,
  `integration/channel_pressure_test.cpp`, `integration/seed_test.cpp`, `integration/automation_rt_test.cpp`,
  `integration/continuity_test.cpp`, `integration/soak_test.cpp`; add `state_v2_test.cpp`,
  `param_table_test.cpp`, `automation_rt_test.cpp`, `continuity_test.cpp`, `soak_test.cpp` to the fast-math-off
  list; `processor_cpu_test.cpp` stays out (`:81-82`).
- `plugins/vorago/CMakeLists.txt`: list the 17 new headers.

### 7.2 Targets and gates (SC-001)

```
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_systems_tests dsp_effects_tests vorago_tests Vorago
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "~[.perf]~[performance]~[long]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_effects_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/vorago_tests.exe 2>&1 | tail -5         (then [long] alone)
node tools/run-cpu-tests.js dsp_systems_tests      # SC-021 CPU gate, alone
node tools/run-cpu-tests.js vorago_tests           # SC-016, alone
tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Vorago.vst3
./tools/run-clang-tidy.ps1 -Target vorago -BuildDir build/windows-ninja ; … -Target dsp
node tools/check-portability.js ; lint-odr.js ; lint-layers.js ; lint-nonfinite-symbols.js ;
     lint-float-bit-goldens.js ; lint-plugin-roster.js
```

Slow suites are captured to a log under `specs/vorago-phase12-parameters/artifacts/` and read from the log
(never re-run for output).

### 7.3 Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | Gravity/Pressure retune structurally unreachable (§2.6) | gate P-0: D-1 + probe run before any Gravity/Pressure SC-021 closure task is generated; measured table to the user; SC-021 (Gravity, Pressure) BLOCKED on a spec amendment of Q1/FR-060/SC-021 until the ruling; never a threshold move; rest of the phase independent |
| R-2 | Comb-feedback early-out skipping the first latch push (C-4 violated silently) | latch-aware early-out (§2.3); FR-004 unit case checks model change after default push |
| R-3 | Re-prepare wiping VP / envelope values (voice prepare re-authors step 5, §1.3 `:654-683`) | `pushAllSurfaces(Scope::Reprepared)` in `setupProcessing` (§4.5 step 6); verified by the **re-prepare arm of `Vorago_EveryRouteReachesTheChain`** (§6.3 SC-004: every persisted ID non-default, second `setupProcessing()` at 44.1 kHz then 96 kHz, full read-back, mutation-checked once) plus SC-009 for the `setState` path |
| R-4 | `setState` racing `process()` | `setState` writes atomics only + two release-store flags; all DSP calls happen in `process()` (Seraphis `processor.cpp:1885-1890`) |
| R-5 | SC-011 finds an unsmoothed destination (e.g. CV density/dimensionality forwarded to `AetherReverb` unsmoothed, cloud tilt stepping amplitudes per recompute) | **Remedy rule (C-3, SC-011: never an exemption, never a looser bound), shaped to the designed loop.** A processor-side `OnePoleSmoother` (20 ms, the `kMasterGainSmoothMs` family) on the failing ID's **plain value**, stepped **once per `process()` inside the pre-slice push step** (§4.3) with `advanceSamples(numSamples)` (`smoother.h:243`, the exact N-sample formula) and pushed once with the smoothed value. No 64-sample chunking and no push inside the slice loop is added, so SC-007's slice count (`== 2` for an event-free 4096 block) and partition script are untouched. Per route: **MB** → `setTargetBase(t, smoothed)` once, before the single `apply()` (P-10 permits one push + one `apply()` per `process()`; this adopts the per-block smoothed base, D-P8, replacing the earlier MB stop-and-surface); **CV / ENG** → the ID's own setter with the smoothed value; **VP** (continuous VP IDs 205, 206, 330–353) → a **single per-field engine fan-out** added for that field only, looping all `kMaxVoices` (the `setEnvelopeMode` shape, `vorago_engine.h:719`; there is deliberately no non-const `getVoice`, `:715`), **never** a `voiceParamGeneration_` bump through `applyVoiceParams` (≈ 30 setters × 6 voices, and each comb push that passes its shadow runs `applySlotConfiguration`, re-enabling all generator types, `noise_organism.h:1931-1945`). The smoother `snapTo`s whenever its tracker is invalid (first push, `pushAllSurfaces`), so preset loads, SC-002/SC-003 and SC-007's sample-0 script see no ramp. Each landed remedy is re-measured under SC-011 and costed by SC-016's remedy arm. If block-rate stepping (10.7 ms at 512 / 48 kHz) still fails SC-011 for an ID, or a group-B discrete ID (§6.4) fails, **stop and surface** with the figures — finer slicing would change SC-007 and needs a spec decision |
| R-6 | Live reseed (seed change mid-note, Q6) allocation or non-finite | `VoragoVoice::setSeed`/`EcosystemEngine::setSeed`/`CavernVerb::reseed` are allocation-free by inspection (§1.3, §1.4); SC-014 (5) and SC-015 measure it |
| R-7 | Fast-math NaN folding in tests / loaders | all loaders and hygiene use `detail::isFinite`; NaN-injecting TUs listed fast-math-off; volatile bit-pattern construction (memory `reference_fastmath_nan_in_tests`) |
| R-8 | Narrowing in brace init (Clang) | enum arrays built with explicit enumerators; `static_cast` for every ID-to-int32 conversion (`registerMacroParams` precedent `macro_params.h:147-149`) |
| R-9 | Denormals from new smoothers / ramps | processor already runs `ScopedDenormalMode` first (`processor.cpp:171`); no new recursive filter in DSP |
| R-10 | `sizeof(Processor)` 64 KiB bound | additions ≈ 94 atomics (~400 B) + trackers (~300 B) + pedal points (1 KiB) + bitsets; the static_assert (`processor.h:115`) is the check |
| R-11 | `kVoiceSizeBound` / `kEngineSizeBound` | four small per-voice arrays (~40 B), engine ~20 B; both static_asserts are the check |
| R-12 | Host-cache hazard for `IMidiMapping` | none: Vorago is unreleased (leaf `CLAUDE.md` decision 1) — lands before 1.0 |
| R-13 | Macro retune moving Phase 10 CPU gate | rows are table arithmetic (~1 ns each); SC-001b re-run alone |
| R-14 | MSVC-only green | `check-portability.js`; g++/clang syntax check via WSL for the new headers (designated initializers, `std::bitset`, `constexpr` scans) |

---

## 8. Decisions made by this plan, and open questions

**Decisions (the plan owns these; each is traceable):**

- **D-P1 Ghost setter survival:** a per-value "set" flag; `prepare()` uses the config only until the setter
  is first called (§2.5). Keeps Phase 10a config-driven tests unchanged and satisfies SC-022 (1).
- **D-P2 `pushAllSurfaces` exclusions:** the seed and polyphony trackers are not invalidated (§4.6). This
  narrows C-3's literal "every tracker"; both are value-compared against state that survives prepare, and
  forcing them would cause an audible reseed per preset load (breaking SC-014 (2) / SC-023 (2)) or falsify
  Phase 11's polyphony edge-trigger counter.
- **D-P3 Sub level offset range `[−24, +24]` dB:** the engine setter has no clamp (§1.2); the range keeps the
  default at the exact midpoint (n₀ = 0.5) and lets every tone reach its +6 dB component ceiling.
- **D-P4 ID 1114 damper rate:** C-6 marks it `log` but it is zero-floored `[0, 1]`; offset-log with ε = 0.01
  (§3.3.1). FR-013's enumerated zero-floored list omits it; the rule it states is applied.
- **D-P5 Early-size range `[80, 300]` ms:** the prepared config's ceiling (`maxEarlySeconds 0.30`), not the
  600 ms absolute, so the whole knob is audible.
- **D-P6 Pedal vs note at equal offsets:** notes first, then pedal (§4.7).
- **D-P7 Request consumption after the not-ready guard** (§4.3).
- **D-P8 SC-011 remedy mechanism = block-rate smoothing in the pre-slice push step** (R-5). Spec C-3 /
  SC-011 require a processor-side smoother for any failing ID and forbid exemptions; this covers the 39
  MB IDs too, whose smoothed plain value is pushed through `setTargetBase` once per `process()` before
  the single `apply()` — inside P-10. VP remedies use per-field engine fan-outs, not `applyVoiceParams`.
  Remedies are contingent: none is written unless SC-011 measures a failure.
- **D-P9 SC-011 scope reading** (§6.4). (a) The 12 macros (100–111) are measured in full although C-6's
  table does not list them: they become live in Phase 12, channel pressure (in scope by Q2) drives the
  same Pressure path, and including them only adds measurement. (b) Discrete IDs whose C-6 Stepped cell
  names a smoothing mechanism ("ducked", "crossfaded", "latch", "measured") are treated as non-stepped
  and measured with a discrete step sweep; only "yes" (plus seed, polyphony, and the pedal's "—") is
  clause-4-only. Both readings are stricter than the minimal one; neither loosens a bound.

**Open questions for the user — ALL RESOLVED 2026-09-24 (spec *Clarifications*, plan-stage session R-1..R-4):
1 → R-1 (iii) new target per unreachable axis, ruled in advance of the probe; 2 → R-2 confirmed;
3 → R-3 confirmed ±24 dB; 4 → R-4 confirmed both. The original questions follow for the record.**

1. **FR-060 Gravity / Pressure.** If the probe confirms §2.6's prediction (Gravity ≤ ~21 % against 30 %;
   Pressure ≪ 3 dB) through every admissible `kRows` row on the 39 existing targets, which remedy outside
   ruling (b) do you want? (i) re-record both FAILED (ruling (a), rejected earlier); (ii) a Vorago-voice
   voicing change — e.g. set the resonance network's keyed ratio table to near-integer ratios in
   `VoragoVoice::prepare()` for Gravity, which is inert only while gravity is exactly 0 and therefore changes
   the default sound through the breathing lane; (iii) a spec amendment adding a 40th target (e.g. an
   `OutputDriveDb` engine target for Pressure, a peak wander-depth target for Gravity); (iv) defer to
   Phase 14 listening with a roadmap edit. Mass proceeds with its concrete row regardless. **This question
   is asked with the P-0 probe table in hand (measured, not predicted), and its answer is written back into
   spec Q1 / FR-060 / SC-021 before any Gravity/Pressure task is generated.**
2. **Confirm D-P2** (seed and polyphony trackers survive `pushAllSurfaces`), since it narrows C-3's wording.
3. **Confirm D-P3** (sub level offset range ±24 dB) — the only registered plain range the spec left without
   an owner clamp to transcribe.
4. **Confirm D-P8 / D-P9** — the SC-011 remedy is block-rate (once per `process()`, including MB via
   `setTargetBase` before `apply()`), and SC-011's scope includes the macros and the destination-smoothed
   discrete IDs (§6.4).

---

## 9. Implementation order

1. D-1 matrix override + `VoragoMacro_TargetBaseOverride` (failing first) → green; FR-002 identity via the
   unedited `vorago_macro_test.cpp` cases.
2. **Gate P-0:** FR-060 probe TU; run it alone; log the table; present it to the user and amend spec
   Q1/FR-060/SC-021 to the ruling (T-P0c blocks every Gravity/Pressure task). Mass row; Gravity/Pressure per
   §2.6 once the amendment (or an admissible probe set) exists; SC-021 sweeps +
   CPU gate alone.
3. D-2..D-5 + `vorago_param_surface_test.cpp` (SC-019, SC-022, SC-023 (1), forwarder unit case).
4. `param_mapping.h`, `plugin_ids.h`, 14 packs + global extension, `param_routes.h` (static_asserts compile).
5. Controller registration/format/mirror + `IMidiMapping`; SC-018 table + SC-010.
6. Processor routes, trackers, `pushAllSurfaces`, state v2; update Phase 11 tests; SC-002/003/004/005/007/
   008/009/017/023 (2).
7. `SustainLatch` + slice merge; SC-012, SC-013; seed; SC-014; SC-015.
8. `[long]` lane: SC-011, SC-014 spread, SC-019 60 s, SC-020, SC-023 (1).
9. Perf lane alone: SC-016, SC-021 CPU. pluginval, clang-tidy (`dsp`, `vorago`), node lints, portability.
10. Leaf `CLAUDE.md` (FR-052): ID table with new bands and live macros, state v2 layout, route table,
    decisions 1 and 5 **delivered**, decision 3 replaced by the shipped MB route for output saturation.

---

## Review notes (plan revision, 2026-09-24)

- **Issue 1 (FR-060 / SC-021): applied, with one scoped deviation.** SC-021 for Gravity and Pressure is
  now BLOCKED on a spec amendment (§0, §2.6, R-1, §8 OQ-1), and the probe is prerequisite gate P-0 with a
  blocking user-ruling task (§2.6 "Gate P-0", §9 step 2). The probe was **not run during this revision**:
  it needs D-1 (`setTargetBase`), which is code, and this document is a plan with no code written. Gate
  P-0 therefore makes the run the first build action and forbids generating any Gravity/Pressure closure
  task until its table has been put to the user and the spec amended. No threshold was touched.
- Issues 2–7: applied in full — SC-003 references pinned to untouched state (§6.3); SC-004 re-prepare
  arm (§6.3, R-3); MB remedy adopted as a per-block smoothed base (R-5, D-P8); SC-011 scope enumerated
  (§6.4, D-P9); FR-045 reset asserted (SC-013 (7)); R-5 rewritten to fit the designed loop, with VP
  per-field fan-outs and an SC-016 remedy arm.
