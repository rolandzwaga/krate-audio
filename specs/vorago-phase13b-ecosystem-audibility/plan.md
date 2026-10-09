# Implementation Plan: Vorago Phase 13b — Ecosystem Audibility

**Spec:** `specs/vorago-phase13b-ecosystem-audibility/spec.md` (reviewed, Clarifications Q1–Q8 of 2026-09-27)
**Roadmap:** `specs/Vorago-roadmap.md` Phase 13b (lines 571–602)
**Branch / base:** `feat/vorago-phase1-events-modulation` at `9ecd6d10`
**Status:** PLAN. No production code is written by this document.

Every `file:line` below was read in the planning session. Where this plan adds a name, the ODR sweep
result is in §1.7.

---

## 0. What this plan decides (read first)

1. **No new class.** All production work is private members, constants and lever code inside
   `VoragoVoice` (`dsp/include/krate/dsp/systems/vorago_voice.h`, Layer 3). `VoragoEngine`
   (`vorago_engine.h`, Layer 3) gets **one friend declaration** and nothing else. Nothing else in
   `dsp/` or `plugins/vorago/src/` changes (FR-026).
2. **Primary levers (OQ-1), all acting upward from base (Q3):**

   | Kind | Wake (kept, FR-020) | Primary lever (ships) | Ladder levers (code added only if §3.5 L4 is entered, §2.8) |
   |---|---|---|---|
   | `Noise` | `setSourceWake` | source level `NoiseOrganism::setSourceLevel(s, dB)` | none. Colour is **excluded** (§1.4) |
   | `Resonator` | `setPeakWake` | peak level `ResonanceDriftNetwork::setPeakLevel(p, dB)` | freq wander `setFreqWander(p, st)`, voice-slewed |
   | `Feedback` | `setLoopWake` | loop gain `FeedbackEcology::setLoopGain(l, g)` | ring coupling `setCoupling(l, (l+1)%n, a)` (Q8) |
   | `Partial` | — | unchanged: mutation + bloom depth, additive (FR-016) | none. FR-016 keeps its routing shape; FR-019a shaping is **not** applied to it (§2.4, §3.5 L6) |
   | `Ghost` | — | unchanged: `atmos_.setLevel(ghostPeak_ * ghost)` (FR-016) | none (its base is macro-owned, §1.5) |

3. **Wake-base retune (FR-018) is derived from data, not tried by ear.** The colony is **open-loop
   with respect to audio**: the voice only reads from `EcosystemEngine` (§1.2), so the routed lane
   distribution does not depend on any voicing choice in this phase. One survey (§3.4) measures the
   per-kind lane quantiles at the default surface, and the new peak/loop bases are computed from them
   by a stated rule. `noiseWakeBase_` stays at 0.35 (Q1).
4. **FR-018a retunes are limited to values that are live in the plugin.** Noise level, loop gain,
   ecology mix, cloud mutation, bloom depth and ghost peak are all **rewritten every `process()` by
   macro rows** (§1.5). A voice-side retune of any of them would show up in the `dsp_*` tests and
   disappear in the plugin. The plan therefore retunes **only** peak level, freq wander and ring
   coupling, which are voice-owned constants with no macro row. FR-016's base retunes of
   `mutationBase_`, `bloomDepthBase_` and `kGhostBurstPeak` are **not used** for the same reason.
5. **Two test seams, one friend.** A new friend `detail::VoragoEcosystemLeverProbe` (declared in both
   headers, defined in one new TU) provides lane injection (FR-023), the control-rate fast path and
   non-const ecosystem access. The existing `detail::VoragoEcosystemRuleProbe` (probe TU) gains the
   FR-007 true-off, the colony-output readout and the limiter-reduction twin.
6. **Limiter gain reduction is measured without touching `TruePeakLimiter`**, using a twin render
   with the limiter ceiling raised through the friend (§4.4). The limiter has zero latency and applies
   one L/R-linked gain `out = g · in` sample-aligned (`true_peak_limiter.h:1-17`, `:159-160`), so
   `g = |y| / |u|` is exact per sample. `g` is **not memoryless**: the attack is instantaneous to
   `ceiling / tp`, where `tp` is the 4x-oversampled true peak folded with the raw samples
   (`:141-150`, `:154-155`), and the release is a one-pole with `kDefaultReleaseMs = 80` (`:47`,
   coefficient `:91-95`, update `:156-157`). Release tails therefore count as reduction, and the §4.4
   validity check is built for that behaviour.
7. **Order of work:** instrument (no production change) → FR-001 before-record → survey → levers +
   retune → Gate 1 → Gate 2 → evidence and regression. §7 has the steps.

---

## 1. Verified facts this plan builds on

### 1.1 `VoragoVoice` — `dsp/include/krate/dsp/systems/vorago_voice.h`

- Friend forward declarations live in `namespace detail` at `:157-172`. The class befriends them at
  `:1532-1537`: `VoragoVoiceSilenceRampProbe`, `VoragoEngineNonFiniteProbe`, `VoragoVoiceIdentityProbe`,
  `VoragoEcosystemRuleProbe`, `VoragoEngine`, `VoragoMacroMatrix`.
- `static constexpr std::size_t kControlChunkSamples = 64;` (`:237`) and
  `kMaxSlotsPerKind = ResonanceDriftNetwork::kMaxPeaks` (`:270`, 12).
- `kVoiceSizeBound = 123840` (`:426`), measured from 117 920 B with 5 % headroom (`:415-425`). The
  `static_assert` is at `:2428`.
- Prepare step 5 installs the section defaults. `setNoiseLevelDb(-18.0f)` is at `:569` and
  `setNoiseWakeBase(0.35f)` at `:571`. The loop at `:580-583` writes `resonance_.setPeakLevel(p, -9.0f)`
  and `resonance_.setFreqWander(p, 1.5f)` **directly on the component**; no voice setter exists for
  either. `peakWakeBase_.fill(0.50f)` is at `:585`. `setEcologyLoopGain(FeedbackEcology::kDefaultLoopGain)`
  is at `:590`. The ring loop `ecology_.setCoupling(l, (l + 1u) % numLoops, 0.12f)` is at `:595-597`,
  followed by `loopWakeBase_.fill(0.50f)` at `:598`. `setEcosystemDepth(0.85f)` is at `:624`.
  Step 8 is `prepared_ = true; reset();` (`:723-724`; the `// --- 8. ---` comment is at `:722`).
- `reset()` (`:738`) → `clearRunState(false)`. `resetForRecovery()` (`:755`) and `resetForSteal()`
  (`:766`) → `clearRunState(true)`. `clearRunState` calls `installIdentityNeutral()` **first**
  (`:1579`), before the component resets that snap ramps to their stored targets (comment
  `:1571-1578`).
- `setNoiseLevelDb(float dB)` (`:1236-1257`) rejects non-finite values, clamps to
  `[NoiseGenerator::kMinLevelDb, kMaxLevelDb]` and early-outs if the value equals
  `noise_.getSourceLevel(0)` (FR-067). It then fans out over `getNumSources()`.
  `getNoiseLevelDb()` returns `noise_.getSourceLevel(0)` (`:1258`).
- `setEcologyLoopGain(float g)` (`:1321-1326`) fans out `ecology_.setLoopGain(l, g)` with no finite
  check of its own. `getEcologyLoopGain()` returns `ecology_.getLoopGain(0)` (`:1327`).
- `setEcosystemDepth` (`:1405`) fills every kind's depth. `setEcosystemDepthFor` is at `:1432`.
  `setEventRateScale` (`:1449`) clamps to [0.1, 10].
- `static constexpr float combineWake(float base, float eco, float sched)` returns
  `std::max(base, std::max(eco, sched))` (`:1018-1021`).
- `kindForFamily` is at `:1708`, `slotCountForKind` at `:1726`, `IdentityLanes { eco, sched }`
  (5 × 12 floats each) at `:1754-1757`, `assignAgentSlots` at `:1773` and `reduceAgentLanes` at
  `:1812`. The latter stores `lanes.eco[k][s] = ecosystemDepth_[k] * output[i]` at `:1831`.
  `gatherEcosystemLanes` (const) is at `:1839` and `gatherSchedulerLanes` at `:1868`.
- `applyIdentityLanes` (`:1911-1944`) is documented as "the only writer of setSourceWake /
  setPeakWake / setLoopWake / setMutation / setDepth / ghostRequest_". It writes the Partial pair
  additively at `:1939-1941`.
- `installIdentityNeutral()` (`:1970-1979`) is `applyIdentityLanes(kNoLanes)` plus the gravity base.
- `publishIdentity()` (`:2022-2028`) runs gather-eco, gather-sched, apply, life lanes and event rate
  scale. It is called from `renderOneChunk()` step 1 (`:2170-2176`) **and** from
  `advanceOneChunkLifeOnly()` (`:2274-2292`), which runs the identity layer with no audio.
- Members: `noiseWakeBase_`, `peakWakeBase_` and `loopWakeBase_` (`:2364-2366`), `ecosystemDepth_`
  (`:2370`), `mutationBase_ = 0.15f` and `bloomDepthBase_ = 0.60f` (`:2375-2376`), and
  `lastEventTarget_` (`:2390`).
- `processStereoBlock(float*, float*, std::size_t)` is at `:841` and `noteOn(float hz, float velocity)`
  at `:907`. `ecosystem() const` is at `:1516`. Counts change only in prepare: `setNumSources`,
  `setNumPeaks` and `setNumLoops` appear only at `:559`, `:576` and `:588`.

### 1.2 `EcosystemEngine` — `dsp/include/krate/dsp/systems/ecosystem_engine.h` (read-only)

- The `Kind` order is Partial 0, Resonator 1, Noise 2, Feedback 3, Ghost 4 (`:282-288`).
  `kNumKinds = 5` (`:168`) and `kMaxAgents = 48` (`:152`).
- `kOutputAnchor = 0.5` (`:231`). `publish()` computes
  `raw = kOutputAnchor * energy_i * agentCount / energyBudget * gate_i`, clamped to [0, 1]
  (`:2323-2340`). The gate moves by `1/rampSteps_` per publish toward `dormant ? 0 : wake`.
- `rampSteps_ = max(1, ceil(0.050 / dt_))` and `dt_ = stepChunks_ * 64 / fs` (`:356-357`).
  `kDefaultStepIntervalChunks = 8` (`:196`), so at 48 kHz dt = 10.667 ms and rampSteps = 5.
- `getAgentOutput(i)` returns a value **held between steps** (`:884-887`). Also available:
  `getAgentEnergy` (`:890`), `getAgentKind` (`:893`), `getControlStepCount()` (`:1021`) and
  `getStepIntervalChunks()` (`:923`).
- `setAgentDormant(i, bool)` (`:776`) touches only the gate target.
- **The colony is open-loop.** The voice's only calls into `ecosystem_` are `prepare`, `setSeed`,
  `reset`, `processChunk` and the getters `getAgentCount/Kind/Energy/Output/AllocatedBytes`
  (grep of `ecosystem_\.` in `vorago_voice.h`: `:520`, `:1549`, `:1608`, `:1763-1846`, `:2170`,
  `:2276`, `:819`). No audio-derived value reaches the simulation. `vorago_engine.h` never touches it.
  **Consequence:** lane statistics are independent of every lever and wake base in this phase.

### 1.3 Lever destinations — setters, clamps, smoothing, getters

| Setter (file:line) | Clamp | Smoothing | Getter returns | Re-arm hazard |
|---|---|---|---|---|
| `NoiseOrganism::setSourceLevel(slot, dB)` (`noise_organism.h:488-509`) | `sanitise(dB, -12)` then [−96, +12] (`:493`) | `levelRamp` LinearRamp, `kGainRampMs = 50` (`:178`), constant **duration** (`:494-497`) | `getSourceLevel` → stored `levelDb` target (`:872`) | yes: every call re-arms (`:506-507`) |
| `ResonanceDriftNetwork::setPeakLevel(p, dB)` (`resonance_drift_network.h:661-667`) | [`kMinPeakLevelDb` −60, `kMaxPeakLevelDb` +12] (`:236-237`) | per-peak `levelRamp`, `kGainRampMs = 50` (`:143`) | `getPeakLevel` → `levelDb` (`:847`) | yes: re-arms on every call, no early-out |
| `ResonanceDriftNetwork::setFreqWander(p, st)` (`:681-685`) | [0, `kMaxFreqWanderSemis` 24] (`:238`) | stored directly. The applied frequency is per-step slew-limited by `freqSlewOct_` (`:1647-1649`), but only while the peak is engine-active (`:1660-1665`) | `getFreqWander` (`:853`) | none |
| `FeedbackEcology::setLoopGain(l, g)` (`feedback_ecology.h:1111-1117`) | [`kMinLoopGain` 0, `kMaxLoopGain` 0.90] (`:264-266`) | `ownFbSmoother` OnePole, `kCouplingSmoothMs = 20` (`:424`, `:760`) | `getLoopGain` → `ownFbTarget` (`:1406-1409`) | OnePole retarget is idempotent |
| `FeedbackEcology::setCoupling(from, to, a)` (`:1139-1146`) | [0, `kMaxCouplingPerPair` 0.5] (`:269`); diagonal is a no-op | `couplingSmoother_` 20 ms (`:766`) | `getCoupling` → `couplingTarget_` (`:1434-1438`) | idempotent |

- Row-sum ceiling `kMaxTotalLoopGain = 0.95` (`feedback_ecology.h:271-272`): own feedback plus incoming
  coupling is normalised. The governor remains the energy authority (E-4).
- **Wake is a linear gain, not a switch.** `gateSteady` returns `wakeAmount` itself unless it is at or
  below `kWakeSilenceEpsilon = 1e-6`, in which case it returns 0 (`resonance_drift_network.h:1262-1267`,
  `feedback_ecology.h:2300-2306`; the noise gate targets `gateSteady(i)` via `refreshGates`,
  `noise_organism.h:2173-2183`). A base of 0.50 therefore holds every peak and loop at −6 dB, and a
  colony lane only matters where it exceeds 0.50. This is the mechanism of the masking the spec
  describes.

### 1.4 Destinations excluded from lever duty

- **Noise colour `setFilterBaseCutoff`** (`noise_organism.h:600-607`) calls `applySlotConfiguration`
  (`:1931`). That function rewrites every generator enable, per-type level, resonator frequency and
  decay, and the comb delays on each call (`:1937-1990`). It is not a smoothed scalar, and a per-step
  call would reconfigure the slot about 94 times per second. It is **excluded** (FR-023's
  "or not chosen").
- **Gain wander `setGainWander`** (`resonance_drift_network.h:697-701`) is stored directly and applied
  unslewed (`composeApplied`, `:1532-1534`: "FR-035 exempts gain from the ceiling"). Peak level
  already covers the level axis, so it is **not chosen**.

### 1.5 Macro ownership — which bases are live in the plugin

`Processor::process()` calls `macros_.apply(*engine_)` once per `process()` (`processor.cpp:356`).
`VoragoMacroMatrix::apply` then writes, **every block, to every voice** (`vorago_macro_matrix.h:1071-1113`):
`setMutation`, `setNoiseLevelDb`, `setNoiseWakeBase`, `setResonanceMix`, `setEcologyMix`,
`setEcologyLoopGain`, `setEcosystemDepth`, `setBloomDepth` and more. The engine also receives
`setGhostPeakLevel` (`:1065`). The row bases are NoiseLevelDb −18 (`:379-380`), BloomDepth 0.60
(`:385-386`), CloudMutation 0.15 (`:462-463`), EcologyMix 0.15 (`:496-497`), EcologyLoopGain 0.72
(`:502-503`), GhostPeakLevel 0.60 (`:581-582`) and ResonanceMix 0.45 (`:730-731`). Life's
EcosystemDepth row has base 0.85 and amount 0.15 (`:630-635`).
**No macro row targets peak level, freq wander or coupling** (grep of `setPeakLevel|setFreqWander|setCoupling`
in `vorago_macro_matrix.h`: no hits). These three are the only voice-owned lever bases that a
voice-side retune reaches in the plugin.

### 1.6 Engine, probe and test infrastructure

- `VoragoEngine` has `kMaxVoices = 6` (`:225`), `kDefaultPolyphony = 4` (`:228`) and
  `kVoiceSaltBase = 0xA000` (`:237`, public). Friends are at `:1302-1304`, including
  `detail::VoragoEcosystemRuleProbe`. Private members include `limiter_` (prepared at `:439-440`
  with `setCeilingDb(kOutputCeilingDb)`), `voices_`, `isRendering(v)` (`:1345`) and `seed_ = 1u` (`:1827`).
- `processOutputStage(l, r, n)` (`:1208-1227`) runs the saturators, then the makeup, then
  `limiter_.processBlock(l, r, n)` **last**.
- `TruePeakLimiter` (`processors/true_peak_limiter.h`) is zero-latency, with one gain linked across
  L and R (`:1-17`). Per sample it computes `tp` = max of the raw |L|, |R| and the four
  4x-oversampled phases of each channel (`:141-146`), then `required = tp > ceiling ? ceiling/tp : 1`
  (`:148-150`). The gain `currentGain_` drops to `required` **instantaneously** when `required` is
  lower (`:154-155`); otherwise it recovers by a **one-pole release**
  `currentGain_ += (required - currentGain_) * releaseCoeff_` (`:156-157`), with
  `kDefaultReleaseMs = 80` (`:47`) and `releaseCoeff_ = 1 - exp(-1/tau)`, `tau = 80 ms · fs`
  (`:91-95`). Consequences for measurement:
  - after any reduction, samples below the ceiling stay attenuated for several time constants;
  - in float the release stalls just below 1.0f: at 48 kHz `releaseCoeff_ ≈ 2.6e-4`, so once
    `1 - g < ~1.1e-4` the increment is under half an ulp of 1.0f and `g` stays at about −0.001 dB
    for the rest of the render;
  - because detection includes the inter-sample peak, a sample whose raw |u| is below the ceiling
    can still be attenuated.

  `setCeilingDb(float)` is public (`:85-89`). No gain-reduction getter exists.
- Ghost lane: `atmos_.setLevel(ghostPeak_ * ghost)` (`vorago_engine.h:1508`), with the max over
  rendering voices at `:1488-1491`. `setGhostPeakLevel` clamps to [0, 1] (`:978-983`).
- Probe TU `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp` (534 lines):
  - the friend definition is at `:70-88` (`forEachEcosystem` over `kMaxVoices`, `setWakeBases`);
  - `kCandidates` is at `:217-232`, `RenderResult` at `:234-239` and `stereoRmsDb` at `:242-253`;
  - `renderOnce(KnobApply, float, double, const std::vector<Override>&, double)` is at `:258-363`;
  - the knob is applied after `setActive(true)` and before the note-on block (`:271-285`), and only
    `m1RmsDb` is computed (`:361`);
  - `TEST_CASE("Vorago_EcosystemRuleProbe", "[.probe][vorago]")` is at `:380`.
- `render_fingerprint.h`: `fingerprintRender(span)` and `compareFingerprints(actual, ref, metricTol =
  2.5e-4, sampleTol = 5e-4)` → `FingerprintComparison::withinTolerance()` (`tests/test_helpers/render_fingerprint.h:55-157`).
- `artifact_detection.h`: `ClickDetectorConfig` (`:38-44`), `ClickDetector(config)`, `prepare()`,
  and `std::vector<ClickDetection> detect(const float*, size_t)`. `ClickDetection` carries `sampleIndex`
  (`:72-75`).
- Existing Phase 10 cases, **not edited**: `VoragoVoice_WakeCombineRule` (`vorago_voice_test.cpp:2593`),
  `VoragoVoice_AgentReductionRule` (`:2687`), `VoragoVoice_EcosystemRouting` `[long]`
  (`vorago_voice_longrun_test.cpp:631`), `VoragoEngine_DeterminismHarness` (`vorago_engine_test.cpp:1498`)
  and `VoragoEngine_CpuBudget` `[.perf]` (`vorago_perf_test.cpp:971`, clauses asserted at `:1011-1012`,
  headroom `kAvailableRegressionHeadroom` at `:259-260`).
- `dsp/tests/CMakeLists.txt` enumerates the Vorago TUs of `dsp_systems_tests` at `:494-552`
  (enumerated, not globbed).
- Tests that read lever-adjacent getters: `getNoiseLevelDb()` / `getEcologyLoopGain()` in
  `vorago_macro_test.cpp:1072,1086`, `vorago_param_surface_test.cpp:111,125`,
  `channel_pressure_test.cpp:85,92`, `param_surface_test.cpp:137,144` and `param_flow_test.cpp:199`.
  Wake-amount reads are in `vorago_voice_test.cpp:2292-2675`, `vorago_voice_longrun_test.cpp:346-362`
  and `vorago_macro_test.cpp:716`. All of them must stay green unedited. This is why getters must
  return the **base** (FR-021).

### 1.7 ODR sweep (FR-024), run this session

`grep -rn "<name>" dsp/ plugins/ tests/` returns **0 hits** for every name this plan introduces:
`VoragoEcosystemLeverProbe`, `kPeakLevelLeverSpanDb`, `kNoiseLevelLeverSpanDb`, `kLoopGainLeverSpan`,
`kCouplingLeverSpan`, `kFreqWanderLeverSpanSemis`, `kFreqWanderLeverSlewSemisPerChunk`, `kPeakLevelBaseDb`,
`kRingCouplingBase`, `kFreqWanderBaseSemis`, `noiseLevelBaseDb_`, `noiseLevelOffsetDb_`, `loopGainBase_`,
`loopGainOffset_`, `freqWanderApplied_`, `freqWanderSlewPerChunk_`, `injectedEco_`, `ecoInjectionActive_`,
`applyEcosystemLevers`, `shapeLeverInput`, `schedLanes`, `kLeverInputGain`, `vorago_ecosystem_lever_test`,
`vorago_ecosystem_lever_longrun_test`. (`shapeLeverInput` and `schedLanes` were swept in the review
revision of this plan: 0 hits.) The §2.8 ladder names are swept now even though they enter the
header only if L4 is reached. An implementer who adds any other name re-runs the sweep first.

---

## 2. Production design — `VoragoVoice` (Layer 3, `vorago_voice.h`)

No new include. `<algorithm>` (`std::clamp`, `std::max`) and every component header are already
included. All additions are private unless marked otherwise.

### 2.1 Constants (private `static constexpr`, beside the other voice constants)

```cpp
// --- Phase 13b levers (FR-015 .. FR-025). Shipped values are fixed by the
// --- tuning procedure (plan S3.5) and recorded before/after in compliance.
static constexpr float kPeakLevelBaseDb          = -9.0f;  // was the literal at :581 (FR-018a-retunable)
static constexpr float kNoiseLevelLeverSpanDb    = 9.0f;   // initial, S3.5 L2
static constexpr float kPeakLevelLeverSpanDb     = 12.0f;  // initial, S3.5 L2
static constexpr float kLoopGainLeverSpan        = 0.15f;  // initial, S3.5 L2
// FR-019a lane shaping (S3.5 L6). 1.0 = identity = not used. Indexed by
// EcosystemEngine::Kind; only the FR-015 lever kinds may be shaped.
static constexpr std::array<float, EcosystemEngine::kNumKinds> kLeverInputGain{1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
static_assert(kLeverInputGain[static_cast<std::size_t>(EcosystemEngine::Kind::Partial)] == 1.0f
              && kLeverInputGain[static_cast<std::size_t>(EcosystemEngine::Kind::Ghost)] == 1.0f,
              "FR-016/FR-019a: Partial and Ghost are already-direct lanes and are never shaped");
// FR-018 wake-base retune floor: never at or below kWakeSilenceEpsilon, so the
// colony path can never generate a dormancy edge (E-3, Phase 5 FR-063).
static constexpr float kMinRetunedWakeBase = 0.05f;
static constexpr float kPeakWakeBase = 0.50f;  // was the literal at :585 (FR-018: set by S3.4)
static constexpr float kLoopWakeBase = 0.50f;  // was the literal at :598 (FR-018: set by S3.4)
static_assert(kPeakWakeBase >= kMinRetunedWakeBase && kLoopWakeBase >= kMinRetunedWakeBase);
```

The ladder constants (`kFreqWanderBaseSemis`, `kRingCouplingBase`, `kCouplingLeverSpan`,
`kFreqWanderLeverSpanSemis`, `kFreqWanderLeverSlewSeconds`) are **not** part of this edit. They enter
the header only if §3.5 L4 is entered (§2.8). Until then the freq-wander and coupling literals at
`:582` and `:596` stay as they are.

All spans are finite and non-negative. **Why this gives FR-017's exact zero:** the offset is
`span * x`, and `x = 0.0f` gives `+0.0f` for any finite span. `base + 0.0f == base` holds exactly in
IEEE-754 for any finite base, including under `-ffast-math`, where `x + 0.0f` is folded to `x`.

### 2.2 Members (private; added in the "identity routing" block after `:2376`)

```cpp
// --- Phase 13b lever state (FR-021 shadows, FR-023 slew, FR-023 seam) ----
float noiseLevelBaseDb_ = -18.0f;                                        // shadow of setNoiseLevelDb
float loopGainBase_ = FeedbackEcology::kDefaultLoopGain;                 // shadow of setEcologyLoopGain
std::array<float, NoiseOrganism::kMaxSources> noiseLevelOffsetDb_{};     // last applied offset
std::array<float, FeedbackEcology::kMaxLoops> loopGainOffset_{};         // last applied offset
// FR-023 lane-injection seam: written ONLY by detail::VoragoEcosystemLeverProbe.
std::array<std::array<float, kMaxSlotsPerKind>, EcosystemEngine::kNumKinds> injectedEco_{};
bool ecoInjectionActive_ = false;
```

The additions total about 290 B per voice (2 + 4 + 6 floats, with `kMaxSources = 4` at
`noise_organism.h:142` and `kMaxLoops = 6` at `feedback_ecology.h:193`, plus 240 B of injection, one
bool and padding). §2.8 adds about 52 B more if L4 is entered. Both are well inside the 5 920 B
headroom `kVoiceSizeBound` recorded (`:415-426`). The `static_assert` at `:2428` is the check. If it
fires, the implementer stops and surfaces it rather than raising the bound.

### 2.3 Base setters and getters (FR-021, E-7, E-8)

```cpp
void setNoiseLevelDb(float dB) noexcept {           // replaces the body at :1236-1257
    if (!detail::isFinite(dB)) return;               // FR-071, unchanged
    const float clamped = std::clamp(dB, NoiseGenerator::kMinLevelDb, NoiseGenerator::kMaxLevelDb);
    if (clamped == noiseLevelBaseDb_) return;        // FR-067 early-out, now against the SHADOW
    noiseLevelBaseDb_ = clamped;
    const std::size_t n = std::min(noise_.getNumSources(), NoiseOrganism::kMaxSources);
    for (std::size_t s = 0; s < n; ++s) writeNoiseLevel(s);   // base + held offset
}
[[nodiscard]] float getNoiseLevelDb() const noexcept { return noiseLevelBaseDb_; }  // FR-021

void setEcologyLoopGain(float g) noexcept {         // replaces :1321-1326
    if (!detail::isFinite(g)) return;                // mirrors the owner's reject (feedback_ecology.h:1113)
    loopGainBase_ = std::clamp(g, FeedbackEcology::kMinLoopGain, FeedbackEcology::kMaxLoopGain);
    const std::size_t n = std::min(ecology_.getNumLoops(), FeedbackEcology::kMaxLoops);
    for (std::size_t l = 0; l < n; ++l) writeLoopGain(l);
}
[[nodiscard]] float getEcologyLoopGain() const noexcept { return loopGainBase_; }
```

The write helpers (private) are the **only** callers of the component setters for these
destinations, apart from prepare:

```cpp
void writeNoiseLevel(std::size_t s) noexcept {
    const float t = std::clamp(noiseLevelBaseDb_ + noiseLevelOffsetDb_[s], -96.0f, 12.0f);
    if (t != noise_.getSourceLevel(s)) noise_.setSourceLevel(s, t);   // E-7: never re-arm an unchanged ramp
}
void writeLoopGain(std::size_t l) noexcept {
    const float t = std::clamp(loopGainBase_ + loopGainOffset_[l],
                               FeedbackEcology::kMinLoopGain, FeedbackEcology::kMaxLoopGain);
    if (t != ecology_.getLoopGain(l)) ecology_.setLoopGain(l, t);
}
```

The helpers compare against the **component** getter, not the shadow. That is what makes a
re-prepare correct: `NoiseOrganism::prepare()` restores its own −12 dB default (comment at `:556-557`).
Prepare step 5's `setNoiseLevelDb(-18)` may then early-out on the shadow, but step 8's
`reset()` → `clearRunState` → `installIdentityNeutral()` → `writeNoiseLevel` sees −12 ≠ −18 and
writes, before `noise_.reset()` snaps the ramp (`:1571-1579`). SC-012's re-prepare arm tests this
path.

Behaviour at depth 0 is unchanged: the offsets are 0, so every write is `base`, exactly what the
old setters wrote. Every test in §1.6's getter list reads the same number it read before.

Prepare step 5 changes from literals to the constants:

- `resonance_.setPeakLevel(p, kPeakLevelBaseDb)` (the freq-wander write at `:582` keeps its literal);
- `peakWakeBase_.fill(kPeakWakeBase)` and `loopWakeBase_.fill(kLoopWakeBase)`;
- `noiseLevelOffsetDb_` and `loopGainOffset_` zero-filled.

There is no value change until §3.4 or §3.5 moves a constant.

### 2.4 Lever application — inside `applyIdentityLanes` (FR-015, FR-017, FR-019, FR-020, FR-022)

The lever code is appended **after** the existing wake, Partial and Ghost writes at `:1911-1944`, in
the same function. It therefore inherits the function's two callers, `publishIdentity` and
`installIdentityNeutral`, and FR-022 holds by construction. `combineWake` and its three call sites are
not touched (FR-020). The levers read **only** `lanes.eco` (FR-019):

The FR-019a shaping is a **private static member function**, not a lambda, so the SC-021 test can
call it through the friend:

```cpp
// FR-019a lever input; identity while gain == 1. 0 -> +0.0f exactly; [0,1] -> [0,1] (SC-021).
[[nodiscard]] static float shapeLeverInput(float lane, float gain) noexcept {
    return std::clamp(gain * lane, 0.0f, 1.0f);
}
```

```cpp
// Noise: level up.
for (std::size_t s = 0; s < sources; ++s) {
    noiseLevelOffsetDb_[s] = kNoiseLevelLeverSpanDb * shapeLeverInput(lanes.eco[kNoise][s], kLeverInputGain[kNoise]);
    writeNoiseLevel(s);
}
// Resonator: level up.
for (std::size_t p = 0; p < peaks; ++p) {
    const float x = shapeLeverInput(lanes.eco[kResonator][p], kLeverInputGain[kResonator]);
    const float lvl = std::clamp(kPeakLevelBaseDb + kPeakLevelLeverSpanDb * x,
                                 ResonanceDriftNetwork::kMinPeakLevelDb, ResonanceDriftNetwork::kMaxPeakLevelDb);
    if (lvl != resonance_.getPeakLevel(p)) resonance_.setPeakLevel(p, lvl);        // E-7
}
// Feedback: own gain up.
for (std::size_t l = 0; l < loops; ++l) {
    const float x = shapeLeverInput(lanes.eco[kFeedback][l], kLeverInputGain[kFeedback]);
    loopGainOffset_[l] = kLoopGainLeverSpan * x;
    writeLoopGain(l);
}
```

The Partial pair (`:1939-1941`) and the Ghost request are **not touched**. FR-016 keeps their routing
shape, and FR-019a allows shaping only on the input of an FR-015 lever, which Partial and Ghost do not
have. The `static_assert` in §2.1 pins their `kLeverInputGain` entries at 1. `ghostRequest_`, the
Partial writes and the three `combineWake` calls all keep reading the **raw** lane (FR-019a, SC-021).
The shaped value exists only inside the three loops above.

`installIdentityNeutral()` needs no addition for the primary levers: the offsets become 0 through the
apply itself (FR-022, SC-012). §2.8 adds a slew-state snap only if L4 ships freq wander.

**Unaddressed slots:** a slot no valid agent addresses has `lanes.eco = 0`
(`IdentityLanes` is value-initialised, `:2023`), so its lever sits at base. That matches E-1.

### 2.5 The FR-023 seam — `publishIdentity`

```cpp
void publishIdentity() noexcept {
    IdentityLanes lanes{};
    if (ecoInjectionActive_) {            // FR-023: detail::VoragoEcosystemLeverProbe only
        lanes.eco = injectedEco_;
    } else {
        gatherEcosystemLanes(lanes);
    }
    gatherSchedulerLanes(lanes);          // unchanged: schedulers keep running
    applyIdentityLanes(lanes);
    publishLifeLanes();
    applyEventRateScale();
}
```

The injection replaces the **whole** eco lane set, Partial and Ghost included, so an injected 1 is
the FR-023 domain maximum on every kind. `ecoInjectionActive_` is `false` in every production path
and costs one well-predicted branch per 64-sample chunk.

### 2.6 Friend declaration

`namespace detail { struct VoragoEcosystemLeverProbe; }` is added at `:171`, beside
`VoragoEcosystemRuleProbe`. `friend struct detail::VoragoEcosystemLeverProbe;` is added at `:1535`.
The comment follows B-4: "DEFINED IN THE TEST TU".

**`VoragoEngine` (`vorago_engine.h`)** gets its only change here:
`friend struct detail::VoragoEcosystemLeverProbe;` beside `:1304`. The forward declaration is already
visible, because `vorago_engine.h` includes `vorago_voice.h`. `injectEcoAll` needs this friend to
reach `voices_`, and the GR twin needs it to reach `limiter_`.

### 2.7 RT safety and cost (FR-024)

- The per-chunk additions are 4 + 12 + 6 clamp/multiply pairs and up to 22 compares. A component
  setter runs only when its target changed. Lanes change only at simulation steps (held output,
  `ecosystem_engine.h:884-887`), which is **once per 8 chunks** at the shipped step interval.
- There is no allocation, lock, exception or I/O. The arrays are fixed-size, indexed by
  `min(getNum*(), kMax*)`.
- Denormals: `lanes.eco` is `depth * output`, and output comes from a double→float clamp that can be
  subnormal. The products run at control rate only, so a subnormal costs nothing measurable. dB
  targets absorb it (`base + tiny == base` after rounding). FTZ is inherited from the engine's audio
  thread. No flush-to-zero is added, because it would break nothing but would also be code with no
  measured need.

### 2.8 Ladder levers — added only if §3.5 L4 is entered

Nothing in this subsection is written at §7 step 3. FR-015 requires one lever per kind, and the
primary levers (§2.4) are that lever. The coupling and freq-wander paths enter `vorago_voice.h` only
when the ladder reaches L4, as that rung's edit. If the phase ends before L4, the shipped header
contains none of it.

When L4 is entered, the edit adds:

```cpp
// --- constants (§2.1 block) ---
static constexpr float kFreqWanderBaseSemis        = 1.5f;   // replaces the literal at :582 (FR-018a-retunable)
static constexpr float kRingCouplingBase           = 0.12f;  // replaces the literal at :596 (FR-018a-retunable)
static constexpr float kCouplingLeverSpan          = /* L4 value, <= 0.30 */;
static constexpr float kFreqWanderLeverSpanSemis   = /* L4 value, <= 6 st  */;
static constexpr float kFreqWanderLeverSlewSeconds = 0.050f; // full-span slew time (FR-023 / E-6)
// --- members (§2.2 block) ---
std::array<float, ResonanceDriftNetwork::kMaxPeaks> freqWanderApplied_{};  // voice slew state (E-6)
float freqWanderSlewPerChunk_ = 0.0f;                                      // derived at prepare()
```

Only the lever(s) L4 actually ships are added. A lever L4 does not ship gets no constant, member or
code path.

- Prepare step 5 writes `resonance_.setFreqWander(p, kFreqWanderBaseSemis)` and
  `ecology_.setCoupling(l, (l+1)%n, kRingCouplingBase)` in place of the literals, and fills
  `freqWanderApplied_` with `kFreqWanderBaseSemis`.
- Prepare step 7 sets
  `freqWanderSlewPerChunk_ = kFreqWanderLeverSpanSemis * 64 / (fs * kFreqWanderLeverSlewSeconds)`,
  next to the other rate-derived values (`:700-706`).
- In §2.4's Resonator loop, after the level write:

  ```cpp
  const float goal = kFreqWanderBaseSemis + kFreqWanderLeverSpanSemis * x;
  freqWanderApplied_[p] += std::clamp(goal - freqWanderApplied_[p],
                                      -freqWanderSlewPerChunk_, freqWanderSlewPerChunk_);
  const float w = std::clamp(freqWanderApplied_[p], 0.0f, ResonanceDriftNetwork::kMaxFreqWanderSemis);
  if (w != resonance_.getFreqWander(p)) resonance_.setFreqWander(p, w);
  ```
- In §2.4's Feedback loop, after `writeLoopGain(l)`:

  ```cpp
  const std::size_t to = (l + 1u) % loops;     // the shipped ring pair only (Q8, E-4)
  const float c = std::clamp(kRingCouplingBase + kCouplingLeverSpan * x,
                             0.0f, FeedbackEcology::kMaxCouplingPerPair);
  if (to != l && c != ecology_.getCoupling(l, to)) ecology_.setCoupling(l, to, c);
  ```
- `installIdentityNeutral()` gains `freqWanderApplied_.fill(kFreqWanderBaseSemis)` before
  `applyIdentityLanes(kNoLanes)` (FR-022, SC-012). This **snaps** the slew state, so the neutral lanes
  then write the base directly.

The §1.3 clamps and smoothing facts for `setFreqWander` and `setCoupling` apply unchanged. The added
size is about 52 B per voice (§2.2).

---

## 3. Algorithms and tuning math

### 3.1 Lever law (normative)

For kind k, slot s: `L = lanes.eco[k][s] ∈ [0, 1]` (`vorago_voice.h:1831`). Then
`x = clamp(g_k · L, 0, 1)` with g_k = `kLeverInputGain[k]` (1 unless §3.5 L6), and
`dest = clamp(base + span_k · x, compLo, compHi)`.

- Neutrality at depth 0 (FR-017, SC-006): `L = 0 · output = 0` → x = 0 → dest = base exactly.
- Scheduler blindness (FR-019, SC-007): `lanes.sched` never enters the lever expression.
- Monotone and upward (Q3). The worst case `L = 1` gives `base + span_k`, clamped by the component.
  E-5 and SC-011(a) bound it.

**Worst-case destinations at the initial spans:**

| Destination | Base | Maximum |
|---|---|---|
| noise level | macro base −18 dB (up to −12 at Density = 1, `vorago_macro_matrix.h:379-383`) | −9 dB (up to −3 dB) |
| peak level | −9 dB | +3 dB, inside the +12 clamp |
| loop gain | 0.72 | 0.87 (up to 0.90 clamped at Pressure = 1, where base = 0.88) |

### 3.2 Why levels, and why this should move the probe (design rationale)

- The descriptor is the mean of three minute descriptors: nine bands, motion, flux, correlation,
  energy spread and crest (`preset_test_support.h:47-54`). A **reseed** (t0) changes lane
  *trajectories* but not lane *statistics*, and the three-minute mean averages trajectories away.
- A rule-knob extreme changes lane statistics: mean output per kind, the balance between kinds, and
  step-to-step variance.
- A level lever maps a kind's mean lane directly into the band energy of the section it drives:
  noise is broadband, peaks are narrow resonant bands, and loops add sustain. The descriptor's band
  vector therefore follows colony *statistics* more than colony *identity*. This is the mechanism by
  which d(knob extreme)/t0 can exceed 2 when d(reseed) does not grow proportionally (E-10).
- **Ecosystem off** (all L = 0) puts every section at its base with wakes at the (lowered) bases.
  Every section is quieter and flatter than "on", which is Gate 1's contrast.

### 3.3 Settle time `T_settle` (SC-008)

- `dt = stepChunks · 64 / fs` (`ecosystem_engine.h:356`).
- `rampSteps = max(1, ceil(0.050 / dt))` (`:357`).
- `T_settle = (rampSteps + 1) · dt + T_lever`. The first term is the ramp publishes after dormancy is
  set, plus one step of held output (`:884-887`). T_lever is:
  - **0** for level, loop gain and coupling. The getters return the stored **target**
    (`noise_organism.h:872`, `resonance_drift_network.h:847`, `feedback_ecology.h:1406-1409`,
    `:1434-1438`), which equals base on the first control step after L reaches 0.
  - `ceil(kFreqWanderLeverSpanSemis / freqWanderSlewPerChunk_) · 64 / fs` for voice-slewed freq wander
    (only if L4 ships it, §2.8). By construction that is `kFreqWanderLeverSlewSeconds` rounded up to
    whole chunks.

| fs | dt | rampSteps | T_settle (targets) |
|---|---|---|---|
| 44.1 kHz | 11.61 ms | 5 | 69.7 ms |
| 48 kHz | 10.667 ms | 5 | 64.0 ms |
| 96 kHz | 5.333 ms | 10 | 58.7 ms |

The test computes the settle time from the live `getStepIntervalChunks()`, never from this table.

### 3.4 Wake-base retune rule (FR-018, SC-020)

The colony is open-loop (§1.2), so one lane survey is valid for every voicing:

1. Take the default surface at voice level: `prepare(48000)`, FR-090 defaults (depth 0.85),
   voice seed = `deriveStreamSeed(1u, VoragoEngine::kVoiceSaltBase + 0u)` (`core/random.h:102`;
   engine `seed_ = 1u`, `vorago_engine.h:1827`; salt `:237`, per `:347`), and `noteOn(65.406f, 100/127)`
   (MIDI 36). Advance to 340 s through the friend's `advanceLifeOnly` (§5.1). Life-only is
   indistinguishable from rendering for the identity layer (Phase 10 SC-030, `vorago_voice.h:2266-2274`).
2. Once per simulation step (whenever `ecosystem().getControlStepCount()` increments) inside
   [155 s, 340 s], collect `lanes.eco[k][s]` via `gatherEcosystemLanes` for k ∈ {Resonator, Feedback,
   Noise}. Collect only slots addressed by at least one `agentValid_` agent (Q6).
3. `Q_k(0.60)` is the 60th percentile of that multiset.
4. The shipped base is
   `kPeakWakeBase = clamp(floor20(Q_Resonator(0.60)), kMinRetunedWakeBase, 0.50)`, and the same for
   `kLoopWakeBase` with Q_Feedback. `floor20` rounds **down** to a multiple of 0.05, so the exceedance
   fraction only grows.
   - The rule leaves about 40 % of (step, slot) pairs above base, which clears SC-020's 25 % with
     margin.
   - If `Q(0.60) < 0.05`, the floor wins and the SC-020 arm is at risk. That is a stop-and-surface
     item, not a threshold change.
5. The survey also prints `Q_Noise` and, for **each** of Resonator, Feedback and Noise, the fraction
   of addressed (step, slot) pairs whose lane exceeds the base currently in the tree (read through the
   friend's `*WakeBase` accessors). Run on the FR-001 tree, these are SC-020's **before** fractions
   (bases 0.50 / 0.50 / 0.35). The Noise arm is recorded, not gated (Q1). The survey has no REQUIRE,
   so every figure reaches the log.

The survey is `VoragoVoice_EcosystemLaneSurvey` `[.probe]` (§5.1). Its output is checked into
`artifacts/lane_survey.log`, and the chosen bases cite its lines.

### 3.5 Tuning ladder (OQ-1) — the only permitted search, in order

Each rung is kept only if it is needed. After each rung the implementer runs the **Gate-1 pair** (both
surfaces, `VORAGO_PROBE_KNOBS=-`, `VORAGO_PROBE_REF=off`; 3 renders × 2 surfaces) and the **SC-005 check**
(default M1 RMS within ±3 dB of FR-001(a), limiter reduction ≤ 1 dB, `VORAGO_PROBE_GR=1`). Every rung's
constants and logs are recorded. **No threshold, candidate or range is ever changed** (FR-003).

| Rung | Change | Constraint |
|---|---|---|
| **L1** | §3.4 wake bases | automatic from the survey |
| **L2** | primary levers at the §2.1 initial spans (noise +9 dB, peak +12 dB, loop gain +0.15) | — |
| **L3** | loudness balance: if SC-005 M1 RMS is out of ±1.5 dB (half the gate, a margin), move `kPeakLevelBaseDb` (FR-018a) by the opposite sign, in 1 dB steps within [−18, −6] | peak base only (§1.5) |
| **L4** | raise spans (noise ≤ +12 dB, peak ≤ +18 dB, loop gain ≤ +0.18), then add the ladder levers by the §2.8 edit: `kCouplingLeverSpan` ≤ 0.30 (ring only, Q8) and `kFreqWanderLeverSpanSemis` ≤ 6 st (slewed). Only the lever(s) kept are added | re-run SC-011 after each |
| **L5** | lower `kRingCouplingBase` / `kFreqWanderBaseSemis` (FR-018a) to widen the lever contrast. Applies only to a ladder lever §2.8 added at L4 | voice-owned only |
| **L6** | FR-019a lever-input gain `kLeverInputGain[k]` ∈ {1.5, 2, 3}, per kind, for **Noise, Resonator and Feedback only**. Partial and Ghost are FR-016 already-direct lanes with no FR-015 lever, so they stay unshaped (§2.1 `static_assert`) | SC-021 becomes active |
| **L7** | voice-owned levers exhausted: proceed to L8 (ruling R-3) | — |
| **L8** | **ruling R-3 (plan stage, 2026-09-27):** lower the noise wake base default — parameter 301 (`kNoiseWakeId`, 0.35) and its macro base — as a recorded ruling with the Phase 12 parameter-table expectation updated (Clarifications Q1's deferred option); re-run Gate 1, SC-005 and SC-020's Noise arm. If L8 also fails: **stop and surface** the measured table to the user | SC-017 gains a documented exception; macro-row bases and `EcosystemEngine` stay out |

Gate 2 (FR-012) is run only after Gate 1 passes on the tree being measured (FR-014): the full table on
both surfaces.

- If Gate 2 fails with Gate 1 green, the implementer may re-enter at L4–L6 **aimed at statistic
  sensitivity**, for example a larger span on the kind whose knob rows sit just under 2·t0. Gate 1
  and SC-005 are then re-run, because the tree changed.
- If Gate 2 still fails, climb L8 (ruling R-3) once; if it still fails, **stop and surface**.

---

## 4. Probe TU extensions — `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp`

The TU keeps its `[.probe][vorago]` tag, stimulus, timeline, descriptor (`VoragoTest::describe` /
`meanOf` / `descriptorDistance`), 14 candidates and `leakExponent` exclusion (FR-002, FR-003, FR-006).
The header comment gains the new options.

### 4.1 Friend additions (same `struct VoragoEcosystemRuleProbe`, `:70-88`)

```cpp
// FR-007: dormancy on every agent of every voice. Re-applied after EVERY process() block,
// because reset()/setSeed()/steal clear dormancy (ecosystem_engine.h reset; vorago_voice.h:1608).
static void silenceColony(VoragoEngine& e) {
    for (std::size_t v = 0; v < VoragoEngine::kMaxVoices; ++v) {
        auto& eco = e.voices_[v].ecosystem_;
        for (std::size_t i = 0; i < eco.getAgentCount(); ++i) eco.setAgentDormant(i, true);
    }
}
// FR-007 validity: true iff every rendering voice's every agent output is exactly 0.
static bool colonySilent(const VoragoEngine& e);
// FR-005: mean over rendering voices' agents of getAgentOutput(i); -1 if none render.
static double meanColonyOutput(const VoragoEngine& e);   // uses e.isRendering(v) (:1345)
// SC-005: the limiter-reduction twin. At +60 dB `required` is 1 on every sample
// (true_peak_limiter.h:148-150), so currentGain_ never leaves its reset value 1.0f
// (:154-157) and the twin's output is the pre-limiter signal u.
static void setLimiterCeilingDb(VoragoEngine& e, float db) { e.limiter_.setCeilingDb(db); }
```

**Why dormancy and not depth.** The macro apply rewrites every voice's depth at the **start** of each
`process()` (`processor.cpp:356` → `vorago_macro_matrix.h:1103`). A friend depth write after a block is
therefore undone before the next block renders. Dormancy is never written by the macro path, so it
survives. Its gate ramps to exactly 0 within `rampSteps` publishes (`ecosystem_engine.h:2323-2329`), and
output is `clamp(raw * gate)`, so it is exactly 0 once the gate reaches 0. It is re-applied every
block, so the seed change delivered in block 0 cannot undo it.

### 4.2 `renderOnce` changes

- New parameters: `bool colonyOff` and `bool unlimited`, plus a returned per-minute record.
  - `colonyOff` calls `silenceColony` after every `process()`. From the first M1 block to the end of
    M3 it also checks `colonySilent` after every block, and records `offValid &= ...`.
  - `unlimited` calls `setLimiterCeilingDb(engine, +60.0f)` once after `setActive(true)`. It skips the
    `peak ≤ 0.9661` check for that render only.
- `RenderResult` gains:
  - `std::array<double,3> minuteRmsDb`: `stereoRmsDb` over `l.subspan(m*kMinuteSamples, kMinuteSamples)`
    (FR-004);
  - `std::array<double,3> minuteColony`: the mean of `meanColonyOutput` sampled once per processed block
    inside each minute (FR-005);
  - `bool offValid`;
  - `std::vector<float> capL, capR`, kept **only** when the caller asks for them (the GR twin needs
    both captures; every other render drops them to hold memory at one 180 s stereo pair).
  - `m1RmsDb` stays and equals `minuteRmsDb[0]`.

### 4.3 Reference modes (FR-007)

- `VORAGO_PROBE_REF=off` selects the true-off render (`colonyOff = true`, no parameter change).
- Any other value keeps today's parameter-override behaviour.
- Whenever the knob filter is not `"-"`, the true-off reference is **always** rendered (FR-004 (iii)).
- When `VORAGO_PROBE_SURFACE` is empty (the default surface), the probe also renders `900=0` and
  prints `d(true-off, 900=0)/t0` (the FR-007 cross-check).
- A reference with `offValid == false` fails the run: `REQUIRE(ref.offValid)` (FR-006).

### 4.4 Limiter gain reduction (SC-005, FR-030) — `VORAGO_PROBE_GR=1`

The base render is repeated with `unlimited = true`. The processor chain is deterministic and nothing
reads the limiter output back (`processor.cpp:1098-1113`: engine → cavern → gain → output stage, which is
last). The two renders are therefore sample-identical up to the limiter. Over the whole capture
(M1–M3):

```
GR_dB = max over samples i with max(|yL|,|yR|) > 1e-6 of
        20·log10( max(|uL_i|,|uR_i|) / max(|yL_i|,|yR_i|) )     (y = shipped, u = unlimited)
```

The per-sample formula stays valid with the limiter's release (§1.6): it measures the gain actually
applied to sample i, whatever state produced it. The probe prints:

- `GR max` in dB (the SC-005 figure) and the time of that sample;
- the fraction of samples with GR > 0.01 dB. **This counts release tails**, not only the samples the
  limiter caught. The 0.01 dB threshold sits above the float release stall (about −0.001 dB at
  48 kHz, §1.6), so the stall alone does not inflate the fraction;
- the number of attack events (samples where the per-sample gain falls by more than 0.01 dB from the
  previous sample), and the index of the first sample where `y != u` (the first engagement), or
  `none`.

**Self-check (validity).** The premise "released gain = identity" does not hold for this limiter:
after any engagement the one-pole release leaves `g < 1` on every later sample (§1.6). The check is
therefore built only from properties the shipped limiter guarantees on **every** sample. Both renders
come from the same binary in the same run, so this is a same-run check, not a golden.

1. **Never amplifies:** for every sample with `max(|uL|,|uR|) > 1e-6`,
   `max(|yL|,|yR|) ≤ max(|uL|,|uR|) · (1 + 1e-6)`. The gain is `≤ 1` (`:154-157`).
2. **One linked gain:** for every sample with `min(|uL|,|uR|) > 1e-3`,
   `|yL/uL − yR/uR| ≤ 1e-5`. The limiter applies the same `currentGain_` to both channels
   (`:159-160`).
3. **Identity before first engagement:** until the first sample where the limiter reduces, its gain
   is exactly the reset value 1.0f, and `required = 1` adds `0 · releaseCoeff_` (`:156-157`). So every
   sample before the reported first-engagement index satisfies `y == u` exactly. This follows from the
   index's definition; it is printed, not asserted.

Checks 1 and 2 fail if the two renders diverge upstream of the limiter (a non-deterministic chain, a
misaligned capture or a wrong channel pairing). Those are the defects that would make GR meaningless.
They pass on every render in which the limiter engages, including the ones SC-005 needs to measure.
A failure of 1 or 2 fails the run. `GR ≤ 1 dB` is **read from the log** by compliance (SC-005). The GR
render is also run on the FR-001 before tree, so the before value exists.

### 4.5 Flags and verdict lines (FR-004, FR-005, FR-010–FR-014)

For each extreme, against the base render:

| Output | Definition |
|---|---|
| `d` | as today |
| `d/t0` | as today, with `t0 = d(true-off, true-off seed twin)` (ruling 2026-09-28, spec Clarifications "Build stage"; the ecosystem-on twin distance is printed as `t0on`) |
| `dOff/t0` | `descriptorDistance(extreme, trueOff) / t0` |
| `rms[m]` | stereo RMS of minute m |
| `ΔRMS[m]` | `rms[m] − base.rms[m]` |
| `colony[m]` | mean colony output in minute m |
| `colony[m]/base.colony[m]` | ratio to the base render's same minute |
| **KILL** | `∃m: |ΔRMS[m]| > 6 dB || rms[m] < −60 dBFS` |
| **OFF-LIKE** | `dOff < t0` |
| **INAUDIBLE** | `d < 2·t0` |

- **Descriptor dump (FR-030, SC-005).** Every run (Gate-1 and table) prints one line for the base
  render and one for the true-off reference, from `RenderResult::d` (the minute mean,
  `ecosystem_rule_probe_test.cpp:356-360`):

  ```
  DESCRIPTOR <default|trueoff> band=b0,b1,b2,b3,b4,b5,b6,b7,b8 motion=… flux=… corr=… energySpread=… crest=…
  ```

  These are the 14 `PresetDescriptor` components (`preset_test_support.h:40-54`: `kDescriptorBands = 9`
  plus five scalars), printed `%.6f`. The existing output prints none of them: its `printf` calls
  (`:394-532`) cover only RMS, `t0`, distances and the table. So `before_gate1_default.log` and
  `after_gate1_default.log` each carry the full descriptor, beside the existing `default M1 stereo RMS`
  (`:470`) and `t0` (`:471`) lines.
- A **counted** extreme is `d ≥ 2·t0 && !KILL && !OFF-LIKE`. A knob's best is its best counted extreme.
- The printed lines:
  - `GATE1 surface=<…> t0=… d(on,off)=… d/t0=… verdict=PASS|FAIL`
  - `audible non-kill knobs: N of 14`
  - `GATE2 verdict=PASS|FAIL (N >= 4)`, printed only when the run is the default surface with a full
    table.
- The Phase 14 roster/`STOP` block is replaced by the FR-034 hand-off line: `table for Phase 14 Q2 — no roster named here`.
- Assertions are unchanged except for the added `offValid` and the §4.4 GR validity checks 1 and 2.
  Verdicts are **not** asserted (FR-006).

### 4.6 Run commands (captured to logs; one run at a time, nothing else executing)

```bash
EXE=build/windows-x64-release/bin/Release/vorago_tests.exe
A=specs/vorago-phase13b-ecosystem-audibility/artifacts
VORAGO_PROBE_KNOBS=- VORAGO_PROBE_REF=off VORAGO_PROBE_GR=1 "$EXE" "Vorago_EcosystemRuleProbe" > $A/<tag>_gate1_default.log 2>&1
VORAGO_PROBE_KNOBS=- VORAGO_PROBE_REF=off VORAGO_PROBE_SURFACE=109=1 "$EXE" "Vorago_EcosystemRuleProbe" > $A/<tag>_gate1_lifemax.log 2>&1
"$EXE" "Vorago_EcosystemRuleProbe" > $A/<tag>_table_default.log 2>&1                           # FR-012/FR-013
VORAGO_PROBE_SURFACE=109=1 "$EXE" "Vorago_EcosystemRuleProbe" > $A/<tag>_table_lifemax.log 2>&1 # FR-013
```

`<tag>` is `before`, `L<n>` or `after`. A full table is about 30 renders of 340 s. The wall-clock of the
first before-run is recorded, and later runs are estimated from it.

---

## 5. Test plan

### 5.1 New friend — `detail::VoragoEcosystemLeverProbe`

It is **defined once**, in `dsp/tests/unit/systems/vorago_ecosystem_lever_test.cpp`. No other TU of
`dsp_systems_tests` defines it. The `[long]` TU (§5.3) uses the public API only.

```cpp
struct VoragoEcosystemLeverProbe {
    using Lanes = std::array<std::array<float, VoragoVoice::kMaxSlotsPerKind>, EcosystemEngine::kNumKinds>;
    static void injectEco(VoragoVoice& v, const Lanes& eco) { v.injectedEco_ = eco; v.ecoInjectionActive_ = true; }
    static void clearInjection(VoragoVoice& v) { v.ecoInjectionActive_ = false; }
    static void injectEcoAll(VoragoEngine& e, const Lanes& eco);        // every voices_[v]
    static void advanceLifeOnly(VoragoVoice& v) { v.advanceOneChunkLifeOnly(); }  // 64 samples of identity layer
    static EcosystemEngine& ecosystem(VoragoVoice& v) { return v.ecosystem_; }
    static void gatherEco(const VoragoVoice& v, Lanes& out);            // v.gatherEcosystemLanes -> out
    static bool slotAddressed(const VoragoVoice& v, std::size_t kind, std::size_t slot); // agentValid_/agentSlot_
    static float noiseLevelBase(const VoragoVoice& v) { return v.noiseLevelBaseDb_; }
    static float loopGainBase(const VoragoVoice& v) { return v.loopGainBase_; }
    static float peakWakeBase(const VoragoVoice& v, std::size_t p) { return v.peakWakeBase_[p]; }
    static float loopWakeBase(const VoragoVoice& v, std::size_t l) { return v.loopWakeBase_[l]; }
    static float noiseWakeBase(const VoragoVoice& v, std::size_t s) { return v.noiseWakeBase_[s]; }
    // SC-021: the production shaping function itself (private static, §2.4).
    static float shapeLeverInput(float lane, float gain) { return VoragoVoice::shapeLeverInput(lane, gain); }
    // FR-019/FR-020: the scheduler lanes the LAST publish used, recomputed without side effects.
    static void schedLanes(const VoragoVoice& v, Lanes& out);
    static std::uint8_t lastEventTarget(const VoragoVoice& v, std::size_t k) { return v.lastEventTarget_[k]; }
    static void setLimiterCeilingDb(VoragoEngine& e, float db) { e.limiter_.setCeilingDb(db); }
};
```

`VoragoVoice::k*` lever constants are private and reached through the friend as `v.k…`.

**`schedLanes`.** `gatherSchedulerLanes` cannot be called from a test: it has side effects (it draws
`slotDrawRng_`, writes `drawnSlot_`, `lastEventTarget_` and `eventWasActive_`, and fires
`bloom_.triggerBloom()`; `vorago_voice.h:1868-1891`). `schedLanes` zeroes `out` and recomputes only the
**const second half** of that function: for each scheduler k with `v.scheduler(k).isEventActive()`
(`:1519-1521`; getters `slow_event_scheduler.h:331,356,361`), a family below `kNumEventFamilies` and not
`BloomTrigger`, it writes
`out[kindForFamily(family)][min(v.drawnSlot_[k], kMaxSlotsPerKind - 1)] = max(out, max(0, getCurrentValue()))`.
This equals the `lanes.sched` of the last publish because `publishIdentity()` is the **last** reader of
scheduler state in `advanceOneChunkLifeOnly()`: the schedulers advance before it (`:2276-2281`) and
nothing touches them after it. It is therefore valid only immediately after `advanceLifeOnly`, which is
how every case uses it.

**The control-rate fast path.** The criteria below that observe **control values**, not audio, drive
the voice with `advanceLifeOnly`: SC-006, SC-007, SC-008, SC-020 and the SC-018 rate arms of
SC-006/SC-008. This is the same `publishIdentity()` the render path runs (FR-022). Parity is proven
once by `VoragoVoice_EcosystemLeverLifeOnlyParity`. This keeps 10-minute windows at three rates
affordable per push.

### 5.2 `dsp/tests/unit/systems/vorago_ecosystem_lever_test.cpp` (per-push)

Tags `[systems][vorago]`. It uses `detail::isFinite` bit-pattern checks only (no `std::isnan`), and
voices and engines are heap-allocated (`vorago_voice.h:2434-2435`).

| Case | FR / SC | Strategy and assertions |
|---|---|---|
| `VoragoVoice_EcosystemLeverMapping` | FR-015, FR-017, FR-019, FR-020, §3.1 | Prepared voice, noteOn, no render. The injection seam covers only `lanes.eco`; the schedulers keep running (§2.5), so sched is **observed, not forced**. `setEventRateScale(10.0f)` so scheduler events occur. For each injected uniform `L ∈ {0, 0.25, 0.5, 1}`, run 10 min of `advanceLifeOnly` (the injection is read at publish). After **every** chunk, read `S = schedLanes(v)`. **REQUIRE exact** `getSourceLevel(s) == clamp(base + span·L)`, and the same for `getPeakLevel`, `getLoopGain` and (if §2.8 shipped) `getCoupling(l,(l+1)%n)` / `getFreqWander`, **whatever S is** (FR-019). Each wake getter equals `combineWake(base, L, S[k][slot])` exactly (FR-020). Expected values use the **same float expression**, so exact comparison is sound. Non-vacuity: REQUIRE at least one chunk per L with `S[k][slot] > L` on some wake kind (sched decides the wake) and at least one with `S` all 0; counts printed. At L = 0 a chunk with sched > 0 must read every lever at base exactly (FR-019). |
| `VoragoVoice_EcosystemLeverNeutral` | SC-006, SC-018 | For fs ∈ {44100, 48000, 96000}: `setEcosystemDepth(0)`, schedulers as shipped, noteOn. Then 10 min of `advanceLifeOnly` (fs·600/64 chunks). After **every** chunk, each lever destination `==` its base: noise `getSourceLevel(s) == noiseLevelBase`, peak `== kPeakLevelBaseDb`, loop `getLoopGain(l) == loopGainBase`, coupling/wander `==` their bases when shipped. It counts scheduler events seen (non-vacuity: at least 1 over 10 min, printed). |
| `VoragoVoice_EcosystemLeverSchedulerBlind` | SC-007 | Depth 0 and `setEventRateScale(10.0f)` (FR-086: interval clock only; ranges 2–9 s and 18–60 s). Seed fixed and printed. 10 min of `advanceLifeOnly`. Count onset edges per family via `lastEventTarget` changes, plus `isEventActive()` rising edges (`scheduler(k)`). **REQUIRE** NoiseWake ≥ 1, PeakWake ≥ 1 and LoopWake ≥ 1, then the neutral assertions. A seed that misses a family fails the test (it is not skipped). Counts are printed. |
| `VoragoVoice_EcosystemLeverBaseReadBack` | SC-009, FR-021 | After prepare, check `getNoiseLevelDb() == -18` and `getEcologyLoopGain() == 0.72`. Inject L = 1 and advance. Then for each of the values {−30, −12, 0} dB and {0.5, 0.8}: call the setter and check that the getter returns exactly the written (clamped) value, while the component getter reads the modulated `clamp(base+span)`. Also check: a non-finite write leaves the previous value; the same value written twice leaves `getSourceLevel` unchanged and does not re-arm; and an early-out on the shadow (E-7). |
| `VoragoVoice_EcosystemLeverReset` | SC-012, FR-022, E-11, E-13 | Voice A: prepare, noteOn, inject L = 1, advance 200 chunks. Then run each of `reset()`, `resetForSteal()` (after `silence()`) and `resetForRecovery()`, and also a second `prepare()` at 96 kHz → 48 kHz. After each, compare every lever destination getter, every `*Offset_` and (if §2.8 shipped) `freqWanderApplied_` to a **freshly prepared voice B** (same seed). REQUIRE equality, then inject 0 (`clearInjection` + depth 0) and render 1 s with A and B. `compareFingerprints` must be `withinTolerance()`. |
| `VoragoVoice_EcosystemLeverLifeOnlyParity` | FR-022 | Two voices with the same seed and depth 1. A renders 30 s via `processStereoBlock(…, 64)` and B advances via `advanceLifeOnly`. After each chunk, every lever destination getter and the three wake getters must be equal (exact: same code, same inputs). |
| `VoragoVoice_EcosystemLeverAttribution` | SC-008, SC-018 | Per fs ∈ {44100, 48000, 96000}: depth 1 (`setEcosystemDepth(1)`), noteOn, a 155 s warm-up by `advanceLifeOnly`, then one minute per kind K ∈ {Noise, Resonator, Feedback}. (1) In the minute before, count per lever the value changes ≥ min step (level 0.5 dB, loop gain 0.01, coupling 0.01, wander 0.1 st, cutoff not shipped). REQUIRE ≥ 3 for **every** kind's lever. (2) `setAgentDormant(i, true)` for all agents of kind K (via `ecosystem(v)`). After `T_settle` (§3.3, computed from `getStepIntervalChunks()`, printed), REQUIRE K's lever `==` base on every chunk to minute end, while the other kinds still meet ≥ 3 changes in that minute. Then un-dormant K and move to the next kind. If some lever never reaches 3 changes/min at depth 1, that is a finding against the spans (§3.5), not a test edit. |
| `VoragoVoice_EcosystemLeverClickFree` | SC-010(a) | 48 kHz, fixed seed, one voice, pinned `ClickDetectorConfig{.sampleRate = 48000.0f, .frameSize = 512, .hopSize = 256, .detectionThreshold = 5.0f, .energyThresholdDb = -60.0f, .mergeGap = 5}`. **Stress arm:** after a 20 s warm-up (fast attack via stage times, as `applyFastAttack` does), inject a schedule. For each K in {Noise, Resonator, Feedback, Partial, Ghost}, one at a time and then all together: 1 s at 0, 1 s at 1 on every slot of K, 1 s at 0. Render it via `processStereoBlock`. **Twin:** the same seed and schedule with the injection held at 0. REQUIRE clicks(stress) ≤ clicks(twin), for L and R separately. Both counts are printed. |
| `VoragoEngine_EcosystemLeverBounded` | SC-011(a)(b), SC-018, E-4, E-5 | Engine at **full polyphony**: `setPolyphony(VoragoEngine::kMaxVoices)` (6; `vorago_engine.h:565`, `:225`), the plugin's maximum (`global_params.h:42`, clamp to [1, 6] at `:72`). Six held notes (36, 43, 48, 55, 60, 67), so every voice slot renders, and a `VoragoMacroMatrix` with Life = 1, applied every block (the `vorago_macro_test` pattern). Chain: `processStereoBlock` → `processOutputStage`. Cavern is omitted: a systems TU may not name a Layer 4 type (`vorago_perf_test.cpp:249`). **(a)** `injectEcoAll` with all 1 on all 6 voices. **(b)** natural colony, 6 voices. Before each arm, REQUIRE `isRendering(v)` for all 6 voices (non-vacuity, via the friend). Each runs 60 s at fs ∈ {44100, 48000, 96000}. REQUIRE every sample `isFinite` and `|out| ≤ 0.9661`, `getNonFiniteRecoveryCount() == 0`, and `getAllocatedBytes()` equal before and after. At 48 kHz only, the GR twin (`setLimiterCeilingDb(+60)`, §4.4 formula) prints the maximum reduction for (a) and (b) as informative. Not `[long]`: this is a bounds sentinel. |
| `VoragoEngine_EcosystemLeverDeterminism` | SC-013, FR-025 | The `VoragoEngine_DeterminismHarness` shape (`vorago_engine_test.cpp:1498-1560`), copied rather than edited (SC-014 "no test file edited"), with `setEcosystemDepth(1)` on every voice so the levers are maximally active. Seed A twice: `compareFingerprints(...).withinTolerance()`. Seed A vs A+1: worst metric > 100·`kMetricTolerance`. The shipped harness itself also exercises the levers unedited, because its default depth is 0.85. |
| `VoragoVoice_EcosystemWakeUnmasked` | SC-020, FR-018 | Added at §7 step 4 with the retuned bases, never on a tree where it is expected to fail. 48 kHz, the §3.4 seed and stimulus, depth 0.85, `advanceLifeOnly` to 340 s. On every simulation step in [155, 340] s, over addressed slots only, compute the fraction with `gatherEco > shipped base` (read through the friend's `peakWakeBase` / `loopWakeBase` / `noiseWakeBase`). It **prints** the three bases and three fractions first, then REQUIREs Resonator ≥ 0.25 and Feedback ≥ 0.25. Noise is printed only (Q1). The SC-020 **before** figures come from `…LaneSurvey` on the FR-001 tree (§3.4 step 5). |
| `VoragoVoice_EcosystemLaneShapingFidelity` | SC-021 | Guarded by `if (all kLeverInputGain == 1) SUCCEED("shaping not used")`. Otherwise, for each shaped kind (Noise, Resonator, Feedback only; §2.1): `shapeLeverInput(0.0f, g) == 0.0f` exactly through the friend (the production function, §2.4); an output in [0, 1] for 10 001 inputs on [0, 1]. Then inject `L_raw` on every kind (Partial included), `advanceLifeOnly`, read `S = schedLanes(v)`, and REQUIRE the kind's wake getters equal `combineWake(base, L_raw, S[k][slot])` bit-for-bit (the **raw** lane reaches the combine), while the lever reads `clamp(base + span · shapeLeverInput(L_raw, g))`. The Partial writes are REQUIREd unshaped (FR-016, `vorago_voice.h:1939-1941`): `cloud().getMutation()` (`:1511`; `harmonic_cloud.h:493`) `== clamp(mutationBase_ + L_raw, 0, 1)` and `bloom().getDepth()` (`:1515`; `bloom_engine.h:679`) `== clamp(bloomDepthBase_ + L_raw, 0, 1)`. |
| `VoragoVoice_EcosystemLaneSurvey` | §3.4 (diagnostic), SC-020 before | `[.probe]`, hidden, never in CI, no REQUIRE. Prints `Q_k(p)` for p ∈ {0.10, 0.25, 0.50, 0.60, 0.75, 0.90}, the addressed-pair counts, the per-kind fraction above the base currently in the tree (§3.4 step 5), and the §3.4 rule's resulting bases. |

### 5.3 `dsp/tests/unit/systems/vorago_ecosystem_lever_longrun_test.cpp` (`[long]`)

| Case | SC | Strategy |
|---|---|---|
| `VoragoVoice_EcosystemLeverClickFreeNatural` | SC-010(b) | Tags `[systems][vorago][long]`. 48 kHz, one voice at depth 1, fast attack, a 10-minute render via `processStereoBlock(…, 64)`. Record the sample index of each chunk where `ecosystem().getControlStepCount()` incremented (that chunk's start is the step boundary). Run `ClickDetector` (the §5.2 pinned config) on L and R. **REQUIRE zero detections within ±64 samples of any boundary** (Q7). Detections elsewhere are printed with their times, and the depth-0 twin's count is printed beside them (record-only). The render takes over 15 s and its failure mode is not toolchain-specific, so it qualifies for `[long]` under the project's `[long]` rule. |

### 5.4 Plugin tests (`vorago_tests`)

- `ecosystem_rule_probe_test.cpp`: extended (§4). It stays `[.probe]`.
- SC-017: `param_table_test`, `state_v2_test` and `state_roundtrip_test` are green **unedited**. No file
  under `plugins/vorago/src/` changes, so this is structural and also asserted by the git diff.
- SC-014 includes `soak_test.cpp` and `processor_audio_test.cpp` unedited.

### 5.5 FR/SC → evidence map

| ID | Evidence |
|---|---|
| FR-001(a), SC-002 before | `artifacts/before_gate1_{default,lifemax}.log`, `before_table_{default,lifemax}.log` (true-off reference, GR) |
| FR-001(b), SC-016 before | `artifacts/before_cpu.log` (`node tools/run-cpu-tests.js dsp_systems_tests`) |
| FR-002, FR-003, FR-006 | the TU diff (candidates untouched), plus the assertions in §4.5 |
| FR-004, FR-005, FR-007 | the TU plus log lines (`offValid`, per-minute cells, colony ratio, cross-check) |
| FR-010, FR-011, SC-001, SC-002 | the `GATE1` lines of `after_gate1_*.log` |
| FR-012, FR-013, FR-014, SC-003, SC-004 | the `after_table_*.log` counts, flags and cells, with the Gate 1 pass on the same commit cited |
| FR-015–FR-017, FR-019, FR-020 | `VoragoVoice_EcosystemLeverMapping`, `…Neutral`, `…SchedulerBlind` |
| FR-016 | compliance records "unchanged": `mutationBase_`, `bloomDepthBase_` and `kGhostBurstPeak` are not retuned (§0.4); Partial and Ghost are never shaped (§2.1 `static_assert`, `…LaneShapingFidelity`'s Partial check) |
| FR-018, SC-020 | before: the fraction lines of `lane_survey.log` (FR-001 tree, bases 0.50/0.50/0.35); after: the printed fractions of `VoragoVoice_EcosystemWakeUnmasked` on the final tree |
| FR-018a | the §3.5 rung log; values before and after |
| FR-019a, SC-021 | `…LaneShapingFidelity` |
| FR-021, SC-009 | `…BaseReadBack`, plus the §1.6 getter tests unedited |
| FR-022, SC-012 | `…Reset`, `…LifeOnlyParity` |
| FR-023, SC-010, SC-011 | `…ClickFree`, `…ClickFreeNatural`, `…Bounded`, plus the Phase 10 SC-004a/b soaks |
| FR-024 | code review; ODR sweep (§1.7); `static_assert` size bound |
| FR-025, SC-013 | `VoragoEngine_EcosystemLeverDeterminism`, `VoragoEngine_DeterminismHarness` |
| FR-026–FR-028, SC-015, SC-017 | `git diff --name-only 9ecd6d10..HEAD`: production files limited to `vorago_voice.h` and `vorago_engine.h`; no `plugins/seraphis/**`, no Seraphis-consumed header, no `plugins/vorago/src/**` |
| FR-030, SC-005 | the `DESCRIPTOR default` line (14 components, §4.5), `default M1 stereo RMS`, `t0` and `GR max` lines of `before_gate1_default.log` and `after_gate1_default.log`, cited side by side |
| FR-031, SC-014 | full suite logs including `[long]`; the §7 step 9 surfaced list |
| FR-032, SC-016 | before/after CPU logs: both WARN clause lines cited, delta in ns and % |
| FR-033, SC-019 | build log (0 warnings), `check-portability.js`, clang-tidy `dsp` + `vorago`, pluginval 5 |
| FR-034 | the probe's hand-off line; compliance names no roster |

---

## 6. Build integration

- **`dsp/tests/CMakeLists.txt`**: in the `dsp_systems_tests` source list, after
  `unit/systems/vorago_macro_retune_probe_test.cpp` (`:552`), add a comment block in the existing style
  and the two TUs:

  ```cmake
      # Vorago Phase 13b (specs/vorago-phase13b-ecosystem-audibility): ecosystem levers.
      # ENUMERATED, not globbed - an unregistered TU silently drops out of the build.
      #   vorago_ecosystem_lever_test.cpp          SC-006..SC-009, SC-010 (a), SC-011, SC-012,
      #                                            SC-013, SC-018, SC-020, SC-021, the
      #                                            LaneSurvey [.probe]; + the ONE definition of
      #                                            detail::VoragoEcosystemLeverProbe
      #   vorago_ecosystem_lever_longrun_test.cpp  SC-010 (b)            (the [long] set)
      unit/systems/vorago_ecosystem_lever_test.cpp
      unit/systems/vorago_ecosystem_lever_longrun_test.cpp
  ```

  Neither TU joins the `-fno-fast-math` block: they build no NaN/Inf inputs, and finiteness is checked
  by bit pattern (`detail::isFinite`).
- **Plugin**: no CMake change. The probe TU is already in `vorago_tests`.
- **Targets built and run**:
  - `dsp_systems_tests` (new and existing cases);
  - `vorago_tests` (plugin and probe);
  - for SC-014: `dsp_core_tests`, `dsp_primitives_tests`, `dsp_processors_tests`, `dsp_effects_tests`,
    `seraphis_tests` and `shared_tests`. `vorago_voice.h` / `vorago_engine.h` are included only by
    Vorago consumers, but the full sweep is the SC.
- **Commands**:
  - `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_systems_tests vorago_tests`
  - `build/windows-x64-release/bin/Release/dsp_systems_tests.exe "VoragoVoice_EcosystemLever*" 2>&1 | tail -5`
- **CPU**: `node tools/run-cpu-tests.js dsp_systems_tests`, alone, on an idle and cooled machine,
  before and after.

---

## 7. Execution order (each step verifiable)

1. **Instrument, no production change.** Apply the §4 probe edits and the §5.1 friend TU, containing
   only `…LaneSurvey` (hidden `[.probe]`, no REQUIRE). No per-push case that is expected to fail on this
   tree is committed. At this step the friend defines only the accessors that
   touch **existing** members: `advanceLifeOnly`, `ecosystem`, `gatherEco`, `slotAddressed`, the three
   `*WakeBase`, `lastEventTarget` and `setLimiterCeilingDb`. The lever-member accessors
   (`injectEco*`, `noiseLevelBase`, `loopGainBase`, `shapeLeverInput`) and `schedLanes` are added in
   step 3 with the code they test.
   The friend needs its declaration in both headers (§2.6). That edit adds a friend declaration only
   and changes no behaviour. It is committed as the instrument commit.
   → verify: build with 0 warnings, and the probe runs with `VORAGO_PROBE_KNOBS=- VORAGO_PROBE_REF=off`.
2. **FR-001 before-record** on that tree:
   - both Gate-1 runs with GR;
   - both full tables;
   - `before_cpu.log`;
   - `VoragoVoice_EcosystemLaneSurvey` → `lane_survey.log` (quantiles, and the SC-020 before fractions
     at bases 0.50/0.50/0.35).
   → verify: all logs are in `artifacts/`.
3. **§2.1–§2.6 production code** (primary levers only; §2.8 is not written here) with every span and
   constant at its **no-change** value: the §2.1 bases equal today's literals and the spans are 0.
   → verify: the full `dsp_systems_tests` and `vorago_tests` are green (the refactor is inert). The
   base-read-back, reset and parity cases pass.
4. **L1 + L2** (§3.4 bases, initial spans), and add `VoragoVoice_EcosystemWakeUnmasked` in the same
   change. → verify: the §5.2 per-push cases are green (WakeUnmasked included), then the Gate-1 pair
   and SC-005.
5. **§3.5 ladder** as needed, logging each rung. → verify: both Gate-1 arms ≥ 2.0 and SC-005 in bounds.
6. **Gate 2**: both full tables on the Gate-1-passing commit. → verify: counted ≥ 4 at the default surface.
7. **`[long]`** `…ClickFreeNatural` and the Phase 10 `[long]` set (SC-004b, overnight-soak equivalent,
   SC-019).
8. **After-CPU** run (alone). → verify: both clauses, and the delta recorded.
9. **Full regression** (SC-014). Any failure is classified:
   - (a) a behavioural bound: this is a defect, fix the code;
   - (b) old voicing encoded as data: this is **stop and surface** (FR-031), listed with the reason
     before any re-measurement.

   Watch-list for (b), from reading only (not yet run): the Phase 10 macro-axis deltas in
   `vorago_macro_test.cpp`, the SC-005 centroid-CV soak in `vorago_engine_longrun_test.cpp`, and any
   plugin render-level threshold in `processor_audio_test.cpp` / `soak_test.cpp`.
10. **Cross-cutting**: `node tools/check-portability.js`; `./tools/run-clang-tidy.ps1 -Target dsp` and
    `-Target vorago`; pluginval strictness 5 on `Vorago.vst3`; the git-diff scope check.

---

## 8. Risks and mitigations

| Risk | Mitigation |
|---|---|
| **Loudness lever trips SC-005** (M1 ±3 dB, GR ≤ 1 dB): upward levels raise RMS. | Lowered wake bases pull the other way. L3 rebalances with the peak-level base (the one live voice-owned level). The GR twin measures reduction exactly. The ladder never uses loudness as the Gate-1 lever: SC-005 is checked at every rung. |
| **FR-018a retune is inert in the plugin** for macro-owned values (§1.5). | Excluded by decision (§0.4). Only peak level, freq wander and ring coupling are retuned. |
| **Loop gain lever saturates at Pressure = 1** (base 0.88 vs clamp 0.90). | Recorded as informative for Phase 14. Coupling (L4) has headroom there. The governor and `kMaxTotalLoopGain` stay the authority (E-4). SC-011(a) runs at Life 1 with levers at maximum. |
| **Ramp re-arm stretch** (FR-067): `setSourceLevel` / `setPeakLevel` re-arm on every call. | Every write early-outs against the component's stored target (§2.3, §2.4). The lanes change once per 8 chunks. |
| **Re-prepare leaves the component at its own default** when the shadow early-outs. | The helpers compare against the component getter. `installIdentityNeutral` in `reset()` writes before the ramp snap. SC-012 has a re-prepare arm. |
| **Dormancy edges from lowered bases** → feedback-loop sleep clears audio (Phase 5 FR-063) and would burst. | `kMinRetunedWakeBase = 0.05 ≫ kWakeSilenceEpsilon = 1e-6` (static_assert), so colony-driven wakes never reach sleep. |
| **Unsmoothed destinations** (E-6). | Colour and gain wander are excluded (§1.4). Freq wander ships only if L4 adds it, and then only with the voice slew (§2.8). Levels, gain and coupling ride their component ramps (§1.3). |
| **Seam leaks into production.** | `ecoInjectionActive_` is written only by the friend, which is defined in one test TU. `false` in all builds. `…LifeOnlyParity` and the unchanged SC-019 cases run with it false. |
| **`-ffast-math` and NaN.** | No `std::isnan`. The finiteness checks use `detail::isFinite` (bit pattern). The setters already reject non-finite input (E-14). Exact-equality assertions compare identical float expressions evaluated by the same code, so they are not tolerance-sensitive. |
| **Brace-init narrowing / aligned SIMD.** | No SIMD is added. Constants are `float` literals with an `f` suffix. `std::array` inits use float literals. `node tools/check-portability.js` runs before commit (MSVC green proves nothing). |
| **Voice size bound.** | About 360 B added against about 5.9 KB of headroom. If the `static_assert` fires, stop and surface. |
| **Probe cost** (about 30 renders × 340 s per full table, two tables per ladder exit). | Gate-1-only runs (3 renders) during the ladder. Full tables only at step 2 and step 6. Logs are captured to files. Wall-clock comes from the first run. |
| **SC-011 per-push cost** (engine 60 s × 2 arms × 3 rates plus the 48 kHz GR twins, at **6 voices**; the engine runs at about 25 % of one core at 4 voices per `vorago_perf_test.cpp` notes, so 1.5× that at 6 voices, roughly 3–4.5 min — an estimate to be replaced by the measured wall-clock). | Kept per-push as the spec rules. The measured wall-clock is recorded. If it proves unacceptable for CI, that is surfaced to the user (see open questions). It is **not** silently tagged `[long]`: CLAUDE.md forbids tagging bounds sentinels. |
| **Gate 2 unreachable with levels alone** (knobs that change only trajectories stay under 2·t0). | §3.2's rationale targets statistics. Ladder L4–L6 increases sensitivity. L7 stops and surfaces with the full table. No range or candidate is widened (FR-003). |
| **SC-020 base floor**: if `Q(0.60) < 0.05` the floor rule cannot reach 25 %. | Detected at step 2 from the survey, before any code change, and surfaced then. |
| **Old voicing encoded as data in earlier suites.** | Classified per §7 step 9. Stop and surface, never silently re-pinned. |

---

## 9. Items left to the user (not decided by this plan)

- ~~If the ladder reaches **L7**~~ **Ruled R-3 (2026-09-27):** L8 = the parameter-301 retune is
  pre-authorised (§3.5); a macro-row base change and an `EcosystemEngine` change stay out; the run
  stops and surfaces only if L8 also fails.
- ~~If `VoragoEngine_EcosystemLeverBounded`'s measured per-push wall-clock is judged too costly~~
  **Ruled R-2:** it stays per-push at 60 s; the wall clock is printed and cited; above 4 minutes
  measured locally the build stops and surfaces the figure. Never shortened, split or tagged `[long]`.
- **Ruled R-1:** the CMake registration split (T005 registers, T037 re-verifies) is accepted.

---

## Review notes

Revision of 2026-09-27 against the plan review (9 issues). No issue was rejected, and no threshold was
relaxed.

- **Limiter model (two majors, same defect).** §0.6 and §1.6 now describe the instantaneous attack,
  the 80 ms one-pole release, the float release stall and the 4x-oversampled detection
  (`true_peak_limiter.h:47`, `:91-95`, `:141-160`). The §4.4 identity self-check is replaced by two
  checks that hold on every sample for this limiter (never amplifies, one linked gain). The
  per-sample `GR_dB` formula and the SC-005 ≤ 1 dB gate are unchanged. The GR fraction is labelled as
  including release tails.
- **FR-030 descriptors.** §4.5 adds the `DESCRIPTOR` line, and §5.5 cites it.
- **SC-011 full poly.** `…LeverBounded` runs at `kMaxVoices` = 6 with six notes; §8's cost estimate
  is scaled to match.
- **Mapping / fidelity sched.** Chosen resolution: observe the live `lanes.sched` through a
  side-effect-free `schedLanes` accessor rather than add an `injectedSched_` production member. This
  keeps the production seam at eco-only (FR-023). `shapeLeverInput` is a private static member, not
  a lambda.
- **Partial shaping.** Removed. L6 shapes only Noise, Resonator and Feedback, enforced by a
  `static_assert` and a Partial check in `…LaneShapingFidelity`.
- **Ladder levers.** Moved to §2.8 and written only if L4 is entered. This includes the
  `kFreqWanderBaseSemis` / `kRingCouplingBase` constants, whose literals stay in place until then.
- **WakeUnmasked red at step 1.** It is added at step 4 with the retuned bases and prints before it
  REQUIREs. The before fractions come from the hidden, REQUIRE-free `…LaneSurvey`.
- **Citations.** `setEcosystemDepth(0.85f)` is at `:624` and step 8 is at `:723-724`. Both were
  re-read this session.
