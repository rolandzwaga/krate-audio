# Implementation Plan: Vorago Phase 10 — Voice & Engine

**Spec:** `specs/vorago-phase10-voice-engine/spec.md`
**Roadmap:** `specs/Vorago-roadmap.md` → Part A → Phase 10 (lines 446–474), Open Questions 5 and 6
(lines 588–589), Open Question 4 (already decided in Phase 6, lines 584–586).
**Deliverables:** three new Layer 3 headers — `dsp/include/krate/dsp/systems/vorago_voice.h`,
`vorago_engine.h`, `vorago_macro_matrix.h`; **two** append-only extensions to shipped Layer 3
headers — `dsp/include/krate/dsp/systems/continuous_body.h` (six dark materials, S4) and
`dsp/include/krate/dsp/systems/feedback_ecology.h` (one RT-safe `silenceAudio()`, B-7 / S3.2a);
one new test-helper header;
one Node generator for the three new mode-ratio tables; **nine** new test TUs (**eight** in
`dsp_systems_tests`, one in `dsp_effects_tests`); edits to three Seraphis-owned test TUs
(the `kNumMaterials`-sized sites, FR-038).
**Plugin work:** none — Phase 11 owns all of it.

---

## S0. Verification ledger — every signature in this plan was read this session

No API below is recalled. Each row was opened at the cited line during planning; if a line number
here disagrees with the file, the file wins and the plan row is the defect.

### S0.1 Pattern template (Seraphis Phase 7)

| Fact | Location (verified) |
|---|---|
| `class SeraphisVoice` | `systems/seraphis_voice.h:127` |
| `enum class EnvelopeMode : std::uint8_t { Standard = 0, Growth = 1 }` — class-scoped | `:137` |
| `kControlChunkSamples = 64`, `kMaxBlockSamples = 2048`, `kTailSilenceThreshold = 1.0e-5f` (−100 dBFS), `kLevelReleaseMs = 100.0f`, `kSilenceRampMs = 1.0f`, `kQuiescentChunksToRetire = 4` | `:144`, `:146`, `:148`, `:151`, `:155`, `:157` |
| `kStageCurve = EnvCurve::Exponential`, `kEnvelopeStages = 4`, `kEnvelopeSustainPoint = 2` | `:167`, `:170`, `:172` |
| Seed salts `kCloudSalt = 0x0100 … kOrbitSalt = 0x0500` + `static_assert` they are pairwise distinct | `:179-184` |
| `kVoiceSizeBound = 50048`, doc records **measured** `sizeof(SeraphisVoice) = 47 616 B` (g++ 13.3, `-O1`) and the bound rule `ceil(measured × 1.05)` rounded up to 64 B | `:186-204` |
| Non-copyable AND non-movable, *stated* because `ContinuousBody` deletes copy and declares no move members (`continuous_body.h:647-648`), so a `= default`ed move would be defined as deleted | `:220-226` |
| `prepare(double, const SeraphisVoiceConfig&)` — rate floor, clamp-never-reject, sub-component prepare, `applySeeds()`, defaults, derived constants, `prepared_ = true; reset();` | `:236-385` |
| `reset()` (clears the fade tail) vs `resetForSteal()` (preserves it) | `:392-405` |
| `silence()` — captures `lastOut*` into `fadeTail*`, arms `fadeRemaining_`, hard-clears sub-components, discards the carry | `:424-438` |
| `processStereoBlock` guard ladder (null → write nothing; `n == 0` → no control step; `!prepared_` → zeros) then the carry-FIFO serve loop; `lastOut*` captured **at serve time** | `:451-484` |
| `advanceLifeOnly(std::size_t)` on the same carry clock | `:492-506` |
| `noteOn(float, float)` / `noteOff()`; the documented `ContinuousBody` [20, 8000] vs `HarmonicCloud` [20, 4000] clamp divergence | `:512-546` |
| `renderOneChunk()` — numbered steps 1…10, envelope **in place on the cloud**, "Nothing downstream of this point is gated" | `:1035-1141`, the gating note at `:1066` |
| `advanceOneChunkLifeOnly()` — spatial stage on a zeroed bus, `updateLevel(0)`, carry refilled with zeros | `:1157-1176` |
| `updateLevel(float)` — instant attack, `levelReleaseCoeff_` release, `quiescentChunks_` counter | `:1178-1183` |
| `levelReleaseCoeff_ = exp(-64 / (0.001 × kLevelReleaseMs × fs))` — **not** `calculateOnePolCoefficient`, which treats its argument as time-to-99 % | `:369-373` |
| `silenceRampSamples_ = max(1, lround(0.001 × kSilenceRampMs × fs))` | `:374-377` |
| `quiescentChunks_` is seeded **at the retire value** in the shared clearing body (`clearRunState()`, S3.2), so a never-rendered slot is `isFinished()` from the start | `:1022-1027` |
| `friend struct detail::SeraphisVoiceSilenceRampProbe;` + the forward declaration | `:898`, `:99` |
| `static_assert(sizeof(SeraphisVoice) <= kVoiceSizeBound, …)` below the class | `:1297-1301` |
| `class SeraphisEngine` | `systems/seraphis_engine.h:205` |
| `kMaxVoices = 16`, `kControlChunkSamples = 64`, `kVoiceSaltBase = 0x9000`, `kSumGainSmoothMs = 100.0f` (with the measured 20 ms→2.651 / 100 ms→1.143 knee table), `kAmnestyLevelThreshold = 0.0316f` ("−30 dBFS long-release steal amnesty threshold"), `kOutputSaturation = 0.15f`, `kOutputDriveDb = 0.0f`, `kResetsPerControlChunk = 1` | `:211`, `:213`, `:217`, `:219-246`, `:247-248`, `:250`, `:252`, `:254` |
| `kEngineSizeBound = kMaxVoices * SeraphisVoice::kVoiceSizeBound + 64 KiB`, with the argument for why it is expressed against the voice bound rather than a frozen byte count | `:264-287` |
| `setPolyphony(std::size_t)` — clamp, walk `allocator_.setVoiceCount()`'s NoteOff events, orphan-tail bookkeeping, `sumGain_.setTarget(sumGainForPolyphony(n))` | `:423-457` |
| `setSeed` — `voices_[v].setSeed(deriveStreamSeed(seed_, kVoiceSaltBase + v))` | `:460-465` |
| `processStereoBlock` — the **absolute** control grid (`sampleCounter_ % 64`), pre-render control step at phase 0, `slice = min(n - done, 64 - phase)`, per-voice render or `advanceLifeOnly`, non-finite detection **at the accumulation point** with a deferred reset, `sumGainHeld_` read once per chunk, post-render control step on the slice that completed a chunk | `:530-608` |
| `processOutputStage(float*, float*, std::size_t)` — "the caller runs this AFTER its reverb … in the composed chain the buffer is the AetherReverb return"; `satL_/satR_.process` per channel, `limiter_.processBlock` always last over the whole block | `:610-634` |
| `runPreRenderControlStep()` — `sumGain_.advanceSamples(63)` then `process()` (the `- 1` is load-bearing), staggered deferred work, `kResetsPerControlChunk` non-finite recoveries | `:1149-1206` |
| `runPostRenderControlStep()` — deferred retirement on the absolute grid (`allocator_.voiceFinished(v)` once `isFinished()`) | `:1215-1265` |
| `clearRunState()` — output stage reset, `sumGain_.snapTo`, `sampleCounter_ = 0`, pending masks cleared | `:1267-1311` |
| `noIdleVoice()` / `freeChosenVictimSlot()` — the three-pass victim selection (pass 0 Releasing **below** the amnesty threshold, pass 1 Releasing at any level, pass 2 Active), `!(level < threshold)` so a non-finite level is not "quietest", tie-break on the engine-owned `voiceSerial_` | `:1313-1425` |
| `isRendering(v)` — `state != Idle || !voices_[v].isFinished()` | `:1141-1147` |
| `friend class SeraphisMacroMatrix;` (the matrix needs non-const voice access), `friend struct detail::SeraphisEngineNonFiniteProbe;` | `:1070`, `:1074` |
| The non-finite probe is a **friend struct declared in the header's `detail` namespace and DEFINED IN THE TEST TU** — no macro, no class-definition change | `seraphis_engine.h:195`, definition at `dsp/tests/unit/systems/seraphis_nonfinite_test.cpp:107-111` |
| `class SeraphisMacroMatrix`; `struct SeraphisMacroRow { macro; owner; target; base; amount; curve; }` with `base` documented as "The FR-019 shipped **VOICE** default, read off `SeraphisVoice::prepare` step 6" | `seraphis_macro_matrix.h:182`, `:159-174`, `:160-163` |
| `struct SeraphisAetherTargets` — the Layer-4 defaults **duplicated as literals with their source lines**, because `AetherReverb`'s `private:` hides them and a Layer 3 header may not name a Layer 4 type | `:105-133` |
| `kRows` — `static constexpr std::array<SeraphisMacroRow, kNumRows>` | `:213` |
| `contributionOf(row)` — unipolar `amount × curve(m)`; bipolar `Gravity` `g = (m − 0.5) × 2`, `amount × curve(|g|) × sign(g)` | `:929-938` |
| `evaluateAll()` — `acc = base(target)` seeded once per target, then one contribution per row; **no** `if (neutral) return;` fast path, and the reason | `:940-957` |
| The six `constexpr` table predicates and their `static_assert`s below the class: `everyRowOwnerIsValid` `:585`, `everyAetherRowHasAPodField` `:613`, `everyEffectsRowHasAPodField` `:626`, `noRowUsesSteppedCurve` `:641`, `everyTargetInFr061to065IsPresent` `:654`, `everyRowSharesOneBasePerTarget` `:676`; asserted at `:972-995` | as cited |
| `setMacro` — non-finite → the macro's neutral, then `clamp(0, 1)`; `Count`/`default` → silent no-op | `:700-723` |
| `apply(SeraphisEngine&)` — `evaluateAll()` once, then one plain setter write per target per voice over `i < getPolyphony()` | `:769-812` |

### S0.2 Sub-components Phase 10 composes

| Component | Fact (verified) |
|---|---|
| `HarmonicCloud` | `class` at `systems/harmonic_cloud.h:127`. `prepare(double)` `:282`; `reset()` `:313`; `setFundamentalHz` `:383`; `setRichness` `:412`; `noteOn()` `:635`; `noteOff()` `:663`; `setSeed` `:701`; `setSpectralTarget(const float*, const float*, std::size_t)` `:800` (doc `:730-799`); `clearSpectralTarget()` `:862`; `hasSpectralTarget()` `:869`; `processStereoBlock(float*, float*, std::size_t)` `:878`; `getActivePartialCount()` `:950`. |
| `HarmonicCloud` laws | `N(r) = clamp(round(64^r), 1, 64)` `:1462-1463`; `p(r) = kRichnessMinExponent + (kRichnessMaxExponent − kRichnessMinExponent)·r = 3.0 − 2.5r` `:200-202`, `:1466-1469`; `kMaxDriftCents = 50` `:214`; `kMinAttackSec/kMaxAttackSec = 0.05/30`, `kMinDecaySec/kMaxDecaySec = 0.05/60` `:218-221`; `kMinTiltDbPerOct/kMaxTiltDbPerOct = −12/+12` `:193-194`; `kMaxInharmonicity = 0.1` `:191`. |
| **The neutrality mechanism** | With a target active the cloud's **frequency** path takes the parametric branch whenever `targetRatio_[i] == n` — the FR-082 identity guard at `:1311-1330` — so `ratios[i] = i + 1` leaves gravity/inharmonicity exactly as they are with no target. Its **amplitude** path is `baseAmplitude_[i] = hasTarget_ ? targetAmp_[i] * tiltGain(i) : exp2(−p·log2N[i]) * tiltGain(i)` `:1491-1493`, so an amplitude of `exp2(−p(r)·log2N[i])` reproduces the no-target value **bit-for-bit** when it is computed off the same table. That table is namespace-scope and reachable: `inline const std::array<float, 64> detail::kHarmonicCloudLog2N` `:57-62`. |
| `NoiseOrganism` | `class` `:136`; `prepare(double, PrepareConfig)` `:226` with `PrepareConfig{maxBlockSamples=2048, maxCombDelayMs=50, numSources=2}` `:190-194`; `reset()` `:310`; `setSeed` `:388`; `processBlock(float*, std::size_t)` `:414` — **mono out**; `setNumSources` `:451`; `setSourceModel` `:463`; `setSourceNoiseType` `:478`; `setSourceLevel(slot, dB)` `:488`, clamped `[−96, +12]`; `setWanderRate` `:781`; `setSourceDormant` `:833`; `setSourceWake(slot, amount)` `:844`; `getSourceWakeAmount` `:880`; `getAllocatedBytes()` `:999`; `kMaxSources = 4` `:142`; `kControlChunkSamples = 64` `:150` with the `static_assert` that it is the shared grid `:158-161`. `enum class NoiseOrganismModel { Direct, FilteredWind, GranularDust, MetallicHiss }` `:126-131`. |
| `ResonanceDriftNetwork` | `class` `:121`; `prepare(double, PrepareConfig)` `:322` with `PrepareConfig{maxBlockSamples, numPeaks}` `:299-306`; `reset()` `:415`; `setSeed` `:467`; `processBlock(const float*, const float*, float*, float*, std::size_t)` `:515` — **inputs may alias the outputs, in either pairing** `:506`, and the grid is an **absolute residue carried across calls** `:529-531`; `enum class AnchorMode { Free, Keyed, Hybrid }` `:293`; `setAnchorMode` `:587`; `setNoteFrequency` `:624`; `setGravity` `:633`; `setPeakLevel` (clamped `[−60, +12]` dB) `:646`; `setPeakQ` (`[0.1, 100]`) `:656`; `setFreqWander` `:666`; `setQWander` `:674`; `setGainWander` `:682`; `setWanderRate` `:715`; `setPeakWake` `:771`; `setPeakDormant` `:784`; `setMix` `:796`; `setWetGain` `:804`; `getPeakWakeAmount` `:855`; `kMaxPeaks = 12` `:128`; `kDefaultWetGainDb = 34.5f` `:232`. |
| `FeedbackEcology` | `class` `:185`; `prepare(double, PrepareConfig)` `:648` with `PrepareConfig{maxBlockSamples, numLoops}` `:604-611`; `reset()` `:854`; `processBlock(const float*, const float*, float*, float*, std::size_t)` `:914` forwarding to `processBlockTapped(…)` `:944` — **inputs may alias the outputs**, and the grid is an absolute residue `:936-939`; `setNumLoops` `:979`; `setLoopGain` `:1079`; `setCoupling(from, to, amount)` `:1107`; `setLoopWake` `:1261`; `setLoopDormant` `:1271`; `setMix` `:1282`; `kMaxLoops = 6` `:193`; `kDefaultMix = 0.15f` `:582`; `kMaxLoopGain = 0.90f` `:266`; `kDefaultLoopGain = 0.72f` `:267`; `kMaxCouplingPerPair = 0.5f` `:269`; `kDefaultCoupling = 0.04f` `:270`. |
| `BloomEngine` | `class` `:193`; `PrepareConfig{capacity, numChildSlots}` `:338-359` — and its `capacity` doxygen is normative for this plan: *"THE CALLER'S PROMISE ABOUT HOW MANY SLOTS THE CLOUD WILL ACTUALLY SOUND, i.e. `HarmonicCloud::getActivePartialCount()` — NOT `kMaxPartials`… a child written past it is silently inaudible"* `:339-347`; `prepare` `:383`; `reset()` `:413`; `setSeed` `:458`; `processChunk(float*, float*, std::size_t, std::size_t)` `:494` with the **≥ `kMaxSlots` (64) writable floats** precondition `:466-481`; `setDepth` `:532`; `setSpawnRateHz` `:543`; `setConsumerTiltDb` `:618`; `setCapacity` `:628`; `setDormant` `:637`; `setWake` `:648`; `triggerBloom()` `:663`; `capacity()` `:701`; `numChildSlots()` `:705`; `reserveBase()` `:710`; `getLiveChildCount()` `:712`; `isEngaged()` `:717`; `kMaxSlots = 64` `:204`, `kMaxChildren = 16` `:209`, `kDefaultChildSlots = 8` `:215`, `kDefaultSpawnRateHz = 1/240` `:281`, `kDefaultFadeInSeconds/HoldSeconds/FadeOutSeconds = 45/120/180` `:270-272`. |
| **`BloomEngine::applyOutput` return** | Not engaged → returns the caller's `parentCount` verbatim; engaged → pads the gap `[pc, reserveBase())` and the whole owned region `[reserveBase(), capacity_)` with `ratio = i + 1, amplitude = 0`, writes live children into their slots, and returns **`capacity_`** `:1534-1580`. An engaged call with `pc > reserveBase()` increments the saturating `overlapEngagements_` counter `:1541-1549`. |
| `EcosystemEngine` | `class` `:143`, **no base class**; `enum class Kind { Partial = 0, Resonator, Noise, Feedback, Ghost }` `:282-288` (append-only, normative); `PrepareConfig{agentCount = 32, resourceCells = 64, energyBudget = 1.0, initialPoolFraction = 0.5, stepIntervalChunks = 8}` `:303-309`; `prepare` `:336`; `reset()` `:388`; `setSeed` `:404`; `processChunk(std::size_t)` `:431` — an **absolute** `samplePhase_`/`chunkPhase_` grid `:437-450`; `setAgentWake` `:741`; `setAgentDormant` `:776`; `getAgentOutput(i)` `:886` (held between steps, `[0, 1]`); `getAgentEnergy(i)` `:891`; `getAgentKind(i)` `:894`; `getAgentCount()` `:911`; `getAgentCountOfKind(Kind)` `:912`; `getControlStepCount()` `:1001`; `kMaxAgents = 48` `:152`; `kNumKinds = 5` `:168`; `kDefaultStepIntervalChunks = 8` `:196`; `kMinUsableSampleRate = 8000.0` `:201`. **Kinds are assigned by a stratified deal then a Fisher–Yates shuffle over the seeded stream** `:2168-2216` — so the kind of agent *i* is a function of (seed, agentCount) fixed at `prepare()`, and is **not** `i % 5`. |
| `SlowEventScheduler` | `class … final : public ModulationSource` `processors/slow_event_scheduler.h:143`; `kNoTarget = 0xFF` `:150`, `kMaxTargets = 16` `:152`, `kMinIntervalSeconds = 1.0f`, `kMaxIntervalSeconds = 600.0f` `:154-155`; `prepare(double)` `:207`; `reset()` `:213`; `setSeed` `:229`; `setIntervalRange` `:238`; `setEnvelopeTimes` `:250`; `setDepthRange` `:258`; `setBipolarProbability` `:266`; `setTargetCount(std::uint8_t)` `:271`; `processBlock(std::size_t)` `:304`; `getCurrentValue()` `:331`; `getActiveTarget()` `:356`; `isEventActive()` `:361`; `getEventPhase()` `:365`; `getPeriodSeconds()` `:376`. |
| `ContinuousBody` | `class` `:71`; `enum class BodyMaterial : std::uint8_t { Glass = 0, Strings, MetalPlate, Chamber, Ice }` `:81`; `enum class Engine { Modal, Waveguide, Comb }` `:82`; `kNumMaterials = 5` `:84`; `kNumSlots = 2` `:90`; `kControlChunkSamples = 64` `:97`; `kModeCountCeiling = 32` `:98`; `kNumCombs = 6` `:99`; `kMinNoteHz/kMaxNoteHz = 20/8000` `:118-119`; `kDefaultResonance = 0.7f` `:124`; `kDefaultDamping = 0.0f` `:128`; `kWgStiffness = 0.15f` `:192`; `kWgPickPosition = 0.22f` `:193`; `kCombSpread = 0.45f` `:197`; `struct MaterialProfile {engine; ratios; defaultModeCount; amplitudeExponent; damping{b1,b3}; stretch; scatter; referenceHz; t60AtMaxResonanceSec; hfDampingParam;}` `:653-673`; `kGlassRatios` `:679`, `kPlateRatios` `:705`, both with their sourcing notes `:673-704`; `kMaterialProfiles` `:729`; `prepare(double)` `:867`; `reset()` `:973`; `setMaterial` `:1122`; `setResonance` `:1161`; `setDamping` `:1170`; `setKeyTracking` `:1180`; `setNoteFrequencyHz` `:1190`; `setMix` `:1209`; `setCloudMix` `:1219`; `setWidth` `:1264`; `setSeed` `:1344`; `processStereoBlock(const float*, const float*, float*, float*, std::size_t)` `:1369` — **not in place**, absolute grid `:1390-1394`; `getMaterial` `:1452`; `getResonance` `:1559`; `getDamping` `:1562`; `getMix` `:1571`; `getWidth` `:1586`. |
| **What a profile actually reaches** | `configureModalSlot` `:2151` passes `ratios / defaultModeCount / amplitudeExponent / damping{b1,b3} / stretch / scatter` into `ModalResonatorBank::setModes`; `configureNonModalSlot` `:2226` uses only `referenceHz`-derived pitch, `t60AtMaxResonanceSec` and `hfDampingParam` — the waveguide's stiffness/pick-position (`kWgStiffness`, `kWgPickPosition`) and the comb bank's spread/count (`kCombSpread`, `kNumCombs`) are **class constants, not profile fields**. Two Comb materials therefore differ only in pitch, T60 and damping, and only one `TimeVaryingCombBank` and one `WaveguideString` instance exist per object (`engineInstanceFreeFor` `:2229`). **This is what drives S4.1's "all six new materials are Modal" ruling.** |
| `AtmosphereEngine` | `class` `:179`; `PrepareConfig{captureSeconds = 8.0f, blurEnabled = true, freezeEnabled = true, blurFftSize = 1024, freezeFftSize = 2048, maxBlockSamples = 2048}` `:369-376`; copy **deleted**, move defaulted `:388-391`; `prepare(double, PrepareConfig)` `:407`; `reset()` `:525`; `processStereoBlock(const float*, const float*, float*, float*, std::size_t)` `:674` — wet texture only, absolute grid `:699-707`; `setGrainSeconds` `:815` / `getGrainSeconds` `:819`; `setDensity` `:828` / `getDensity` `:832` (doc: grains/s, `[0.1, 20]`, default 4.0, `:821-828`); `setPositionSpread` `:851` / `getPositionSpread` `:854`; `setPitchSemitones` `:858` / `getPitchSemitones` `:862`; `setDecorrelation` `:902` / `getDecorrelation` `:905`; `setBlur` `:910` / `getBlur` `:914`; `setLevel` `:982` / `getLevel` `:986`; `setSeed` `:1013`. **No reverse control and no event-trigger entry point anywhere in the header.** |
| `SubharmonicEngine` | `class` `:155`; `enum class Tone { Div2, Div4, FifthBelow }` `:320`; `kNumTones = 3` `:162`; `kDefaultToneLevelDb{−18, −24, −30}` `:218`; `PrepareConfig{maxBlockSamples = 2048}` `:327-332`; `prepare` `:380`; `reset()` `:487`; `processBlock(const float*, const float*, float*, float*, std::size_t)` `:545` → `processBlockTapped` `:554` (absolute `controlPhase_` residue `:600-607`); `setFundamentalHz` `:616`; `setToneLevelDb(std::size_t, float)` `:638`; `setTrackingAmount` `:674`. |
| `SpectralSmear` | `class` `processors/spectral_smear.h:76`; `PrepareConfig{fftSize = kDefaultFftSize, enabled = false}` `:138-150` — **`enabled` defaults to false, deliberately, so a Phase-10 owner who omits it buys no latency** `:141-149`; `prepare` `:174`; `reset()` `:301`; `processBlock(float*, float*, std::size_t)` `:341` — in place, and a disabled instance is a **bit-identical bypass** `:15-16`, `:342-347`; `setSmearAmount` `:376`; `setDecoherence` `:381`; `setSmearTilt` `:386`; `getSmearAmount/getDecoherence/getSmearTilt` `:422-424`; `isEnabled()` `:461`; `getLatencySamples()` `:472` — `fftSize_` when prepared **and** enabled, else `0`; `kDefaultFftSize = 2048` `:86`. |
| `CavernVerb` (Layer 4 — named only by the one composed test TU) | `class` `effects/cavern_verb.h:193`; `processStereoBlock` `:568`; `setSize` `:613`; `setDarkness` `:619`; `setDecaySeconds` `:626`; `setFog` `:652`; `setDamperDepth` `:717`; `setFreeze` `:735`; `setWidth` `:741`; `setMix` `:748`; `getLatencySamples()` `:786`. Defaults: `kDefaultSize = 0.50f` `:253`, `kDefaultDarkness = 0.80f` `:254`, `kDefaultDecaySeconds = 20.0f` `:255`, `kDefaultFog = 0.30f` `:259`, `kDefaultDamperDepth = 0.35f` `:247`, `kDefaultWidth = 1.00f` `:263`, `kDefaultMix = 1.00f` `:264`. |
| `VoiceAllocator` | `enum class VoiceState { Idle, Active, Releasing }` `systems/voice_allocator.h:43`; `enum class AllocationMode { RoundRobin, Oldest, LowestVelocity, HighestNote }` `:55-60` — **no `Quietest` mode**; `struct VoiceEvent {Type type; uint8_t voiceIndex, note, velocity; float frequency;}` `:102-116`; `noteOn` `:228`; `noteOff` `:258`; `voiceFinished` `:288`; `setAllocationMode` `:317`; `setStealMode` `:322`; `setVoiceCount` `:331`; `getVoiceState` `:424`; `getVoiceNote` `:406`. |
| `MultiStageEnvelope` | `kMinStages = 4`, `kMaxStages = 8` `processors/multi_stage_envelope.h:63-64`; `prepare(float)` `:73`; `reset()` `:79`; `gate(bool)` `:99`; `setNumStages` `:135`; `setStage(int, float, float, EnvCurve)` `:166`; `setSustainPoint(int)` `:178`; `setReleaseTime(float)` `:206`; `setRetriggerMode` `:215`; `process()` `:223`; `isActive()` `:251`. |
| `GrowthEnvelope` | `class … : public ModulationSource` `processors/growth_envelope.h:93`; `prepare(double)` `:117`; `reset()` `:129`; `setSeed` is a documented **no-op** `:140`; `setDuration(float)` `:144`; `getDuration()` `:149`; `trigger()` `:161`; `processBlock(size_t)` `:185`; `getCurrentValue()` `:197`. |
| `BreathingModulator` | `processors/breathing_modulator.h:105`; `prepare(double)` `:144`; `reset()` `:152`; `setSeed` `:164`; `setRate` `:170`; `setDepth` `:177`; `setIrregularity` `:184`; `processBlock(size_t)` `:209`; `getCurrentValue()` `:222`. |
| `TidalModulator` | `processors/tidal_modulator.h:122`; `prepare(double)` `:169`; `reset()` `:178`; `setSeed` `:193`; `setRate` `:202`; `setDepth` `:209`; `getBasePeriodSeconds()` `:217`; `processBlock` `:250`; `getCurrentValue()` `:263`. |
| `TapeSaturator` | `prepare(double, [[maybe_unused]] size_t)` `processors/tape_saturator.h:141`; `reset()` `:180`; `setDrive(float dB)` `:239`; `setSaturation(float)` `:248` (clamped `[0,1]`); `getDrive` `:278`; `getSaturation` `:283`; `process(float*, size_t)` `:335` — mono, in place. |
| `TruePeakLimiter` | `class` `processors/true_peak_limiter.h:44`; `prepare(double, std::size_t)` `:59`; `reset()` `:78`; `setCeilingDb(float)` `:85`; `processBlock(float*, float*, int)` `:104`. |
| `Biquad` | `struct BiquadCoefficients {b0, b1, b2, a1, a2;}` `primitives/biquad.h:242-248`; `class Biquad` `:308`; `setCoefficients(const BiquadCoefficients&)` `:325`; `process(float)` `:352`; `reset()` `:385` — TDF2. |
| `deriveStreamSeed(std::uint32_t base, std::size_t salt)` — lowbias32 finaliser with the non-zero substitution `0x2545F491u` | `core/random.h:102-113` |
| `detail::isFinite(float)` — the fast-math-immune bit-pattern check (`isNaN` `:99`, `isInf` `:260`) | `core/db_utils.h:118` |
| `enum class ModCurve { Linear, Exponential, SCurve, Stepped }` | `core/modulation_types.h:79-85` |
| `applyModCurve(ModCurve, float)` | `core/modulation_curves.h:38` |
| `RenderFingerprint{rms, peak, meanAbs, totalVariation, checkpoints[32]}` `:63`; `fingerprintRender(std::span<const float>)` `:73`; `compareFingerprints(actual, reference, metricTolerance = 2.5e-4, sampleTolerance = 5.0e-4)` `:122`; `kSampleTolerance = 5.0e-4f` `:58`; `kMetricTolerance = 2.5e-4` `:61` | `tests/test_helpers/render_fingerprint.h` |
| `AllocationScope` / `AllocationDetector` — and the global `operator new/delete` replacement lives in `<allocation_operator_overrides.h>`, whose **single owner in `dsp_systems_tests` is `unit/systems/selectable_oscillator_test.cpp:388`**; a second include is a duplicate-symbol link error | `tests/test_helpers/allocation_detector.h`, the ownership note quoted at `dsp/tests/unit/systems/noise_organism_perf_test.cpp:150-153` |
| `dsp_systems_tests` is an **enumerated** source list opening at `add_executable(dsp_systems_tests` | `dsp/tests/CMakeLists.txt:324`, list ends `:493` |
| `dsp_effects_tests` list + the target-wide `KRATE_DSP_AETHER_TEST_HOOKS` define and its stated ODR reason | `dsp/tests/CMakeLists.txt:504-535`, `:541-549` |
| Layer-3 block of the strict-lint TU (Vorago headers at `:179`, `:182`, `:185`, `:188`, `:191`, `:194`) | `dsp/lint_all_headers.cpp` |
| Perf measurement basis: `kBlockBudgetNs = (512/48000)×1e9`, `kRegressionFactor = 1.5`, `kReferenceNs = kBlockBudgetNs × ceiling`, `kMaxAdmissibleNs = kReferenceNs / kRegressionFactor`, best-of-25 × 500 blocks, paired `static_assert` per baseline | `dsp/tests/unit/effects/cavern_verb_perf_test.cpp:105-125` |
| The seven `kNumMaterials`-sized sites | `continuous_body_perf_test.cpp:283` (`static_assert(== 5)`), `:352` (`kMaterials`), `:360` (`kMaterialNames`), `:373-379` (`crossfadePartner` = `(idx + 1) % kNumMaterials`), `:881-897` (the four budget loops); `continuous_body_test.cpp:1082` (`REQUIRE(CB::kNumMaterials == 5u)`); `seraphis_perf_test.cpp:419` (`static_assert(== 5)`), `:571`/`:579` (`kMaterials`/`kMaterialNames`), `:654-662` (`MaterialSurvey::nsPerBlock` + `surveyMaterials()`), `:1190-1205` (the report loop and `kMaterials[survey.worstIndex]`) |
| The two capped `continuous_body_perf_test` baselines: `kSteadyBaselineNsPerBlock = 35500.0` (worst measured 35 669) and `kOperatingBaselineNsPerBlock = 35500.0` (worst measured 41 141, *"exceeds `kMaxAdmissibleHalfPctNs` by 15.7 %"*) | `continuous_body_perf_test.cpp:160-180`, `:220-229` |

### S0.3 ODR sweep — re-run this session from the repo root

```
grep -rn "\b<name>\b" dsp/ plugins/ tools/ tests/
```

| Name | Hits |
|---|---|
| `VoragoVoice`, `VoragoVoiceConfig`, `VoragoVoiceParams` | 0 / 0 / 0 |
| `VoragoEngine`, `VoragoEngineConfig` | 0 / 0 |
| `VoragoMacroMatrix`, `VoragoMacro`, `VoragoMacroTarget`, `VoragoMacroRow`, `VoragoMacroValues`, `VoragoCavernTargets` | 0 each |
| `StoneChamber`, `SteelTank`, `WoodenHull`, `CathedralColumn`, `CavernWall`, `GlassSphere` | 0 each |
| `kNumSeraphisMaterials`, `KRATE_DSP_VORAGO_TEST_HOOKS` | 0 / 0 |

`VoragoMacroTargetOwner`, `detail::VoragoVoiceSilenceRampProbe`, `detail::VoragoEngineNonFiniteProbe`,
`kRoomModeRatios`, `kClampedPlateRatios`, `kBarRatios`, `kFastAttackEnvelopeConfig` — all 0 as well
(same command, run for each). Every new name is `Vorago`-prefixed or class-scoped, which is what makes
the sweep a clean zero against roadmap line 128's near-name hazard list (`ResonatorBank`,
`FeedbackNetwork`, `NoiseGenerator`, `GranularEngine`, `PatternScheduler` all exist and are not
reused).

---

## S0.4 SEVEN THINGS THE IMPLEMENTER MUST NOT DISCOVER AT BUILD TIME

These are the places where a literal reading of the spec meets the shipped code and one of them has
to give. Each states the fact, the consequence, and the ruling this plan takes. **B-1 and B-2 change
observable behaviour and must be read before any code is written; B-7 changes the *shape of the
voice's clearing paths* and adds the phase's second shared-component change, so it must be read
before `VoragoVoice::silence()` or `VoragoEngine::noteOn` is written.**

### B-1 (BLOCKING). `BloomEngine::setCapacity` must track the cloud's **active** partial count, and the voice must publish `reserveBase()` as the parent count — not `getActivePartialCount()`

**The facts.** (a) `HarmonicCloud` renders `kernelCount_ = max(activeCount_, tailHighWater_)` partials
and `activeCount_ = clamp(round(64^r), 1, 64)` is a function of **richness alone**
(`harmonic_cloud.h:1462-1463`, `:1500-1501`) — supplying a longer `setSpectralTarget` count does **not**
raise it. (b) `BloomEngine::PrepareConfig::capacity`'s own doxygen says so outright: *"THE CALLER'S
PROMISE ABOUT HOW MANY SLOTS THE CLOUD WILL ACTUALLY SOUND, i.e.
`HarmonicCloud::getActivePartialCount()` — NOT `kMaxPartials` … a child written past it is silently
inaudible"* (`bloom_engine.h:339-347`). (c) An engaged `processChunk` returns `capacity_`, pads
`[pc, reserveBase())` and `[reserveBase(), capacity_)`, and writes children only into
`[reserveBase(), capacity_)` (`:1534-1580`). (d) An engaged call with `pc > reserveBase()` increments
the saturating overlap counter every chunk (`:1541-1549`).

**The consequence.** Spec FR-010 step 2 (`setSpectralTarget(…, returnedCount)`) and FR-011
(`count = getActivePartialCount()`) are the **same number** only when `capacity_ == activeCount_`.
And if the voice hands `parentCount = getActivePartialCount()` to `processChunk`, then `pc == capacity_
> reserveBase()` on every engaged chunk, so the overlap counter — the component's own "the caller
overran my reserved region" signal — saturates immediately and means nothing.

**The ruling.**

```
capacity = clamp(cloud_.getActivePartialCount(),
                 kBloomChildSlots + kMinParentSlots,     // 6 + 8 = 14
                 HarmonicCloud::kMaxPartials);           // 64
bloom_.setCapacity(capacity);                            // FR-012
bloom_.setConsumerTiltDb(cloudTiltDb_);                  // FR-012
parentCount_ = bloom_.reserveBase();                     // = capacity - numChildSlots()
// fill ratios_[0..parentCount_), amplitudes_[0..parentCount_) with the FR-011 law
const std::size_t count = bloom_.processChunk(ratios_.data(), amplitudes_.data(),
                                              parentCount_, kControlChunkSamples);
```

`count` is then `capacity` while engaged and `parentCount_` while not. FR-011's
`count = getActivePartialCount()` is satisfied whenever richness keeps `activeCount_ >= 14`;
below that the **floor** wins and the top `numChildSlots()` slots sit above `activeCount_`, i.e.
**the bloom becomes inaudible at very low richness and the parents are never displaced.** That is the
deliberate degradation direction: the alternative (`capacity = activeCount_` with no floor) drives
`reserveBase()` to 0 at `activeCount_ <= 6` and deletes the parent spectrum entirely. Recorded in the
header; SC-018's accounting arm asserts `count <= HarmonicCloud::kMaxPartials` and
`reserveBase() >= kMinParentSlots` at every control step.

### B-2 (BLOCKING). FR-011's "level- and timbre-neutral" holds for the **parent region only**, and only if the amplitude law is evaluated off the cloud's own log2 table

The cloud computes, with a target active,
`baseAmplitude_[i] = targetAmp_[i] * tiltGain(i)`, and without one,
`exp2(−p(r) * detail::kHarmonicCloudLog2N[i]) * tiltGain(i)` (`harmonic_cloud.h:1491-1493`).
Writing `amplitudes_[i] = std::pow(float(i + 1), −p)` is **not** the same number: `std::pow` and
`exp2(−p·log2N)` differ in the last bits, and under `-ffast-math` `pow(n, 1.0f)` is documented in this
very file to hand back `31.999998` for `n = 32` (`:1289-1293`). The voice therefore evaluates

```cpp
const float p = HarmonicCloud::kRichnessMinExponent
              + (HarmonicCloud::kRichnessMaxExponent - HarmonicCloud::kRichnessMinExponent) * richness_;
for (std::size_t i = 0; i < parentCount_; ++i) {
    ratios_[i]     = static_cast<float>(i + 1);                                   // EXACT: takes the
                                                                                  // FR-082 identity branch
    amplitudes_[i] = std::exp2(-p * detail::kHarmonicCloudLog2N[i]);              // no tilt: the cloud
                                                                                  // still multiplies tiltGain(i)
}
```

using the same namespace-scope table (`harmonic_cloud.h:57-62`). Two further consequences are
normative and belong in the header:

1. **The voice must mirror the cloud's richness.** `p` is derived from the value the voice last wrote
   with `setRichness`, which the voice therefore shadows in `cloudRichness_`. **The shadow is a
   single-write-path device, not a missing getter** — the earlier justification here was wrong and is
   corrected: `HarmonicCloud::getRichness()` does exist (`harmonic_cloud.h:490`,
   `[[nodiscard]] float getRichness() const noexcept { return richness_; }`) and returns exactly the
   number `p` is derived from, so `updateSpectrumTarget()` could read the cloud directly and get the
   same `p`. The shadow earns its place only on the one-write-path argument below; an implementer who
   prefers one fewer field may instead read `cloud_.getRichness()` in `updateSpectrumTarget()`, which
   is the equivalent implementation and removes the drift hazard outright. Either way the rule stands:
   any path that writes richness — `prepare()`, the
   macro matrix's `CloudRichness` row, a direct forwarder — goes through `VoragoVoice::setRichness`,
   which updates the shadow **and** the cloud. One write path, the `seraphis_voice.h:589-596`
   `applyStage` discipline.
2. **Neutrality is a parent-region claim, not a whole-spectrum claim.** Slots
   `[reserveBase(), capacity)` carry amplitude 0 while no child is live, where the untargeted cloud
   would carry `n^-p`. That is exactly why FR-011 requires `clearSpectralTarget()` whenever
   `getLiveChildCount() == 0`, and why SC-018a measures both edges. The header says this in one
   sentence rather than claiming a neutrality the arithmetic does not have.

### B-3 (BLOCKING). The FR-015 all-pass pair is flat **per channel**; the mono sum is bounded, not flat

Two different all-pass filters `A_L`, `A_R` each have exactly unit magnitude, so **each channel** is
magnitude-flat by construction. Their mono sum is `|A_L + A_R| = 2|cos(Δφ/2)|`, which is flat only if
the phase difference `Δφ` is constant — the property of a 90° phase-difference network, which a single
second-order section per channel can only approximate. Clarification Q8 asks for "a fixed 2nd-order
all-pass pair … for a flat mono magnitude response"; taken literally that is unachievable with two
sections, and the plan will not pretend otherwise.

**The ruling.** Ship the two sections, chosen as the two branches of the classic polyphase-IIR
90°-difference network, whose transfer functions are second-order all-passes with `a1 == 0`:

```
H_k(z) = (a_k + z^-2) / (1 + a_k z^-2)   ->   BiquadCoefficients{.b0 = a_k, .b1 = 0, .b2 = 1,
                                                                 .a1 = 0,   .a2 = a_k}
kNoiseApCoeffL = 0.6923878f     // branch A, first section
kNoiseApCoeffR = 0.4021921f     // branch B, first section
```

with the right channel additionally delayed by **one sample** (one float of state, named
`noiseApDelayR_`) — that unit delay is what makes the pair a phase-*difference* network rather than two
unrelated all-passes. The header states: *per-channel magnitude is exactly flat; the mono-sum magnitude
deviates by at most the measured figure recorded here over [20 Hz, 8 kHz], which is the property a
fractional-delay decorrelator cannot offer at all.* A dedicated case
(`VoragoVoice_NoiseDecorrelationMonoSum`, S10.4) measures the worst mono-sum deviation in dB across
that band and asserts (a) `<= 3.0 dB` and (b) at least **6 dB better** than a one-sample-delay pair
measured in the same case. Allocation-free, 2 biquads + 1 float per voice, sized at `prepare()`.

### B-4 (deviation from FR-087, recommended). The non-finite probe is a friend struct **defined in the test TU** — no `KRATE_DSP_VORAGO_TEST_HOOKS`, no target-wide define

FR-087 asks for a hook compiled target-wide under `KRATE_DSP_VORAGO_TEST_HOOKS`, in the shape
`dsp/tests/CMakeLists.txt:547-555` uses for `KRATE_DSP_AETHER_TEST_HOOKS`. That define exists because
`AetherReverb::injectNonFiniteStateForTest` is a **member function**: the macro changes the class
definition, so every TU in the image must see the same one. Seraphis's engine probe has the identical
job and needs **no macro at all**, because it is a `detail` struct *forward-declared* in the header
(`seraphis_engine.h:193-195`), *befriended* (`:1074`), and *defined in the test TU*
(`seraphis_nonfinite_test.cpp:107-111`). The class definition is byte-identical in every build.

**The ruling.** Take the Seraphis shape. `detail::VoragoEngineNonFiniteProbe` and
`detail::VoragoVoiceSilenceRampProbe` are forward-declared in their headers, befriended, and defined in
`vorago_nonfinite_test.cpp` / `vorago_voice_test.cpp`. No `target_compile_definitions` line is added,
`dsp_lint_stub` is untouched, and the ODR obligation FR-087 spends a paragraph managing simply does not
arise. **This is a deviation from a normative FR** and is carried to the user in S14 as a one-line spec
amendment; if the user declines it, add the define and make the probe a macro-gated member — the tests
below are written against the probe's *operations*, not its spelling, so nothing else moves.

The probe must do more than Seraphis's (which only sets `nonFinitePending_`): SC-029 requires the
**detection** path to run, not just the recovery. It therefore pokes the victim voice's served audio:

```cpp
struct VoragoEngineNonFiniteProbe {
    // Writes `bits` into voice v's carry FIFO and marks it servable, so the ENGINE's
    // accumulation-point scan (S6.5 step 3) is what discovers it - exactly the path
    // FR-072 specifies. `bits` is a bit pattern (0x7FC00000 qNaN / 0x7F800000 +Inf),
    // never a std::numeric_limits value, which -ffast-math folds.
    static void poisonVoiceCarry(VoragoEngine& e, std::size_t v, std::uint32_t bits) noexcept;
    static std::uint32_t pendingMask(const VoragoEngine& e) noexcept;
};
```

### B-5 (ambiguity, ruled). The ghost return is summed into the bus **before** the subharmonic stage

FR-056 says the atmosphere is "fed from the voice sum … tapped **before** [subharmonic and smear] so
the ghost hears the raw ensemble … and summed back as wet texture only … into **the same bus**".
"The same bus" is the voice-sum bus, i.e. pre-subharmonic. The plan takes that reading and states the
musical reason in the header: the ghost is a memory of the instrument and must share the instrument's
sub-weight and fog, not sit outside them. The chunk order is fixed in S6.5 and is
`sum → atmosphere(tap = sum) → bus += wet → subharmonic → smear`.

### B-6 (arithmetic, ruled at plan stage). Roadmap line 470 is not reachable; the recommended polyphony is 4, and `kMaxVoices` is **not free**

Worked in S12. The short form: at the OQ-1(b)/(c) architecture the spec fixes (`V = 474 395`,
`G = 411 330`), **and with the per-slot life-only cost `L` of the unconditional render loop counted**
(S6.5 gives every non-rendering slot an `advanceLifeOnly`, whose S3.4 step-1 body is
`EcosystemEngine::processChunk` — **29 074.0 ns/block**, spec FR-081's table, sourced to
`specs/vorago-phase8-ecosystem/compliance.md:62` — plus the two `SlowEventScheduler`s, the two life
modulators and `publishIdentity()`'s reduction scan, so **`L ≈ 31 000`**), `kReferenceNs` admits
**5 voices bare / 4 with the SC-003 overhead** and the regression-gated line admits **3 bare / 2** —
below the roadmap's floor of 4. The plan's recommendation to the user is **shipped polyphony 4,
`kMaxVoices` 6, no L-3** (it would delete roadmap line 122's two-body blend), **L-4 applied only if
FR-082's measured probe says the gated line needs it**.

**The claim this ruling previously made — "`kMaxVoices` is a ceiling that SC-001a measures and
nothing gates" — was wrong, and is withdrawn.** Every slot between the shipped polyphony and
`kMaxVoices` pays `L` on **every block**, because the render-loop bound is `v < kMaxVoices`
unconditionally (S6.5, following `seraphis_engine.h:544-547`). At `kMaxVoices = 8` and polyphony 4
that is four never-allocated slots × `L` ≈ **124 000 ns/block**, ~5.8 % of `kMaxAdmissibleNs`, and it
is precisely what moves polyphony 4 + L-4 from 4.30 to **3.99** in the gated ×1.15 column (S12.2) —
i.e. from passing to failing, on the one number Q-A asks the user to rule. `kMaxVoices` is therefore
a **budget decision**, carried to the user as part of Q-A, and SC-001a **measures `L` directly** — a standalone
`advanceLifeOnly` arm beside SC-002's stage probe, because `kMaxVoices` is a compile-time constant and
therefore cannot be swept at run time — and recomputes the ladder with the `(kMaxVoices − N)·L` term,
so the ceiling's cost is measured rather than projected. `kMaxVoices` stays at **8 while the survey
runs**, so SC-001a can measure the whole {1, 2, 4, 6, 8} polyphony curve; it is lowered, if Q-A rules
that way, in the same commit that checks in the SC-001b baseline. Nothing is checked in as a baseline
until the user's ruling is written back into the spec as SC-001b (FR-083's rule).

### B-7 (BLOCKING). `FeedbackEcology::reset()` is a control-thread call, so the steal path and the deferred non-finite recovery may not reach it — the voice needs two clearing paths and the component needs one appended O(1) entry point

**The facts, each read this session.**

(a) `FeedbackEcology::reset()` (`feedback_ecology.h:854`) runs `clearLoopAudio(i)` **and**
`L.delay.reset()` for every loop, and its own comment states the thread contract:
*"The extra delay.reset() is the O(buffer) wipe S3.1 keeps OFF the audio thread; **reset() is a
control-thread call**"* (`:861-862`).

(b) The component quantifies it: `delay.reset()` is *"a std::fill over the whole power-of-two buffer
— **131 072 B per loop at 44.1/48 kHz and 524 288 B at 192 kHz**"* and *"a single 131 KB fill already
exceeds"* the **8 889 ns** control-chunk budget (`:2401-2412`, the same figures repeated at `:1610`).

(c) Every loop is prepared at `kMaxDelaySeconds = 0.52` (`:683`, `:243`) and `kMaxLoops = 6`
(`:193`), so **one** `VoragoVoice` ecology clear is ~**786 KB** of `std::fill` at 48 kHz and ~**3.1 MB**
at 192 kHz.

(d) The O(1) route exists and is documented: `clearLoopAudio(i)` (`:2426`) clears the SVF, the
resonator, the DC blocker and both previous-sample vectors, snaps the delay taps, and installs a
read-mute window of exactly one delay length — *"everything CrossfadingDelayLine::reset() does EXCEPT
the O(buffer) wipe"* (`:2432-2437`). It is **private** (`private:` at `:1523`) and is reachable from
outside today only indirectly, through the FR-063 sleep edge at `:1899`, which needs a gate ramp and
a control step and therefore cannot serve a steal that must complete inside one chunk.

(e) No other sub-component forbids the audio thread on `reset()`. `ContinuousBody::reset()` says the
opposite outright — *"Real-time safe"* (`continuous_body.h:965-973`). `FeedbackEcology` is the only
offender, so the fix is one call wide, not a redesign.

**The consequence on the paths this plan makes mandatory.** S6.7 makes a steal
`silence()` -> `resetForSteal()` -> `noteOn()` and states that *"with a 4-voice pool and minutes-long
tails stealing is the normal allocation path"*; S3.2 defined **both** of those as "every
sub-component's `reset()`", so a steal ran (b)'s wipe **twice per voice**, inside
`VoragoEngine::noteOn`, on the audio thread. The same defect sat under S6.6 step 2:
`kResetsPerControlChunk = 1` was justified by "one whole-voice reset fits inside one control chunk"
— it does not; by (b) a **single loop's** fill already exceeds the chunk budget, so the throttle was
sized against a cost model the component contradicts, on the path FR-072 / SC-029 make mandatory.

**The ruling, in four parts.**

1. **One append-only addition to `feedback_ecology.h`** — a public, O(1)-per-loop, RT-safe audio
   clear, in exactly the append-only shape AR-4 uses for `ContinuousBody`:
   ```cpp
   /// @brief The RT-SAFE half of reset(): clear every loop's audio with NO
   /// O(buffer) wipe. Calls clearLoopAudio(i) for i < config_.numLoops and
   /// nothing else - no delay.reset(), no counter clear, no configuration
   /// touched, no smoother snapped. Safe on the audio thread, which reset()
   /// (:861-862) deliberately is not. Added for Vorago Phase 10's steal and
   /// non-finite-recovery paths (plan B-7); a fifth caller of the function
   /// whose comment at :95 enumerates four.
   void silenceAudio() noexcept {
       if (!prepared_) { return; }
       for (std::size_t i = 0; i < config_.numLoops; ++i) { clearLoopAudio(i); }
   }
   ```
   It adds no class, no member and no enumerator, so `lint-odr.js` has nothing new to see, and
   `FeedbackEcology` has **no Seraphis consumer** — a repo-wide `grep -rn "feedback_ecology.h" dsp/
   plugins/` run this session names only Vorago Layer 3 peers, `lint_all_headers.cpp` and the
   component's own five test TUs — so SC-016's Seraphis gate does not widen. The green gate is
   `FeedbackEcology`'s own suites, run explicitly in S11.2 step 3a.

2. **Why dropping the wipe is still sound for a *non-finite* recovery**, which is the one place a
   reader will doubt it. `clearLoopAudio` does not zero the ring; it makes the ring **unobservable**
   for exactly one delay length, and the component says why that is equivalent: *"a loop that WRITES
   but does not READ cannot emit anything the buffer already held: the delay half of the clear is a
   READ-MUTE WINDOW of exactly one delay length"* (`:2412-2420`), with the position frozen while the
   window runs so that one delay length is exactly enough (`:2421-2425`). During the window the read
   contributes 0, so the loop writes clean input; after it, every sample the read tap reaches was
   written **after** the clear. A NaN in the ring is therefore never read — which is the entire
   property the O(buffer) wipe was buying.

3. **`VoragoVoice` gets one shared clearing body and four named entry points**, and S9 states the
   thread each belongs to (S3.2 is rewritten to this shape):

   | Entry point | Thread | Body | Fade tail | Sole caller |
   |---|---|---|---|---|
   | `reset()` | **control** | `clearRunState(/*rtSafe=*/false)` — `ecology_.reset()`, the O(buffer) wipe | cleared | `prepare()` step 8; `VoragoEngine::reset()`/`silence()` |
   | `resetForRecovery()` | RT-safe | `clearRunState(true)` — `ecology_.silenceAudio()` | cleared (a poisoned tail must not survive) | S6.6 step 2, FR-072 |
   | `resetForSteal()` | RT-safe | `clearRunState(true)` | **preserved** | the engine's steal teardown |
   | `silence()` | RT-safe | arm the ramp, then `clearRunState(true)` | armed | steal, `VoragoEngine::silence()` |

   The three RT entry points differ from `reset()` by **exactly one call**, stated in one place, so
   they cannot drift; `rtSafe` is an argument at two internal call sites, not a member.

4. **`kResetsPerControlChunk`'s rationale is restated** against the measured cost of the RT-safe
   path, not of `reset()` (S6.2). The throttle survives — it is still one whole-voice clear per chunk
   — but its stated reason is now the true one.

The two cases that keep this honest are in S10.3a: `VoragoVoice_SilenceClearsEcologyAudio` (the
behavioural half — the O(1) clear must still clear) and `VoragoVoice_ClearingPathCost` `[.perf]` (the
cost half — the RT paths must stay an order of magnitude below `reset()`).

---

## S1. Architecture at a glance

```
                          VoragoEngine  (Layer 3, vorago_engine.h)
  noteOn/noteOff ──► VoiceAllocator ──► dispatch ──► voices_[0..kMaxVoices)
                                                         │
   ┌─────────────────────────────────────────────────────┴───────────────────────────┐
   │  VoragoVoice  (Layer 3, vorago_voice.h) — one 64-sample control chunk           │
   │                                                                                  │
   │  identity layer:  EcosystemEngine ─┐   SlowEventScheduler x2 ─┐                  │
   │                   BreathingModulator, TidalModulator, GrowthEnvelope             │
   │                                    └── routed (max-combine) ──┴──► wake/depth    │
   │                                                                   + ghostRequest │
   │                                                                                  │
   │  BloomEngine.processChunk(ratios_, amplitudes_, reserveBase(), 64)               │
   │        └─► HarmonicCloud.setSpectralTarget(...)  |  clearSpectralTarget()        │
   │  HarmonicCloud ─┐                                                                │
   │  NoiseOrganism ─┴─► exc L/R  ─► [voice envelope, IN PLACE — AR-5]                │
   │        └─► ResonanceDriftNetwork (in place) ─► FeedbackEcology (in place)        │
   │                 └─► ContinuousBody A ─┐                                          │
   │                     ContinuousBody B ─┴─► blend b (per-sample) ─► carry FIFO     │
   └──────────────────────────────────────────────────────────────────────────────────┘
                                                         │  voice sum x sumGainHeld_
                                                         ▼
     AtmosphereEngine (GLOBAL, engine-owned; level = max over voices of getGhostRequest())
                                                         │ + wet
                                                         ▼
                        SubharmonicEngine  ─►  SpectralSmear     [engine.processStereoBlock]
                                                         │
                     ══════ Layer 4 SEAM — the CALLER owns CavernVerb (AR-1) ══════
                                                         │
                        TapeSaturator (low drive) ─► TruePeakLimiter
                                                            [engine.processOutputStage]

  VoragoMacroMatrix (Layer 3, vorago_macro_matrix.h)
     kRows (constexpr) ──► apply(VoragoEngine&)              : Voice- and Engine-owned rows
                      └──► computeCavernTargets() -> POD     : Cavern-owned rows, floats only
```

Three invariants hold the whole thing together and every later section is a consequence of them:

* **D1 — the voice never renders a partial chunk.** Whole 64-sample chunks are rendered into a stereo
  carry FIFO and the caller is served out of it (`seraphis_voice.h:440-484`). `HarmonicCloud` restarts
  its control grid on every call (`harmonic_cloud.h:713-716` as quoted in its cadence note `:736-753`),
  so handing it a 36-sample slice would give a `36 + 28` split two control steps where an unsplit 64
  gives one. Every other sub-component carries an **absolute** grid across calls and is therefore
  partition-safe on its own; the carry makes the cloud safe too and makes SC-007 exact.
* **D2 — the engine runs its own absolute grid** (`sampleCounter_ % 64`) and hands slices to the
  voices, which absorb any caller partition through D1. The engine's global stage components
  (`AtmosphereEngine`, `SubharmonicEngine`, `SpectralSmear`) all carry absolute residues or are
  hop-based, so they take the slice directly.
* **D3 — nothing downstream of the voice envelope is gated** (AR-5). `noteOff` releases the envelope;
  the resonance network, the ecology, both bodies and the global chain ring out. The voice retires on a
  level detector, not on the envelope.

---

## S2. `VoragoVoice` — file, constants, configuration, public API

### S2.1 File, banner, includes (FR-001)

`dsp/include/krate/dsp/systems/vorago_voice.h`, `namespace Krate::DSP`, header-only,
`#pragma once`. The banner states the layer, the RT contract, the three invariants above, B-1, B-2 and
B-3 in one paragraph each, and the FR-090 default table in full.

```cpp
#include <krate/dsp/core/db_utils.h>            // detail::isFinite (fast-math-immune)
#include <krate/dsp/core/env_curve.h>           // EnvCurve
#include <krate/dsp/core/random.h>              // deriveStreamSeed, Xorshift32

#include <krate/dsp/primitives/biquad.h>        // FR-015 decorrelation pair (B-3)
#include <krate/dsp/primitives/smoother.h>      // LinearRamp (blend + noise gain)

#include <krate/dsp/processors/breathing_modulator.h>
#include <krate/dsp/processors/growth_envelope.h>
#include <krate/dsp/processors/multi_stage_envelope.h>
#include <krate/dsp/processors/slow_event_scheduler.h>
#include <krate/dsp/processors/tidal_modulator.h>

#include <krate/dsp/systems/bloom_engine.h>
#include <krate/dsp/systems/continuous_body.h>
#include <krate/dsp/systems/ecosystem_engine.h>
#include <krate/dsp/systems/feedback_ecology.h>
#include <krate/dsp/systems/harmonic_cloud.h>
#include <krate/dsp/systems/noise_organism.h>
#include <krate/dsp/systems/resonance_drift_network.h>

#include <algorithm> <array> <cmath> <cstddef> <cstdint>
```

**No `effects/` header, ever** — `lint-layers.js` would flag it, and AR-1 is the reason it must not be
there in the first place. `atmosphere_engine.h` is **not** included: the ghost tap is engine-owned
(OQ-1 (b)).

### S2.2 Public constants (all class-scoped, `kPascalCase`)

```cpp
static constexpr std::size_t kControlChunkSamples = 64;   // FR-007; matches the 8 cited components
static constexpr std::size_t kMaxBlockSamples     = 2048; // FR-004 clamp ceiling
static constexpr std::size_t kNumBodies           = 2;    // FR-036
static constexpr std::size_t kNumEventSchedulers  = 2;    // FR-022 (Q7)

// --- retirement (FR-013, Q6) -------------------------------------------------
/// -90 dBFS. NOT Seraphis's -100 (seraphis_voice.h:148): a Vorago tail is minutes
/// long and the level detector must be able to declare it over.
static constexpr float kTailSilenceThreshold = 3.1623e-5f;
/// Level-detector release TAU (NOT a time-to-99%, so calculateOnePolCoefficient
/// must not be used - seraphis_voice.h:369-373).
static constexpr float kLevelReleaseMs = 100.0f;
/// silence() ramp; shorter than one control chunk at every supported rate, so a
/// steal completes inside one chunk (seraphis_voice.h:155).
static constexpr float kSilenceRampMs = 1.0f;
/// FR-013's TEN SECONDS, expressed as a duration and derived at prepare() into
/// quiescentChunksToRetire_ = lround(kQuiescentSeconds * fs / kControlChunkSamples).
/// A literal chunk count would silently mean 2.5 s at 192 kHz.
static constexpr float kQuiescentSeconds = 10.0f;

// --- envelope (FR-014, Q5) ---------------------------------------------------
static constexpr EnvCurve kStageCurve          = EnvCurve::Exponential;
static constexpr int      kEnvelopeStages      = 6;   // <= MultiStageEnvelope::kMaxStages (8)
static constexpr int      kEnvelopeSustainPoint = 4;  // stages 0..3 are the pre-sustain walk
enum class EnvelopeMode : std::uint8_t { Standard = 0, Growth = 1 };

// --- bloom slot budget (B-1) -------------------------------------------------
static constexpr std::size_t kBloomChildSlots = 6;
static constexpr std::size_t kMinParentSlots  = 8;
static constexpr std::size_t kMinCloudCapacity = kBloomChildSlots + kMinParentSlots;  // 14

// --- noise decorrelation (FR-015, B-3) ---------------------------------------
static constexpr float kNoiseApCoeffL = 0.6923878f;
static constexpr float kNoiseApCoeffR = 0.4021921f;

// --- blend (FR-036, FR-037) --------------------------------------------------
static constexpr float kBlendRampMs = 50.0f;   // the library-wide gain-ramp time
                                               // (noise_organism.h:178)

// --- seed salts (FR-025; pairwise distinct, asserted) ------------------------
static constexpr std::size_t kCloudSalt      = 0x0100;
static constexpr std::size_t kNoiseSalt      = 0x0200;
static constexpr std::size_t kResonanceSalt  = 0x0300;
static constexpr std::size_t kEcologySalt    = 0x0400;
static constexpr std::size_t kBloomSalt      = 0x0500;
static constexpr std::size_t kEcosystemSalt  = 0x0600;
static constexpr std::size_t kBodyASalt      = 0x0700;
static constexpr std::size_t kBodyBSalt      = 0x0800;
static constexpr std::size_t kBreathSalt     = 0x0900;
static constexpr std::size_t kTideSalt       = 0x0A00;
static constexpr std::size_t kSchedSaltBase  = 0x0B00;   // + scheduler index
static constexpr std::size_t kSlotDrawSaltBase = 0x0C00; // + scheduler index (FR-022 slot draw)
static_assert(/* all twelve pairwise distinct, and kSchedSaltBase + kNumEventSchedulers
                 <= kSlotDrawSaltBase, and kSlotDrawSaltBase + kNumEventSchedulers
                 < VoragoEngine's kVoiceSaltBase */, "FR-025 / FR-045");

/// FR-002's ownership guard. MEASURED, not guessed: recorded here by T0xx from
/// VoragoVoice_SizeAndOwnership's printout, as ceil(measured x 1.05) rounded up
/// to the next 64 B - the seraphis_voice.h:186-204 rule.
static constexpr std::size_t kVoiceSizeBound = /* filled from the measurement */;
```

`GrowthEnvelope::setSeed` is a documented no-op (`growth_envelope.h:140`) and is deliberately **not**
called, so it carries no salt — the `seraphis_voice.h:927-930` note, restated.

### S2.3 `VoragoVoiceConfig` (FR-004) — designated initialisers only

```cpp
struct VoragoVoiceConfig {
    std::size_t maxBlockSamples      = 2048;  ///< clamped [1, kMaxBlockSamples]
    std::size_t numNoiseSources      = 4;     ///< clamped [1, NoiseOrganism::kMaxSources]
    std::size_t numResonancePeaks    = 12;    ///< clamped [1, ResonanceDriftNetwork::kMaxPeaks]
    std::size_t numEcologyLoops      = 6;     ///< clamped [1, FeedbackEcology::kMaxLoops]
    std::size_t ecosystemAgents      = 32;    ///< clamped [kMinAgents, EcosystemEngine::kMaxAgents]
    std::size_t ecosystemCells       = 64;    ///< clamped [1, kMaxResourceCells]
    std::size_t ecosystemStepChunks  = 8;     ///< clamped [8, 64]
    std::size_t bloomChildSlots      = kBloomChildSlots;  ///< clamped [0, kMaxChildren]
    float       maxCombDelayMs       = 50.0f; ///< forwarded to NoiseOrganism, clamped [5, 200]
};
```

No atmosphere field: `AtmosphereEngine` is engine-owned (FR-004, FR-042). Every field is **clamped,
never rejected**; each owner clamps again, and the getter that reports it is the owner's
(`getNumSources()`, `getNumPeaks()`, `getAgentCount()`, …), so the voice stores no shadow of a capacity
it does not own.

### S2.4 Public API — the complete shape

```cpp
class VoragoVoice {
public:
    VoragoVoice() noexcept = default;
    // NON-COPYABLE AND NON-MOVABLE, stated rather than silently produced: ContinuousBody
    // user-declares a deleted copy ctor and no move members (continuous_body.h:647-648) and
    // AtmosphereEngine-style deletion propagates; a `= default`ed move here would be DEFINED
    // AS DELETED while reading as if the type were movable (seraphis_voice.h:214-226).
    VoragoVoice(const VoragoVoice&) = delete;   VoragoVoice& operator=(const VoragoVoice&) = delete;
    VoragoVoice(VoragoVoice&&) = delete;        VoragoVoice& operator=(VoragoVoice&&) = delete;

    // --- lifecycle (FR-003, FR-005, FR-006). THE THREAD COLUMN IS B-7's TABLE,
    //     and it is part of the contract: exactly one of these four reaches
    //     FeedbackEcology::reset()'s O(buffer) wipe, and it is not on the audio thread.
    void prepare(double sampleRate, const VoragoVoiceConfig& cfg) noexcept;  // ONLY allocating path
    /// CONTROL THREAD ONLY (B-7): reaches ecology_.reset(), ~786 KB of std::fill at
    /// 48 kHz and ~3.1 MB at 192 kHz. Clears the armed fade tail.
    void reset() noexcept;
    /// RT-SAFE (B-7). reset()'s twin with ecology_.silenceAudio() in place of
    /// ecology_.reset(). Clears the fade tail, because a poisoned tail must not
    /// survive the recovery. Sole caller: S6.6 step 2 (FR-072).
    void resetForRecovery() noexcept;
    /// RT-SAFE (B-7). Same body; PRESERVES the armed fade tail
    /// (seraphis_voice.h:399-405). Sole caller: the engine's steal teardown.
    void resetForSteal() noexcept;
    /// RT-SAFE (B-7). Arms kSilenceRampMs, then clears run state through the same path.
    void silence() noexcept;
    [[nodiscard]] std::size_t getAllocatedBytes() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // --- render (FR-007, FR-008, FR-009) ------------------------------------
    void processStereoBlock(float* outL, float* outR, std::size_t n) noexcept;
    void advanceLifeOnly(std::size_t n) noexcept;

    // --- notes (FR-013) ------------------------------------------------------
    void noteOn(float frequencyHz, float velocity) noexcept;
    void noteOff() noexcept;
    [[nodiscard]] bool  isFinished() const noexcept;
    [[nodiscard]] float getCurrentLevel() const noexcept;
    [[nodiscard]] bool  hasRenderedSinceNoteOn() const noexcept;

    // --- seeding (FR-025) ----------------------------------------------------
    void setSeed(std::uint32_t seed) noexcept;
    [[nodiscard]] std::uint32_t getSeed() const noexcept;

    // --- the identity layer's published request (FR-020b) --------------------
    [[nodiscard]] float getGhostRequest() const noexcept;

    // --- the two FR-026 life-modulator lanes, PUBLISHED so they are observable
    //     rather than inferred. Without these two getters FR-026 has no assertion
    //     of its own anywhere in the plan and an implementation that advanced both
    //     modulators and discarded their outputs would pass every case (S10.3a,
    //     VoragoVoice_LifeModulatorLanes).
    /// FR-026 lane 2: the fog depth this voice publishes for the ENGINE to fold onto
    /// its own smear base (S6.6 step 5). EXACTLY `std::max(0.0f, tide_.getCurrentValue())`
    /// - a NET, not a fold (S3.6 (c)'s construction, so a tide trough cannot pull fog
    /// BELOW the engine's base), held between control steps, and exactly 0.0f at tidal
    /// depth 0. There is NO second `tideDepth_` factor: TidalModulator already scales by
    /// its own depth inside the component (tidal_modulator.h:207-210, :317). The voice
    /// never touches SpectralSmear; it does not own one (FR-002, FR-052).
    [[nodiscard]] float getTidalFogDepth() const noexcept;
    /// FR-026 lane 1: EXACTLY `breath_.getCurrentValue()`, the SIGNED term the voice adds
    /// to `gravityBase_` before writing ResonanceDriftNetwork::setGravity each control
    /// step. Again no second depth factor - BreathingModulator returns
    /// `std::clamp(depth_ * bipolar, -1, 1)` (breathing_modulator.h:286, read back at
    /// :222), so the lane is already depth-scaled. NOT `getBreathingDepth()`, which is
    /// FR-071's read-back of the CONFIGURED depth: deliberately different names for
    /// deliberately different things.
    [[nodiscard]] float getBreathingGravityLane() const noexcept;

    // --- envelope (FR-014) ---------------------------------------------------
    void setEnvelopeMode(EnvelopeMode) noexcept;
    void setEnvelopeStageTimeMs(int stage, float ms) noexcept;
    void setEnvelopeReleaseMs(float ms) noexcept;
    void setGrowthDurationSeconds(float seconds) noexcept;
    [[nodiscard]] EnvelopeMode getEnvelopeMode() const noexcept;
    [[nodiscard]] float getEnvelopeStageTimeMs(int stage) const noexcept;
    [[nodiscard]] float getEnvelopeReleaseMs() const noexcept;
    [[nodiscard]] float getEnvelopeOutput() const noexcept;

    // --- the macro-writable surface (S7's Voice-owned targets) ---------------
    // Each is a one-to-one forwarder onto a shipped setter EXCEPT where the comment
    // says it fans out; none adds clamping, because the owner already clamps and a
    // second guard would only let the two surfaces disagree (seraphis_voice.h:641-647).
    void setRichness(float r) noexcept;              // ALSO updates cloudRichness_ (B-2)
    void setSpectralTiltDb(float dbPerOct) noexcept; // ALSO pushes bloom_.setConsumerTiltDb (FR-012)
    void setMutation(float m) noexcept;
    void setInharmonicity(float B) noexcept;
    void setDriftDepthCents(float cents) noexcept;
    void setStereoSpread(float s) noexcept;
    void setNoiseLevelDb(float dB) noexcept;         // fan-out: every configured source slot
    void setNoiseWakeBase(float w) noexcept;         // fan-out: FR-021's per-destination base
    void setNoiseWanderRate(float hz) noexcept;
    void setResonanceGravity(float g) noexcept;      // base for FR-016's two summed lanes
    void setResonanceMix(float m) noexcept;
    void setResonanceWanderRate(float hz) noexcept;
    void setEcologyMix(float m) noexcept;
    void setEcologyLoopGain(float g) noexcept;       // fan-out: every configured loop
    void setBodyBlend(float b) noexcept;             // FR-036/FR-037, per-sample ramped
    void setBodyDamping(float d) noexcept;           // fan-out: BOTH bodies
    void setBodyResonance(float r) noexcept;         // fan-out: BOTH bodies
    void setBodyMix(float m) noexcept;               // fan-out: BOTH bodies
    void setBodyMaterialA(ContinuousBody::BodyMaterial) noexcept;
    void setBodyMaterialB(ContinuousBody::BodyMaterial) noexcept;
    void setEcosystemDepth(float d) noexcept;        // FR-021's routing depth
    void setEventRateScale(float s) noexcept;        // FR-022's interval scale, Life's target
    void setBloomDepth(float d) noexcept;
    void setBloomSpawnRateHz(float hz) noexcept;
    void setBreathingDepth(float d) noexcept;
    void setBreathingIrregularity(float i) noexcept;
    void setTidalDepth(float d) noexcept;
    // ... and the matching getter for EVERY one of them (FR-071's read-back rule).

    // --- sub-component read access for tests and for the engine --------------
    [[nodiscard]] const HarmonicCloud&          cloud()     const noexcept;
    [[nodiscard]] const NoiseOrganism&          noise()     const noexcept;
    [[nodiscard]] const ResonanceDriftNetwork&  resonance() const noexcept;
    [[nodiscard]] const FeedbackEcology&        ecology()   const noexcept;
    [[nodiscard]] const BloomEngine&            bloom()     const noexcept;
    [[nodiscard]] const EcosystemEngine&        ecosystem() const noexcept;
    [[nodiscard]] const ContinuousBody&         bodyA()     const noexcept;
    [[nodiscard]] const ContinuousBody&         bodyB()     const noexcept;
    [[nodiscard]] const SlowEventScheduler&     scheduler(std::size_t i) const noexcept;
    [[nodiscard]] const BreathingModulator&     breathing() const noexcept;
    [[nodiscard]] const TidalModulator&         tide()      const noexcept;
    [[nodiscard]] const GrowthEnvelope&         growth()    const noexcept;

private:
    friend struct detail::VoragoVoiceSilenceRampProbe;   // SC-011
    friend struct detail::VoragoEngineNonFiniteProbe;    // SC-029 (B-4)
    friend class  VoragoEngine;        // the engine reads/writes its own voices
    friend class  VoragoMacroMatrix;   // S7's apply() needs non-const voice access
};
```

`getAllocatedBytes()` sums the six sub-components that publish one — `NoiseOrganism` (`:999`),
`ResonanceDriftNetwork`, `FeedbackEcology`, `BloomEngine`, `EcosystemEngine` — and **says in its
doxygen that `HarmonicCloud`, `ContinuousBody` and `MultiStageEnvelope` publish no such figure**, so it
is a partial accounting and SC-014's real gate is `AllocationScope`, not this number. A getter that
quietly claimed to be complete would be the worse of the two options.

### S2.5 Private state layout

```cpp
// --- owned sub-components (FR-002; exactly these, by value) ------------------
HarmonicCloud          cloud_;
NoiseOrganism          noise_;
ResonanceDriftNetwork  resonance_;
FeedbackEcology        ecology_;
BloomEngine            bloom_;
std::array<ContinuousBody, kNumBodies> bodies_;     // A = 0, B = 1
EcosystemEngine        ecosystem_;
MultiStageEnvelope     mse_;
GrowthEnvelope         growth_;
std::array<SlowEventScheduler, kNumEventSchedulers> sched_;
BreathingModulator     breath_;
TidalModulator         tide_;
// FORBIDDEN, and the size guard is what enforces it: ModulationEngine, VoiceModRouter,
// PolySynthEngine, SynthVoice, CavernVerb, SubharmonicEngine, SpectralSmear,
// AtmosphereEngine (FR-002).

// --- scratch: ONE control chunk each (D1). Nine buffers, 2 304 B/voice --------
// excL_/excR_ carry the signal from the cloud all the way to the body inputs:
// ResonanceDriftNetwork (:506) and FeedbackEcology (:908) BOTH document that the
// inputs may alias the outputs, so those two stages run IN PLACE and no extra
// pair is needed. ContinuousBody does NOT support in place (:1155-1156 as cited by
// seraphis_voice.h:1129), hence the four body buffers.
std::array<float, kControlChunkSamples> excL_{}, excR_{};
std::array<float, kControlChunkSamples> noiseMono_{};
std::array<float, kControlChunkSamples> bodyAL_{}, bodyAR_{}, bodyBL_{}, bodyBR_{};
std::array<float, kControlChunkSamples> carryL_{}, carryR_{};
std::size_t carryAvail_ = 0, carryRead_ = 0;
bool carryIsLifeOnly_ = true;

// --- the bloom/cloud spectrum handoff (FR-011; EXACTLY kMaxSlots entries) -----
std::array<float, BloomEngine::kMaxSlots> ratios_{};      // 64 - the normative precondition
std::array<float, BloomEngine::kMaxSlots> amplitudes_{};  // (bloom_engine.h:466-481)
std::size_t parentCount_ = 0;
bool        targetActive_ = false;   // the FR-011 edge latch

// --- noise decorrelation (FR-015, B-3) ---------------------------------------
Biquad noiseApL_, noiseApR_;
float  noiseApDelayR_ = 0.0f;
LinearRamp noiseGain_;              // kGainRampMs, driven by setNoiseLevelDb

// --- the two-body blend (FR-036, FR-037) -------------------------------------
LinearRamp blend_;                  // per-SAMPLE advanced inside the chunk

// --- identity routing (FR-020, FR-020a, FR-021, FR-022, FR-023) --------------
std::array<std::uint8_t, EcosystemEngine::kMaxAgents> agentSlot_{};   // fixed at prepare()
std::array<std::uint8_t, EcosystemEngine::kMaxAgents> agentValid_{};
std::array<float, NoiseOrganism::kMaxSources>         noiseWakeBase_{};
std::array<float, ResonanceDriftNetwork::kMaxPeaks>   peakWakeBase_{};
std::array<float, FeedbackEcology::kMaxLoops>         loopWakeBase_{};
float ecosystemDepth_ = 0.0f;
float ghostRequest_   = 0.0f;       // FR-020b, held between control steps
// The FR-021/FR-026 bases the identity layer sums onto, and the two lanes it PUBLISHES.
// publishIdentity() is the only writer of the last two; S2.4's getters are the only readers.
float gravityBase_ = 0.0f, mutationBase_ = 0.15f, bloomDepthBase_ = 0.60f;   // FR-016/FR-020
float breathGravityLane_ = 0.0f, tidalFogDepth_ = 0.0f; // the PUBLISHED lane values (FR-026)
// The two modulator DEPTHS are deliberately NOT shadowed: setBreathingDepth/setTidalDepth
// forward to breath_.setDepth/tide_.setDepth and the FR-071 getters read the components'
// own breathing_modulator.h:189 / tidal_modulator.h:214 - the S2.3 rule (the voice stores
// no shadow of a value it does not own) and B-2's corrected reasoning applied again.
std::array<Xorshift32, kNumEventSchedulers> slotDrawRng_{};   // FR-022's seeded slot draw
std::array<std::uint8_t, kNumEventSchedulers> lastEventTarget_{};
std::array<std::uint8_t, kNumEventSchedulers> drawnSlot_{};
std::array<bool, kNumEventSchedulers> eventWasActive_{};

// --- level detector / retirement (FR-013) -------------------------------------
float level_ = 0.0f;
float levelReleaseCoeff_ = 0.0f;
int   quiescentChunks_ = 0;              // seeded AT quiescentChunksToRetire_ in clearRunState()
int   quiescentChunksToRetire_ = 7500;   // derived at prepare(): 10 s at 48 kHz

// --- envelope shadows (the seraphis_voice.h:585-596 single write path) --------
EnvelopeMode envMode_ = EnvelopeMode::Standard;
std::array<float, MultiStageEnvelope::kMaxStages> stageTimeMs_{}, stageLevel_{};
float releaseMs_ = 45000.0f, velocity_ = 1.0f, envOutput_ = 0.0f;

// --- silence carry (the D3 anti-click tail) -----------------------------------
float fadeTailL_ = 0.0f, fadeTailR_ = 0.0f, lastOutL_ = 0.0f, lastOutR_ = 0.0f;
int   fadeRemaining_ = 0, silenceRampSamples_ = 48;

// --- bookkeeping ---------------------------------------------------------------
double sampleRate_ = 48000.0;
bool prepared_ = false, hasSounded_ = false, renderedSinceNoteOn_ = false;
std::uint32_t seed_ = 1u;
float cloudRichness_ = 0.70f;      // B-2's shadow; the ONE source of p(r)
float cloudTiltDb_   = -4.0f;      // FR-012's consumer-tilt mirror
float eventRateScale_ = 1.0f;
```

**`std::array<VoragoVoice, kMaxVoices>` is several hundred KB.** The engine carries the same warning
Seraphis does (`seraphis_engine.h:201-204`): never a test local, always heap-allocated.

---

## S3. `VoragoVoice` — lifecycle and the control chunk

### S3.1 `prepare(double, const VoragoVoiceConfig&)` — numbered, and the order is load-bearing

1. **Rate floor.** `sampleRate_ = (sampleRate > 1.0) ? sampleRate : 1.0` — the
   `seraphis_voice.h:238` idiom. Each owner floors again at its own `kMinUsableSampleRate = 8000.0`
   (`ecosystem_engine.h:201`, `bloom_engine.h:229`), which is FR-076's "substituted, then floored".
2. **Clamp the config, never reject** (FR-004). Every field through `std::clamp` against the owner's
   own published bounds, named in S2.3.
3. **Sub-component prepare**, designated initialisers throughout (no positional brace init anywhere —
   Clang errors on narrowing where MSVC does not, `ecosystem_engine.h:290-301`):
   ```cpp
   cloud_.prepare(sampleRate_);                                    // harmonic_cloud.h:282
   noise_.prepare(sampleRate_, NoiseOrganism::PrepareConfig{
       .maxBlockSamples = maxBlock, .maxCombDelayMs = combMs, .numSources = numNoise});
   resonance_.prepare(sampleRate_, ResonanceDriftNetwork::PrepareConfig{
       .maxBlockSamples = maxBlock, .numPeaks = numPeaks});
   ecology_.prepare(sampleRate_, FeedbackEcology::PrepareConfig{
       .maxBlockSamples = maxBlock, .numLoops = numLoops});
   bloom_.prepare(sampleRate_, BloomEngine::PrepareConfig{
       .capacity = kMinCloudCapacity, .numChildSlots = childSlots});   // B-1 raises it per chunk
   ecosystem_.prepare(sampleRate_, EcosystemEngine::PrepareConfig{
       .agentCount = agents, .resourceCells = cells, .energyBudget = 1.0,
       .initialPoolFraction = 0.5, .stepIntervalChunks = stepChunks});
   for (auto& b : bodies_) { b.prepare(sampleRate_); }                 // continuous_body.h:867
   mse_.prepare(static_cast<float>(sampleRate_));                      // :73 takes FLOAT
   growth_.prepare(sampleRate_);
   for (auto& s : sched_) { s.prepare(sampleRate_); }
   breath_.prepare(sampleRate_);
   tide_.prepare(sampleRate_);
   ```
4. **Seeds, before the first note.** `applySeeds()` (S3.7). `ContinuousBody::setSeed` is
   configure-time only and deliberately not retro-deterministic (`continuous_body.h:1339-1344`), so it
   must happen here, not at `noteOn`.
5. **The FR-090 default table**, written in one block, every row including the unchanged ones, so the
   table *is* the code (the `seraphis_voice.h:263-333` shape). S8 is that table.
6. **The agent → destination deal** (S3.6 (a)), computed once from `ecosystem_.getAgentKind(i)`, which
   is fixed for the life of this `prepare()`/`setSeed()` pair (`ecosystem_engine.h:2168-2216`).
7. **Derived constants.**
   ```cpp
   levelReleaseCoeff_ = std::exp(-static_cast<float>(kControlChunkSamples)
                                 / (0.001f * kLevelReleaseMs * static_cast<float>(sampleRate_)));
   silenceRampSamples_ = std::max(1, static_cast<int>(std::lround(
                              0.001f * kSilenceRampMs * static_cast<float>(sampleRate_))));
   quiescentChunksToRetire_ = std::max(1, static_cast<int>(std::lround(
                              kQuiescentSeconds * sampleRate_
                              / static_cast<double>(kControlChunkSamples))));   // FR-013
   noiseGain_.configure(NoiseOrganism::kGainRampMs, static_cast<float>(sampleRate_));
   blend_.configure(kBlendRampMs, static_cast<float>(sampleRate_));
   noiseApL_.setCoefficients(BiquadCoefficients{.b0 = kNoiseApCoeffL, .b1 = 0.0f, .b2 = 1.0f,
                                                .a1 = 0.0f, .a2 = kNoiseApCoeffL});
   noiseApR_.setCoefficients(BiquadCoefficients{.b0 = kNoiseApCoeffR, .b1 = 0.0f, .b2 = 1.0f,
                                                .a1 = 0.0f, .a2 = kNoiseApCoeffR});
   ```
   `levelReleaseCoeff_` is **not** `calculateOnePolCoefficient`: that helper treats its argument as a
   time-to-99 % (`smoother.h:86-93`), which would give `tau = 20 ms` here and break FR-013's
   derivation (`seraphis_voice.h:369-373`).
8. `prepared_ = true; reset();` — so a freshly prepared voice is silent (FR-003).

`prepare()` may be called repeatedly (FR-077). Configuration — seeds, every setter value, the macro
bases — survives, because every owner's own `prepare()` re-derives **state, never configuration**
(`ecosystem_engine.h:330-335`). The one exception is `NoiseOrganism`, whose `prepare()` is documented
to restore its own FR-016 defaults (`noise_organism.h:219-221`); step 5's default block therefore runs
on **every** `prepare()`, which is what makes this class's contract uniform. Stated in the header.

### S3.2 `reset()`, `resetForRecovery()`, `resetForSteal()`, `silence()` — one body, four entry points, two threads (B-7)

`clearRunState(bool rtSafe)` is the shared body: every sub-component's `reset()` **except**
`FeedbackEcology`, which takes `rtSafe ? ecology_.silenceAudio() : ecology_.reset()` (B-7 — the O(1)
read-mute clear versus the O(buffer) wipe); all nine scratch buffers and
both spectrum arrays cleared, `carryAvail_ = carryRead_ = 0`, `carryIsLifeOnly_ = true`,
`noiseGain_.snapTo(...)`, `blend_.snapTo(...)`, `noiseApL_/R_.reset()`, `noiseApDelayR_ = 0`,
`level_ = 0`, `ghostRequest_ = 0`, the slot-draw RNGs re-seeded from `seed_`,
`hasSounded_ = renderedSinceNoteOn_ = false`, `velocity_ = 1`, and

```cpp
// SEEDED AT THE RETIRE VALUE, not 0 (seraphis_voice.h:1022-1027). The counter only advances
// inside renderOneChunk/advanceOneChunkLifeOnly, so with a 0 seed every never-rendered slot
// would report isFinished() == false and the engine would take the full render path on all
// kMaxVoices slots for the first quiescentChunksToRetire_ chunks - ten seconds of it.
quiescentChunks_ = quiescentChunksToRetire_;
```

The four entry points, and the **one call** that separates the control-thread one from the other
three (B-7 part 3):

* **`reset()`** = `clearRunState(/*rtSafe=*/false)` + clear the fade tail. **NOT AN AUDIO-THREAD
  OPERATION** — this is the only path that reaches `ecology_.reset()`'s `std::fill` over six
  power-of-two delay buffers (~786 KB at 48 kHz, ~3.1 MB at 192 kHz, `feedback_ecology.h:861-862`,
  `:2401-2412`). Sole callers: `prepare()` step 8 and `VoragoEngine::reset()`/`silence()`, both of
  which S6.7 and S9 already declare not-audio-thread.
* **`resetForRecovery()`** = `clearRunState(true)` + clear the fade tail. FR-072's deferred recovery
  (S6.6 step 2). The tail is cleared for the reason the old text gave for using `reset()` there — a
  poisoned voice must not carry a poisoned fade tail into the next note — and the *wipe* is dropped
  for B-7 part 2's reason: the read-mute window makes the ring unobservable for exactly one delay
  length, so a non-finite sample sitting in it is never read.
* **`resetForSteal()`** = `clearRunState(true)` only; its sole caller is the engine's steal teardown,
  and it **preserves** the armed tail (`seraphis_voice.h:399-405`).
* **`silence()`** captures `lastOut*` into `fadeTail*`, arms `fadeRemaining_ = silenceRampSamples_`,
  runs `clearRunState(true)`, and discards the un-served carry — the `seraphis_voice.h:424-438`
  construction, including the reason it cannot fade by rendering (a steal is issued *between* blocks,
  so there are no samples for a fade to occupy).

A steal runs `silence()` then `resetForSteal()`, so `clearRunState(true)` executes **twice**. That is
harmless now and was the defect before: at `clearLoopAudio`'s O(1) it is a few hundred nanoseconds
twice, at `ecology_.reset()`'s O(buffer) it was ~1.6 MB of `std::fill` per steal at 48 kHz, on the
audio thread, on what S6.7 itself calls "the normal allocation path".

`FeedbackEcology`'s sleep edge clears its loop audio state by design (Phase 5 FR-063, roadmap lines
565–567); the voice does **not** re-implement that and does **not** assume symmetry with the other
wake surfaces (FR-024, Edge Cases). `silenceAudio()` is not that path either — it is the same
`clearLoopAudio` owner reached directly, with no gate ramp and no control step, which is what a steal
needs and what the sleep edge cannot give it.

### S3.2a The one append-only `FeedbackEcology` addition (B-7)

This phase makes **two** shared-component changes, not one. S4 is the `ContinuousBody` material
append (AR-4); this is the second, and it is three lines.

| | |
|---|---|
| File | `dsp/include/krate/dsp/systems/feedback_ecology.h` |
| Change | **append** one public method, `void silenceAudio() noexcept`, beside `reset()` (`:854`) |
| Body | `if (!prepared_) { return; } for (std::size_t i = 0; i < config_.numLoops; ++i) { clearLoopAudio(i); }` |
| Deletions | **none** — the diff is `git diff --numstat` zero-deleted-lines, the same bar FR-039 sets for `continuous_body.h` |
| New names | **none** — no class, no member, no enumerator, no constant; `lint-odr.js` sees nothing new |
| Seraphis exposure | **none** — a repo-wide `grep -rn "feedback_ecology.h" dsp/ plugins/` run this session names only Vorago Layer 3 peers (`bloom_engine.h`, `ecosystem_engine.h`, `subharmonic_engine.h` — all comment references), `dsp/lint_all_headers.cpp:185` and the component's own five test TUs. No Seraphis file and no plugin includes it, so SC-016's Seraphis gate does not widen. |
| Green gate | `dsp_systems_tests.exe "FeedbackEcology*"` in full, run explicitly as S11.2 step 3a |
| Doxygen | states that it is the RT-safe half of `reset()`, cites `:861-862` for why `reset()` is not, and cites `:2412-2425` for why the read-mute window is equivalent to the wipe |

Two consequences the header must also state, because they are the ways a later reader gets this
wrong:

1. `silenceAudio()` clears **audio only**. It does not touch `crossfadeCount_`, `clampEngagements_`,
   `nonFiniteResets_`, `controlPhase_` or `laneCounter_` — `reset()`'s step 1 — because those are
   accounting, and an RT path that silently zeroed a monotone counter would make
   `FeedbackEcology`'s own SC clauses unreadable. `clearLoopAudio` already documents the same rule
   for `crossfadeCount` (`:2420-2424`).
2. It is bounded by `config_.numLoops`, not `kMaxLoops`, so a voice configured with fewer loops pays
   for fewer. `reset()` deliberately walks all `kMaxLoops` (`:809`) because it is the control-thread
   path and clearing a dormant loop's ring costs nothing there.

### S3.3 `processStereoBlock` / `advanceLifeOnly`

Byte-for-byte the `seraphis_voice.h:451-506` construction: the guard ladder (null → write nothing;
`n == 0` → consume no control step; `!prepared_` → `n` zeros and no advance), then the carry serve loop
with `lastOut*` captured **at serve time** — not from `carryL_[63]` at render time, which on a
mid-chunk steal is up to 63 samples of program material away from the amplitude the output actually
reached (`:471-478`, and this spec's Edge Case "a steal arriving mid-chunk"). `advanceLifeOnly` runs
the same loop over `advanceOneChunkLifeOnly()`.

### S3.4 `renderOneChunk()` — exactly `kControlChunkSamples`, always (FR-010)

```cpp
constexpr std::size_t n = kControlChunkSamples;
renderedSinceNoteOn_ = true;

// 1. IDENTITY LAYER (FR-020 ... FR-023). Advanced BEFORE anything reads a wake value,
//    so the whole chunk renders against one consistent identity state.
ecosystem_.processChunk(n);                    // ecosystem_engine.h:431
for (std::size_t k = 0; k < kNumEventSchedulers; ++k) { sched_[k].processBlock(n); }
breath_.processBlock(n);                       // breathing_modulator.h:209
tide_.processBlock(n);                         // tidal_modulator.h:250
publishIdentity();                             // S3.6 - the ONLY writer of the wake/depth surfaces

// 2. BLOOM -> CLOUD SPECTRUM HANDOFF (FR-011, FR-012, B-1, B-2)
updateSpectrumTarget();                        // S3.5

// 3. EXCITATION: the cloud
cloud_.processStereoBlock(excL_.data(), excR_.data(), n);       // harmonic_cloud.h:878

// 4. EXCITATION: the noise organism, decorrelated and summed in (FR-015, B-3)
noise_.processBlock(noiseMono_.data(), n);                      // noise_organism.h:414 - MONO
for (std::size_t s = 0; s < n; ++s) {
    const float g  = noiseGain_.process();                      // LinearRamp, per sample
    const float m  = noiseMono_[s] * g;
    const float aL = noiseApL_.process(m);                      // biquad.h:352
    const float aR = noiseApR_.process(noiseApDelayR_);         // the one-sample branch delay (B-3)
    noiseApDelayR_ = m;
    excL_[s] += aL;
    excR_[s] += aR;
}

// 5. THE VOICE ENVELOPE, IN PLACE ON THE EXCITATION BUS (AR-5). NOTHING DOWNSTREAM
//    OF THIS POINT IS GATED - seraphis_voice.h:1066, adopted normatively.
if (envMode_ == EnvelopeMode::Growth) {
    growth_.processBlock(n);
    const float gGrowth = growth_.getCurrentValue();            // held across the chunk
    for (std::size_t s = 0; s < n; ++s) {
        const float g = velocity_ * gGrowth * mse_.process();
        excL_[s] *= g; excR_[s] *= g; envOutput_ = g;
    }
} else {
    for (std::size_t s = 0; s < n; ++s) {
        const float g = velocity_ * mse_.process();             // multi_stage_envelope.h:223
        excL_[s] *= g; excR_[s] *= g; envOutput_ = g;
    }
}

// 6. RESONANCE - IN PLACE. "inputs may alias the outputs, in either pairing"
//    (resonance_drift_network.h:506).
resonance_.processBlock(excL_.data(), excR_.data(), excL_.data(), excR_.data(), n);

// 7. ECOLOGY - IN PLACE, same documented aliasing licence (feedback_ecology.h:908).
ecology_.processBlock(excL_.data(), excR_.data(), excL_.data(), excR_.data(), n);

// 8. THE TWO-BODY BLEND (FR-036, FR-037). NOT in place - ContinuousBody forbids it
//    (seraphis_voice.h:1128-1129 quoting continuous_body.h:1155-1156). BOTH bodies run
//    at every blend value, so the composed render never depends on blend history.
bodies_[0].processStereoBlock(excL_.data(), excR_.data(), bodyAL_.data(), bodyAR_.data(), n);
bodies_[1].processStereoBlock(excL_.data(), excR_.data(), bodyBL_.data(), bodyBR_.data(), n);
for (std::size_t s = 0; s < n; ++s) {
    const float b = blend_.process();          // PER SAMPLE (FR-037) - never stepped at the
    const float a = 1.0f - b;                  // chunk boundary, which SC-017a measures
    carryL_[s] = a * bodyAL_[s] + b * bodyBL_[s];
    carryR_[s] = a * bodyAR_[s] + b * bodyBR_[s];
}

// 9. SILENCE FADE TAIL (guarded: fadeRemaining_ must never run negative, which would add
//    an inverted, magnitude-GROWING tail forever - seraphis_voice.h:1152-1162).
for (std::size_t s = 0; s < n && fadeRemaining_ > 0; ++s) {
    const float w = static_cast<float>(fadeRemaining_) / static_cast<float>(silenceRampSamples_);
    carryL_[s] += fadeTailL_ * w;  carryR_[s] += fadeTailR_ * w;  --fadeRemaining_;
}

// 10. LEVEL DETECTOR + RETIREMENT, on the POST-blend buffer - exactly what the voice contributes.
float chunkPeak = 0.0f;
for (std::size_t s = 0; s < n; ++s) {
    chunkPeak = std::max(chunkPeak, std::max(std::fabs(carryL_[s]), std::fabs(carryR_[s])));
}
updateLevel(chunkPeak);

carryAvail_ = n; carryRead_ = 0; carryIsLifeOnly_ = false;
// lastOut* is NOT assigned here - it is captured at SERVE time (S3.3).
```

`advanceOneChunkLifeOnly()` runs **step 1 only**, plus `updateLevel(0.0f)`, plus a zero-filled carry
and `lastOutL_ = lastOutR_ = 0.0f`. It deliberately does **not** run step 2: `BloomEngine::processChunk`
is a spectrum write, and its clock draw is taken once per control step regardless of dormancy or wake
(`bloom_engine.h`'s `clockRng_` note — "a pure function of elapsed control steps"). Advancing it in both
paths would make a voice that alternates idle and rendering run the bloom clock twice per chunk. The
invariant SC-030 asserts — `EcosystemEngine::getControlStepCount()` and the scheduler event counts equal
across the two paths — is a property of step 1, and step 1 is exactly what both paths share.

`updateLevel` is Seraphis's verbatim (`seraphis_voice.h:1178-1183`):

```cpp
level_ = (chunkPeak > level_) ? chunkPeak : chunkPeak + (level_ - chunkPeak) * levelReleaseCoeff_;
quiescentChunks_ = (level_ < kTailSilenceThreshold) ? (quiescentChunks_ + 1) : 0;
```

`isFinished()` is `quiescentChunks_ >= quiescentChunksToRetire_`. At −90 dBFS with a 10 s counter the
100 ms detector release is **not** the hysteresis — the counter is. The header says so, so nobody
later "tunes" `kLevelReleaseMs` expecting it to matter.

### S3.5 `updateSpectrumTarget()` — the FR-011 / FR-012 / B-1 / B-2 body

```cpp
void updateSpectrumTarget() noexcept {
    // FR-012: the bloom's slot budget tracks what the cloud will actually SOUND (B-1), and its
    // consumer tilt tracks the cloud's tilt (mirrored by setSpectralTiltDb, S2.4).
    const std::size_t active   = cloud_.getActivePartialCount();               // :950
    const std::size_t capacity = std::clamp(active, kMinCloudCapacity,
                                            HarmonicCloud::kMaxPartials);      // B-1's floor
    if (capacity != bloom_.capacity()) { bloom_.setCapacity(capacity); }        // :628

    parentCount_ = bloom_.reserveBase();                                       // :710 - B-1
    // B-2: the cloud's OWN laws, evaluated off the cloud's OWN table, so the parent region is
    // bit-identical to the untargeted render at every richness/tilt/gravity/B setting.
    const float p = HarmonicCloud::kRichnessMinExponent
                  + (HarmonicCloud::kRichnessMaxExponent - HarmonicCloud::kRichnessMinExponent)
                    * cloudRichness_;
    for (std::size_t i = 0; i < parentCount_; ++i) {
        ratios_[i]     = static_cast<float>(i + 1);                            // FR-082 identity branch
        amplitudes_[i] = std::exp2(-p * detail::kHarmonicCloudLog2N[i]);       // :57-62, :1492
    }
    const std::size_t count = bloom_.processChunk(ratios_.data(), amplitudes_.data(),
                                                  parentCount_, kControlChunkSamples);  // :494

    // FR-011's two edges. While the bloom owns at least one live child the cloud reads the
    // supplied target; the moment it owns none the cloud returns to its own parametric laws, so
    // the richness rolloff, the tilt, the gravity warp and the inharmonicity stretch are never
    // shadowed by a stale target. Both transitions go through the cloud's own dirty-flag path and
    // its FR-014 amplitude smoother, which is what makes them click-free
    // (harmonic_cloud.h:861-866); SC-018a measures both.
    const bool wantTarget = (bloom_.getLiveChildCount() > 0);                  // :712
    if (wantTarget) {
        cloud_.setSpectralTarget(ratios_.data(), amplitudes_.data(), count);   // :800
    } else if (targetActive_) {
        cloud_.clearSpectralTarget();                                          // :862
    }
    targetActive_ = wantTarget;
}
```

Two invariants the header states and SC-018 asserts at every control step:
`count <= HarmonicCloud::kMaxPartials`, and `bloom_.reserveBase() >= kMinParentSlots` (B-1's floor).
`setSpectralTarget` can never be rejected here: `count` is in `[1, 64]`, every ratio is `> 0`, every
amplitude is `>= 0` and finite — the four rejection conditions at `harmonic_cloud.h:801-803`, checked
off one by one in the header comment.

### S3.6 `publishIdentity()` — the FR-020 series, in one function

This is the **only** writer of `setSourceWake` / `setPeakWake` / `setLoopWake` / `setMutation` /
`setDepth` / `ghostRequest_`, which is what makes FR-021's "at depth 0 every destination reads its
configured base" a property of one expression rather than of five call sites.

**(a) The deal, computed once at `prepare()` (FR-020).** Agent kinds come from a seeded stratified deal
followed by a Fisher–Yates shuffle (`ecosystem_engine.h:2168-2216`), so kind is **not** `i % 5` and the
deal must be read, never assumed:

```cpp
void buildAgentDeal() noexcept {
    std::array<std::size_t, EcosystemEngine::kNumKinds> cursor{};
    agentValid_.fill(0);
    for (std::size_t i = 0; i < ecosystem_.getAgentCount(); ++i) {
        const std::size_t k = static_cast<std::size_t>(ecosystem_.getAgentKind(i));   // :894
        const std::size_t slots = slotCountForKind(k);   // Partial 1, Resonator numPeaks,
                                                         // Noise numSources, Feedback numLoops, Ghost 1
        if (slots == 0) { continue; }
        agentSlot_[i]  = static_cast<std::uint8_t>(cursor[k] % slots);   // round-robin over that kind
        agentValid_[i] = 1;
        ++cursor[k];
    }
}
```

**(b) The many-to-one reduction (FR-020a, Q2).** Several agents of one kind can address the same slot
(the deal wraps), and `Resonator` agents can outnumber `kMaxPeaks` leaving surplus slots unaddressed.
A slot reads **only the strongest addressing agent**, `argmax` over
`EcosystemEngine::getAgentEnergy(i)` (`:891`, a `double`). A per-control-step scan is accepted cost
(Q2). One pass, no sort, no allocation — `kNumKinds × kMaxSlotsPerKind` (5 × 12) of stack:

```cpp
// bestE initialised to -1.0 (energies are >= 0), bestV to 0.0f.
for (each agent i with agentValid_[i]) {
    const auto k = kindOf(i);
    const auto s = agentSlot_[i];
    const double e = ecosystem_.getAgentEnergy(i);
    if (e > bestE[k][s]) { bestE[k][s] = e; bestV[k][s] = ecosystem_.getAgentOutput(i); }
}
```

Ties: strict `>` keeps the **lower agent index**, which is deterministic under a fixed seed. SC-019b's
enumerated table includes a tie and asserts exactly that, so the rule is observable rather than
incidental.

**(c) The scheduler contribution (FR-022, Q7).** Per scheduler `k`, the five destination families are
fixed and `setTargetCount(5)` is written at `prepare()`:

```
family := sched_[k].getActiveTarget()        // 0..4, or kNoTarget (0xFF) while idle
value  := max(0.0f, sched_[k].getCurrentValue())   // setBipolarProbability(0) makes every event
                                                    // positive; the max is a net, not a fold
families = { BloomTrigger, NoiseWake, PeakWake, LoopWake, GhostBurst }
```

The **slot within a multi-slot family** is drawn once per event from `slotDrawRng_[k]` (FR-022, Q2).
An event onset is the rising edge of `isEventActive()` (`slow_event_scheduler.h:361`), latched in
`eventWasActive_[k]`; on that edge the voice draws
`drawnSlot_[k] = rng.next() % familySlotCount(family)` and holds it for the whole event. Same seed and
configuration ⇒ same slot sequence; a different seed ⇒ a different one (SC-020a).
`BloomTrigger` calls `bloom_.triggerBloom()` **on the onset edge only** — `armed_` is edge-like and a
level-polling caller is explicitly the natural bug the component guards against
(`bloom_engine.h:654-663`).

**(d) The combine rule (FR-023) — the maximum, stated once, asserted by SC-019a.**

```cpp
const float eco   = ecosystemDepth_ * reducedEcosystemValue(kind, slot);   // (b), FR-021
const float sched = schedulerValueFor(kind, slot);                          // (c)
const float wake  = std::max(base(kind, slot), std::max(eco, sched));
```

Neither source can silence a slot the other woke, and with `ecosystemDepth_ == 0` **and** every
scheduler depth 0 the destination reads exactly its configured base — SC-019 clause 1's stated
precondition. Written through the shipped setters and nothing else — `noise_.setSourceWake(slot, w)`
(`:844`), `resonance_.setPeakWake(peak, w)` (`:771`), `ecology_.setLoopWake(loop, w)` (`:1261`) — so
dormancy semantics stay the components' own and the voice adds no second gate (FR-024).

`Partial`-kind agents drive `cloud_.setMutation(clamp(mutationBase_ + eco, 0, 1))` and
`bloom_.setDepth(clamp(bloomDepthBase_ + eco, 0, 1))` (FR-020's first table row).
`Ghost`-kind agents and the `GhostBurst` scheduler family drive **only**

```cpp
ghostRequest_ = std::max(ecoGhost, schedGhost);    // FR-020b, held between control steps
```

The voice never touches `AtmosphereEngine` — it does not own one (FR-002, FR-056).

**(e) The two life modulators (FR-026).**
`resonance_.setGravity(clamp(gravityBase_ + breath_.getCurrentValue(), -1, 1))` — two
lanes summed onto base `0`, with the `Gravity` macro owning `gravityBase_` (FR-016, Q4).
**There is no second `breathDepth_` factor, and that is a reading of the component, not an
omission:** `BreathingModulator::getCurrentValue()` is already depth-scaled inside the component —
`return std::clamp(depth_ * bipolar, -1.0f, 1.0f)` (`breathing_modulator.h:286`, surfaced at `:222`)
— so multiplying by a voice-side shadow of the same depth would apply it twice and make the
`Movement → BreathingDepth` row quadratic in its own parameter. The lane is published verbatim as
`getBreathingGravityLane()` (S2.4).
**`HarmonicCloud::setSpectralGravity` is driven by neither lane** and keeps its FR-090 value.
`getTidalFogDepth()` (S2.4) publishes `std::max(0.0f, tide_.getCurrentValue())` — a **net, not a
fold**, exactly the construction (c) uses for the scheduler value, so a tide *trough* cannot pull fog
**below** the engine's base; and again with no second `tideDepth_` factor, because `TidalModulator`
scales by its own depth inside the component (`tidal_modulator.h:207-210`, `:317`). The engine folds
that value onto its own smear **base** at its control step (S6.6 step 5) — roadmap
line 257's "fog rolls in via `TidalModulator`", expressed without the voice reaching a component it
does not own.

**Both lanes are published, and both are asserted.** The breathing lane is readable as
`getBreathingGravityLane()` and the tidal lane as `getTidalFogDepth()`, and
`VoragoVoice_LifeModulatorLanes` (S10.3a) is FR-026's only assertion anywhere in this plan: without
it, an implementation that advanced `breath_` and `tide_` once per chunk and then discarded both
outputs — breath depth effectively 0, the tidal fold never summed — would pass every criterion in
S10.3, because SC-008's `Gravity` row exercises only the macro lane and SC-008's `Entropy` fold
clause reads `BreathingIrregularity` back as a setter value rather than as a driver of gravity.

### S3.7 Seeding (FR-025) and the note surface (FR-013)

```cpp
void applySeeds() noexcept {
    cloud_.setSeed(deriveStreamSeed(seed_, kCloudSalt));          // core/random.h:102
    noise_.setSeed(deriveStreamSeed(seed_, kNoiseSalt));
    resonance_.setSeed(deriveStreamSeed(seed_, kResonanceSalt));
    ecology_.setSeed(deriveStreamSeed(seed_, kEcologySalt));
    bloom_.setSeed(deriveStreamSeed(seed_, kBloomSalt));
    ecosystem_.setSeed(deriveStreamSeed(seed_, kEcosystemSalt));
    bodies_[0].setSeed(deriveStreamSeed(seed_, kBodyASalt));
    bodies_[1].setSeed(deriveStreamSeed(seed_, kBodyBSalt));
    breath_.setSeed(deriveStreamSeed(seed_, kBreathSalt));
    tide_.setSeed(deriveStreamSeed(seed_, kTideSalt));
    for (std::size_t k = 0; k < kNumEventSchedulers; ++k) {
        sched_[k].setSeed(deriveStreamSeed(seed_, kSchedSaltBase + k));
        slotDrawRng_[k].seed(deriveStreamSeed(seed_, kSlotDrawSaltBase + k));   // FR-022 (Q2)
    }
    // GrowthEnvelope::setSeed is a documented NO-OP (growth_envelope.h:140) and is deliberately
    // not called - the seraphis_voice.h:927-930 note, restated.
}
```

`deriveStreamSeed` substitutes `0x2545F491u` when the hash lands on 0 (`core/random.h:110`), which is
why seed 0 is legal. `setSeed` re-derives immediately when prepared; `ContinuousBody::setSeed` is
configure-time only, so callers seed before the first note and the header says so.

`noteOn(frequencyHz, velocity)`: `hasSounded_ = true; renderedSinceNoteOn_ = false;` drop an **idle**
carry so the onset is sample-accurate, but **keep a live one** — dropping it would skip up to 63
rendered samples and create exactly the click SC-011 measures (`seraphis_voice.h:519-528`); clamp the
velocity; then

```cpp
cloud_.setFundamentalHz(f);  cloud_.noteOn();                          // :383, :635
bodies_[0].setNoteFrequencyHz(f);  bodies_[1].setNoteFrequencyHz(f);   // :1190
resonance_.setNoteFrequency(f);                                        // :624
mse_.gate(true);
if (envMode_ == EnvelopeMode::Growth) { growth_.trigger(); }
```

The header documents, and does **not** repair, that `ContinuousBody` clamps note frequency to
`[20, 8000]` and `HarmonicCloud` to `[20, 4000]`, so MIDI 127 lands at different places in the two
engines — the `seraphis_voice.h:512-516` precedent and this spec's own Edge Case.

`noteOff()` is `cloud_.noteOff(); mse_.gate(false);` and **nothing else** (AR-5, D3).

### S3.8 The envelope (FR-014, FR-014a, Q5)

Six stages, sustain point 4, so the shipped shape is exactly what FR-014 pins — attack 20 s, three
body stages inside 30–60 s, release 45 s — and the stage table is written through the **single write
path** `applyStage()` with the `seraphis_voice.h:585-596` shadow discipline (the shadows are the source
of truth, `setEnvelopeMode` restores from them, and a direct `mse_.setStage` anywhere else would make a
Standard → Growth → Standard round trip silently install a 0 ms attack).

| Stage | Level | Time (ms) | Role |
|---:|---:|---:|---|
| 0 | 1.00 | 20 000 | attack (FR-014, reproduced not re-derived) |
| 1 | 0.80 | 30 000 | body stage 1 (30–60 s) |
| 2 | 0.92 | 45 000 | body stage 2 (30–60 s) |
| 3 | 0.85 | 60 000 | body stage 3 (30–60 s) |
| 4 | 0.85 | 0 | sustain hold (`kEnvelopeSustainPoint`) |
| 5 | 0.00 | 0 | post-sustain |
| release | — | 45 000 | FR-014 |

**FR-014 says "the 4-stage `MultiStageEnvelope`" and this plan ships `kEnvelopeStages = 6`. That is a
deviation, and it is carried explicitly rather than absorbed** (the AR-6 / B-4 discipline). FR-014's
"4-stage" is a statement about the **pre-sustain walk** — attack plus three body stages, which is
exactly stages 0–3 above and exactly the three numbers FR-014 pins. Stages 4 and 5 exist because
`MultiStageEnvelope`'s `advanceToNextStage()` only enters `Sustaining` at
`currentStage_ == sustainPoint_` (`seraphis_voice.h:565-579`, `multi_stage_envelope.h:215`), so a
sustain-hold stage at `kEnvelopeSustainPoint = 4` and a post-sustain stage at 5 are required by the
component's own contract, both at **0 ms**, and neither adds a millisecond of envelope. Recorded in
S14's amendment list as a one-line spec clarification: FR-014's "4-stage" means four pre-sustain
stages.

`setRetriggerMode(RetriggerMode::Legato)` is written explicitly at `prepare()`: the component default
is `Hard`, which would restart a 20 s attack on every re-articulation
(`seraphis_voice.h:362-364`, `multi_stage_envelope.h:215`).
`Growth` mode forces **every** stage from 0 up to `sustainPoint − 1` to 0 ms, preserving level and
curve — zeroing stage 0 alone is not enough, because `advanceToNextStage()` only enters `Sustaining`
at `currentStage_ == sustainPoint_` (`seraphis_voice.h:565-579`).

**`kFastAttackEnvelopeConfig` (FR-014a)** lives once, in `tests/test_helpers/vorago_fixtures.h`
(S10.2), as a POD of six `{level, ms}` pairs plus a release, with attack **50 ms**, every stage time
**50 ms** and release **100 ms** — all ≤ FR-014a's 100 ms ceiling. SC-021a, SC-008 and SC-022 (2) cite
it **by name**; SC-004b does not and may not (FR-014a's second sentence).

---

## S4. Dark materials — the append-only `ContinuousBody` change (AR-4, FR-030 – FR-039)

### S4.0 The ruling that shapes everything else: all six new materials are **Modal**

`configureNonModalSlot` reaches only `referenceHz`, `t60AtMaxResonanceSec` and `hfDampingParam`: the
waveguide's stiffness and pick position are the class constants `kWgStiffness` / `kWgPickPosition`
(`continuous_body.h:192-193`) and the comb bank's spread and count are `kCombSpread` / `kNumCombs`
(`:197`, `:99`), none of which is a `MaterialProfile` field. Two Comb materials would therefore differ
only in pitch, T60 and damping — not enough for SC-015's "distinct from every other new material by
≥ 5 %" — and, because there is exactly one `TimeVaryingCombBank` and one `WaveguideString` per object
(`engineInstanceFreeFor`, `:2229`), a crossfade between two Comb materials cannot run two engines and
takes the FR-024a collapse instead, which is a **different workload** from the one
`continuous_body_perf_test`'s crossfade baseline was measured against.

`configureModalSlot` (`:2151`), by contrast, passes **six** profile fields into
`ModalResonatorBank::setModes` — `ratios`, `defaultModeCount`, `amplitudeExponent`, `damping{b1,b3}`,
`stretch`, `scatter` — and modal bank *i* is bound to slot *i*, so two modal materials can always
crossfade properly. The modal engine is also the **cheapest** of the three: the file's own measurement
note records that with the mode loop vectorised end to end "the 32-mode plate is CHEAPER than the
single waveguide string" (`continuous_body_perf_test.cpp:160-165`).

**Ruling:** every one of the six is `Engine::Modal` with `defaultModeCount <= kModeCountCeiling (32)`.
That is what makes S4.4's budget claim structural rather than hopeful — the workload each new material
presents is the one the shipped baselines were *already* set against — and it keeps the identity of a
material in **data**, which is what roadmap line 459's "data authoring, not new DSP" means.

### S4.1 The three new ratio tables (FR-033, FR-034)

Each is a `static constexpr std::array<float, kModeCountCeiling>` at class scope, appended below
`kPlateRatios` (`continuous_body.h:705`), each with the sourcing block
`continuous_body.h:673-704` uses for Glass and Plate, and each guarded by a `constexpr` strict-ascent
predicate in the `aetherTableStrictlyAscending` shape (`cavern_verb.h:121`):

```cpp
[[nodiscard]] static constexpr bool ratioTableStrictlyAscending(
    const std::array<float, kModeCountCeiling>& t) noexcept {
    for (std::size_t i = 1; i < t.size(); ++i) { if (!(t[i] > t[i - 1])) { return false; } }
    return true;
}
static_assert(ratioTableStrictlyAscending(kRoomModeRatios),    "FR-033 / :669-671");
static_assert(ratioTableStrictlyAscending(kClampedPlateRatios), "FR-033 / :669-671");
static_assert(ratioTableStrictlyAscending(kBarRatios),          "FR-033 / :669-671");
```

Strict ascent over **all 32** entries is what makes FR-043's Nyquist *prefix* truncation exact: if mode
*k* is above the guard, so is every mode after it (`:669-671`).

| Table | Physical basis (the FR-034 citation the header carries) | Generating law |
|---|---|---|
| `kRoomModeRatios` | Rectangular-room eigenmodes — Rayleigh's equation; Kuttruff, *Room Acoustics*, §3.1. Room proportions **1 : 1.26 : 1.59** (Sepmeyer's low-degeneracy set), which is why the series is not a degenerate comb. | `f(nx,ny,nz) = (c/2)·sqrt((nx/Lx)² + (ny/Ly)² + (nz/Lz)²)` over `nx,ny,nz ∈ [0,4]`, excluding `(0,0,0)`; sorted ascending, normalised by the first, thinned to a minimum spacing of **1.5 %** so strict ascent holds, first 32 kept. `r[0] = 1.0` exactly. |
| `kClampedPlateRatios` | Clamped-edge circular plate — Leissa, *Vibration of Plates* (NASA SP-160), Table 4.4; Fletcher & Rossing, *The Physics of Musical Instruments*, ch. 3. Published first eight: **1.000, 2.080, 3.410, 3.890, 5.000, 5.950, 6.820, 8.280**. | The eight published values verbatim, then a **constant-modal-density linear continuation** of the LSQ slope over `k = 4..8`, anchored at `k = 8` — exactly the technique `kPlateRatios` documents for the free plate (`continuous_body.h:695-704`), because a thin plate's modal density is asymptotically constant (Cremer & Heckl). |
| `kBarRatios` | Free–free flexural bar/column — Fletcher & Rossing, ch. 2 (the bar series a marimba bar is tuned away from). | `r[n] = (β_n / β_1)²` with `β_1..β_5 = 4.73004, 7.85320, 10.99561, 14.13717, 17.27876` and `β_n = (2n+1)π/2` for `n ≥ 6`. First eight: **1.0000, 2.7565, 5.4039, 8.9330, 13.3443, 18.6379, 24.8137, 31.8718**. |

**The tables are generated, not typed.** `tools/gen-vorago-material-tables.js` (Node — the project rule)
prints all three as paste-ready `constexpr` initialiser blocks with 4-decimal fixed formatting and
explicit `f` suffixes, asserts strict ascent and the 1.5 % thinning for the room set, and prints the
LSQ slope it used for the plate continuation so the number in the header comment is the number the
script computed. The script is committed beside the header change; the generated values are pasted in
(never `#include`d), exactly as `kGlassRatios` and `kPlateRatios` are literals today.

### S4.2 The six profiles (FR-030, FR-031, FR-032, FR-035)

Appended to `kMaterialProfiles` in **enumerator order**, designated initialisers with explicit `f`
suffixes throughout (`continuous_body.h:717-725` states why: Clang errors on narrowing where MSVC does
not). `kNumMaterials` becomes `11`; `BodyMaterial` gains six enumerators **after `Ice`**, so
`Glass = 0, Strings, MetalPlate, Chamber, Ice` keep their values and no stored Seraphis material index
moves (FR-030, FR-039).

| Material | `ratios` | modes | `amplitudeExponent` α | `damping {b1, b3}` | `stretch` | `scatter` | `referenceHz` | `t60` (s) | What makes it dark, and what makes it *this* material |
|---|---|---:|---:|---|---:|---:|---:|---:|---|
| `StoneChamber` | `kRoomModeRatios` | 32 | **1.80** | `{0.90, 2.0e-7}` | 0.00 | 0.15 | 82.0 | 9.0 | A room, not an object: the mode set is the room-eigenvalue series, so the partials are dense and inharmonic-but-ordered. α 1.8 starves everything above the first few modes; `b1` 0.9 is nearly twice Glass's 0.50. |
| `SteelTank` | `kClampedPlateRatios` | 32 | **1.20** | `{0.45, 8.0e-8}` | 0.08 | 0.05 | 98.0 | 16.0 | The longest of the six and the least damped — a steel end-plate rings. Its darkness comes from α and from a clamped-plate series whose low modes are closer together than the free plate's. |
| `WoodenHull` | `kPlateRatios` *(shared, the Ice/Glass precedent)* | 24 | **2.20** | `{1.40, 6.0e-7}` | 0.20 | 0.30 | 130.0 | 3.5 | Wood's internal loss factor is an order of magnitude above metal's (Fletcher & Rossing, ch. 3), which is `b1` 1.40 and `b3` 6e-7; 24 modes rather than 32 because the HF modes are gone anyway, and that is also the cheapest row of the six. |
| `CathedralColumn` | `kBarRatios` | 32 | **1.50** | `{0.60, 1.2e-7}` | 0.00 | 0.05 | 65.0 | 20.0 | A tall thin column: the free–free bar series, lowest `referenceHz` of the six, longest `t60`, near-zero scatter — it is the one that sounds *tuned*. |
| `CavernWall` | `kRoomModeRatios` *(shared with StoneChamber)* | 32 | **2.00** | `{1.10, 4.0e-7}` | 0.35 | **1.00** | 55.0 | 6.0 | Same room series, driven to the bank's scatter ceiling (`modal_resonator_bank.h:702`) with a large positive stretch — the rock face that is not a room. Exactly the Ice-from-Glass construction (`continuous_body.h:783-820`), applied to a different parent. |
| `GlassSphere` | `kGlassRatios` *(shared with Glass and Ice)* | 20 | **2.40** | `{1.60, 9.0e-7}` | 0.10 | 0.20 | 147.0 | 5.0 | A **thick** sphere: the same shell law, but α 2.40 against Glass's 1.00 and `b1` 1.60 against Glass's 0.50. It is the darkest of the six by construction and the pair that proves the axis is α-and-damping, not the ratio table. |

`hfDampingParam` equals `damping.b3` for every row (the modal convention the five shipped rows follow,
`continuous_body.h:747`, `:767`, `:829`).

**The FR-035 darkness axis is α, then damping, then mode count — in that order.** `a_k = k^-α`
(`continuous_body.h:660`), and every shipped material sits at α ≤ 1.0 (Glass 1.0, MetalPlate 0.7,
Ice 0.9; Strings and Chamber do not use the field). Every new material sits at α ≥ 1.20. If SC-015
measures a centroid that is **not** below the minimum of the five shipped, the lever is to raise that
material's α and `b1` and re-measure — **never** to relax SC-015 (roadmap line 558). The same three
levers, applied in the same order, are FR-038b's response to a material over budget, with "drop the
material and surface it to the user" as the terminal step.

### S4.3 The seven `kNumMaterials`-sized sites (FR-038, FR-038a) — exactly what changes

`ContinuousBody` gains one appended line:

```cpp
/// The five materials shipped with Seraphis Phase 4; the prefix seraphis_perf_test.cpp
/// surveys, so appending materials cannot re-point Seraphis's measured SC-001/SC-002
/// subject (Vorago Phase 10 FR-038a).
static constexpr std::size_t kNumSeraphisMaterials = 5;
```

| # | Site | Now | Becomes |
|---|---|---|---|
| 1 | `continuous_body_perf_test.cpp:283` | `static_assert(kNumMaterials == 5, "SC-005 measures 5 materials x 4 configurations = 20 measurements")` | `== 11`, message "11 x 4 = 44" |
| 2 | `continuous_body_perf_test.cpp:352` | `constexpr std::array<BodyMaterial, kNumMaterials> kMaterials` (5 initialisers) | **eleven** initialisers, enumerator order |
| 3 | `continuous_body_perf_test.cpp:360` | `constexpr std::array<const char*, kNumMaterials> kMaterialNames` (5 initialisers) | **eleven** initialisers, same order |
| 4 | `continuous_body_perf_test.cpp:373-379` | `crossfadePartner(m) = (idx + 1) % kNumMaterials` | **wraps inside each block**: `(i + 1) % kNumSeraphisMaterials` for `i < 5`, `5 + ((i - 5 + 1) % 6)` for the six new. Plain widening would re-point Ice's partner from Glass to StoneChamber and change a **Seraphis-measured** crossfade pairing, which SC-016 clause 4 forbids. |
| 5 | `continuous_body_test.cpp:1082` | `REQUIRE(CB::kNumMaterials == 5u)` | `== 11u` (the sibling `REQUIRE(kMaterialProfiles.size() == kNumMaterials)` at `:1081` needs no edit) |
| 6 | `seraphis_perf_test.cpp:419` | `static_assert(ContinuousBody::kNumMaterials == 5, "SC-001 measures all five materials and uses the worst")` | **re-pointed** at `kNumSeraphisMaterials`, message unchanged in substance |
| 7 | `seraphis_perf_test.cpp:571`, `:579`, `:654`, `:662`, `:1196` | `kMaterials` / `kMaterialNames` sized by `kNumMaterials`; `MaterialSurvey::nsPerBlock` sized by it; `surveyMaterials()` and the report loop bounded by it | all **re-sized / re-bounded to `kNumSeraphisMaterials`**; the five initialisers and their contents are **unchanged**, so `kMaterials[survey.worstIndex]` at `:1205` keeps selecting from exactly the five materials Seraphis's checked-in baselines were set against |

Sites 2, 3, 7 are the ones that would have **rotted rather than broken**: `constexpr std::array<T, N>`
with five initialisers still compiles at `N = 11` and zero-fills, giving six `BodyMaterial{0}` (Glass)
entries surveyed at `seraphis_perf_test.cpp:662` and six **null** `const char*` streamed at `:1196` —
undefined behaviour, not a build break. That is why FR-038 enumerates them and why SC-016 clause 1
checks the *modification scope* with `git diff --numstat --diff-filter=M`.

**No other Seraphis assertion, threshold, baseline or default moves.** Verification is mechanical
(SC-016 clauses 1–2):

```bash
git diff --numstat --diff-filter=M dsp/tests/unit/systems/     # exactly the three files above
git diff --numstat dsp/include/krate/dsp/systems/continuous_body.h   # exactly TWO deleted lines
git diff -U0  dsp/include/krate/dsp/systems/continuous_body.h        # ...and they are the
                                                                     # BodyMaterial line and the
                                                                     # kNumMaterials line
```

Everything else in the header — the six profile rows, the three ratio tables, `kNumSeraphisMaterials`,
the three `static_assert`s — is a **pure append** (FR-039, the Phase 3 `resonator_bank.h` precedent).

### S4.4 Why SC-025 is expected green, and what to do if it is not

`continuous_body_perf_test.cpp` asserts `steady`, `operating`, `crossfade` and `cloudOnly` over **every**
material index at `kRegressionFactor = 1.5` (`:881-897`), and two of those four baselines are **capped,
not measurement-pinned** — the file records "operating 41.1 → 41,200, which exceeds
`kMaxAdmissibleHalfPctNs` by 15.7 %", capped at 35,500 (`:160-180`, `:218-229`) — so a new material
inherits **29 %** of headroom against the worst shipped measurement, not 50 %.

Three structural reasons the six should clear it:

1. **Same engine, same ceiling.** All six are Modal at `defaultModeCount <= 32`, and the 32-mode
   `MetalPlate` is already the pinned worst case; the mode loop is vectorised end to end, so cost is
   near-flat in the mode count above the SIMD granularity (`continuous_body_perf_test.cpp:155-165`).
2. **Two of the six are cheaper by construction** — `WoodenHull` at 24 modes and `GlassSphere` at 20.
3. **Every crossfade pairing inside the new block is Modal/Modal**, which is the pairing the fifth
   shipped row (Ice → Glass) already measures, and modal bank *i* is bound to slot *i*, so the pairing
   can never degrade into an FR-024a collapse.

If a row still misses: **re-voice it** (fewer modes → a cheaper engine is not available since all six
are already the cheapest engine → a gentler damping law), and if that fails, **drop the material from
the six and surface the drop to the user** (FR-038b). Never raise a baseline, never narrow the measured
configuration, never exclude a material from the loop — all three are the forbidden move of roadmap
line 558.

---

## S5. The two-body blend (FR-036, FR-037)

`out = (1 − b)·A + b·B`, with `b` advanced **per sample** by `blend_` (a `LinearRamp` configured at
`kBlendRampMs = 50 ms`) inside the chunk — S3.4 step 8. Three properties, and each is a criterion:

* **Both bodies always run** (`b = 0` and `b = 1` included), so the composed render never depends on
  blend history and SC-017 clause 4's ramp-return arm is true by construction rather than by luck. The
  cost is honest and is in the FR-081 projection as `ContinuousBody × 2`.
* **`b = 0` and `b = 1` are exact.** At `b = 0` the expression is `1.0f * A + 0.0f * B`; IEEE-754
  multiplication by exactly `0.0f` of a finite `B` is exactly `+0.0f` (and `-0.0f` for negative `B`,
  which adds to `A` as `A`), so body B contributes **bit-nothing**. That is precisely what SC-017
  clauses 1–3 assert — render equality across two different body-B *materials* and two different
  body-B *seeds* — and it is why the criterion is stated as contribution nullity rather than as
  "a single-body render", which is not a configuration this product has (there is no body-count field
  in `VoragoVoiceConfig`).
  The one caveat the header states: a non-finite `B` would make `0.0f * B` a NaN. It cannot occur —
  `ContinuousBody` contains its own non-finite input at the point of accumulation
  (`continuous_body.h:1402-1412`) and the engine contains the voice's output (S6.6) — and if it ever
  did, FR-072's containment is the mechanism, not a defensive multiply here.
* **The ramp is per sample, never chunk-stepped.** A chunk-stepped blend is a 64-sample staircase —
  the same zipper SC-010 guards for macros — and SC-017's endpoint and ramp-return clauses would not
  see it. **SC-017a** measures it directly, with SC-010's measurement shape.

`setBodyBlend(float b)` rejects a non-finite argument (previous value stands), clamps to `[0, 1]`, and
calls `blend_.setTarget(b)`; `getBodyBlend()` reports the clamped **target**, not the ramp position
(the `tape_saturator.h:248-252` / `:283` read-back convention). `reset()` snaps the ramp to the target
so a recycled voice does not ramp away from a stale blend — the `seraphis_voice.h:975-999` argument,
which is stated there as a defensive state restoration and is *not* defensive here: a blend of 1
followed by a `reset()` and a note is a perfectly ordinary sequence.

---

## S6. `VoragoEngine` — file, API, state, and the two render entry points

### S6.1 File and includes (FR-040)

`dsp/include/krate/dsp/systems/vorago_engine.h`, `namespace Krate::DSP`, header-only.

```cpp
#include <krate/dsp/core/db_utils.h>                  // detail::isFinite
#include <krate/dsp/core/random.h>                    // deriveStreamSeed
#include <krate/dsp/primitives/smoother.h>            // OnePoleSmoother (the sum gain)
#include <krate/dsp/processors/spectral_smear.h>
#include <krate/dsp/processors/tape_saturator.h>
#include <krate/dsp/processors/true_peak_limiter.h>
#include <krate/dsp/systems/atmosphere_engine.h>      // the GLOBAL ghost tap (FR-056)
#include <krate/dsp/systems/subharmonic_engine.h>
#include <krate/dsp/systems/voice_allocator.h>
#include <krate/dsp/systems/vorago_voice.h>
#include <algorithm> <array> <cassert> <cmath> <cstddef> <cstdint> <span>
```

**It must not include `effects/cavern_verb.h` or any other `effects/` header** (AR-1). `lint-layers.js`
flags a `systems/` → `effects/` include (`tools/lint-layers.js:74`), and the constructive reason is
AR-1: the cavern sits **outside** this class, at the documented seam between `processStereoBlock` and
`processOutputStage`.

### S6.2 Public constants

```cpp
/// The CEILING (S12 / OQ-1(a)) - and NOT a free one. Every slot between kDefaultPolyphony
/// and this value pays the life-only cost L on EVERY block, because the render loop bound
/// is unconditional (S6.5, B-6): at 8 and polyphony 4 that is ~124 000 ns/block, ~5.8 % of
/// kMaxAdmissibleNs. It stays 8 while SC-001a's survey runs so the whole {1,2,4,6,8} curve
/// is measurable; S12.3 RECOMMENDS lowering it to 6, and Q-A is the ruling.
static constexpr std::size_t kMaxVoices           = 8;
static constexpr std::size_t kDefaultPolyphony    = 4;      // the SHIPPED value (S12's recommendation)
static constexpr std::size_t kControlChunkSamples = 64;     // the same grid the voice runs
static constexpr std::size_t kMaxBlockSamples     = 2048;
/// FR-045. Disjoint from EVERY VoragoVoice salt (which occupy 0x0100..0x0C01).
static constexpr std::size_t kVoiceSaltBase       = 0xA000;
static constexpr std::size_t kAtmosSalt           = 0xB000;
/// FR-043. 100 ms, NOT 20 ms: the value is read once and HELD for a whole control chunk, so it
/// is a staircase whose first stair is 28.35 % of the step at 20 ms - measured at 2.651x the
/// click bound against 1.143x at 100 ms (seraphis_engine.h:219-246).
static constexpr float kSumGainSmoothMs      = 100.0f;
/// FR-044. -30 dBFS long-release steal amnesty (seraphis_engine.h:247-248).
static constexpr float kAmnestyLevelThreshold = 0.0316f;
/// FR-053. Low drive (roadmap line 462): the saturator is a gluing stage, not an effect.
static constexpr float kOutputSaturation = 0.12f;
static constexpr float kOutputDriveDb    = 0.0f;
static constexpr float kOutputCeilingDb  = -0.3f;    // TruePeakLimiter::setCeilingDb
/// At most one deferred non-finite voice CLEAR per control chunk (seraphis_engine.h:254).
/// The serviced call is VoragoVoice::resetForRecovery() - the RT-SAFE path (B-7) - and NOT
/// reset(). The earlier rationale here ("one whole-voice reset fits inside one chunk") was
/// arithmetically false and is corrected: reset() reaches FeedbackEcology::reset()'s
/// O(buffer) wipe, and ONE of its six fills "already exceeds" that component's own 8 889 ns
/// control-chunk budget (feedback_ecology.h:2401-2412), so a throttle sized against it would
/// still blow the chunk by an order of magnitude - on the path FR-072/SC-029 make mandatory,
/// in the same block as a non-finite event. Against the RT-safe path the throttle bounds the
/// work that ACTUALLY remains: two ContinuousBody::reset()s (documented "Real-time safe",
/// continuous_body.h:965-973), one EcosystemEngine re-derive, six O(1) clearLoopAudio calls
/// and the scalar state. VoragoVoice_ClearingPathCost [.perf] measures the figure this
/// constant is sized against, and asserts it stays an order of magnitude under reset().
static constexpr std::size_t kResetsPerControlChunk = 1;
/// FR-017's ghost gating: the level written when a burst is fully open. Base is 0.0.
static constexpr float kGhostBurstPeak = 0.60f;
static constexpr std::size_t kEngineSizeBound =
    kMaxVoices * VoragoVoice::kVoiceSizeBound + (64u * 1024u);   // seraphis_engine.h:264-287's rule
```

### S6.3 `VoragoEngineConfig` (FR-042, FR-052)

```cpp
struct VoragoEngineConfig {
    std::size_t maxBlockSamples = 2048;
    VoragoVoiceConfig voice{};            ///< forwarded verbatim to every voice
    // --- the global ghost tap (FR-056; moved off VoragoVoiceConfig by OQ-1 (b)) ---
    float       atmosCaptureSeconds = 20.0f;   ///< clamped to AtmosphereEngine's [1, 30]
    bool        atmosBlurEnabled    = true;
    bool        atmosFreezeEnabled  = false;   ///< no Phase-10 consumer; off saves the freeze FFT
    std::size_t atmosBlurFftSize    = 1024;
    std::size_t atmosFreezeFftSize  = 2048;
    // --- the global smear (FR-052) ---
    bool        smearEnabled = true;           ///< the component defaults to FALSE
                                               ///  (spectral_smear.h:141-149); Vorago's Fog macro
                                               ///  needs it, and SC-022 (2) needs BOTH states to
                                               ///  be reachable on the real object
    std::size_t smearFftSize = 2048;
};
```

`atmosCaptureSeconds = 20.0f` is the one figure here that is not a component default and it carries
its FR-091 justification: a ghost grain is 12 s long (FR-017), so a 4 s or 8 s capture ring could
never hold one. 20 s is the smallest ring that holds a whole grain plus the 0.90 position spread the
same table asks for. The memory cost is `2 × 20 × fs` floats — 7.3 MB at 48 kHz, **once**, because the
tap is global (OQ-1 (b)); per-voice at `kMaxVoices` it would have been 58 MB.

### S6.4 Public API

```cpp
class VoragoEngine {
public:
    VoragoEngine() noexcept = default;
    VoragoEngine(const VoragoEngine&) = delete;  VoragoEngine& operator=(const VoragoEngine&) = delete;
    VoragoEngine(VoragoEngine&&) = delete;       VoragoEngine& operator=(VoragoEngine&&) = delete;

    void prepare(double sampleRate, const VoragoEngineConfig& cfg) noexcept;  // ONLY allocating path
    void reset() noexcept;      // NOT an audio-thread operation - see S6.7
    void silence() noexcept;    // per-voice silence() + reset(), then the output stage

    void setPolyphony(std::size_t n) noexcept;                 // FR-043
    [[nodiscard]] std::size_t getPolyphony() const noexcept;
    void setSeed(std::uint32_t seed) noexcept;                 // FR-045
    [[nodiscard]] std::uint32_t getSeed() const noexcept;

    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept;   // FR-044
    void noteOff(std::uint8_t note) noexcept;

    void processStereoBlock(float* outL, float* outR, std::size_t n) noexcept;  // FR-050
    void processOutputStage(float* l, float* r, std::size_t n) noexcept;        // FR-053, AR-1

    [[nodiscard]] std::size_t getLatencySamples() const noexcept;      // FR-052
    [[nodiscard]] std::size_t getActiveVoiceCount() const noexcept;    // FR-047
    [[nodiscard]] std::size_t getRenderingVoiceCount() const noexcept;
    [[nodiscard]] float       getVoiceLevel(std::size_t i) const noexcept;
    [[nodiscard]] VoiceState  getVoiceState(std::size_t i) const noexcept;
    [[nodiscard]] const VoragoVoice& getVoice(std::size_t i) const noexcept;
    [[nodiscard]] int         getLastStolenVoiceIndex() const noexcept;   // SC-011/SC-012
    [[nodiscard]] std::uint32_t getNonFiniteRecoveryCount() const noexcept;  // FR-072
    [[nodiscard]] std::size_t getAllocatedBytes() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

    // --- the global chain's own surface (S7's Engine-owned macro targets) ---
    void setSubToneLevelOffsetDb(float dB) noexcept;   // fan-out over the three tones (FR-068 Weight)
    void setSubTrackingAmount(float a) noexcept;
    /// WRITES `smearBase_`, NOT `smear_` - and `getSmearAmount()` REPORTS `smearBase_`, not
    /// `smear_.getSmearAmount()`. FR-026's tidal fold re-writes the component every control
    /// chunk as `clamp(smearBase_ + maxOverRenderingVoices(getTidalFogDepth()), 0, 1)`
    /// (S6.6 step 5), so a setter that wrote the component directly would be silently
    /// overwritten on the next chunk and the Fog macro row would stop meaning anything; and
    /// a getter that reported the component would make SC-009 clause 1 ("row.base equals the
    /// value read back from that target's getter immediately after prepare()") compare
    /// against a number the matrix does not own. This is the SAME base-for-a-summed-lane
    /// annotation VoragoVoice::setResonanceGravity carries (S2.4), and it applies to every
    /// engine setter the control step folds a lane onto - which today is EXACTLY this one.
    void setSmearAmount(float a) noexcept;
    void setSmearDecoherence(float a) noexcept;
    void setSmearTilt(float t) noexcept;
    void setGhostPeakLevel(float v) noexcept;          // the FR-017 burst peak; base stays 0
    void setAtmosBlur(float a) noexcept;
    void setOutputSaturation(float a) noexcept;
    // ...and a getter for each (FR-071).

    // --- voice-parameter fan-out: the ONLY mutable route from outside to the voices ---
    // getVoice(i) is CONST and the Voice-owned macro rows reach the voices through
    // `friend class VoragoMacroMatrix`, so without these four there is no legal route to a
    // voice envelope at all - and S10.2's `applyFastAttack(VoragoEngine&)`, cited BY NAME by
    // SC-021a, SC-008 and SC-022 (2), plus SC-013a's "every macro, every exposed setter"
    // fuzz, would have to const_cast. Each fans out over ALL kMaxVoices, NOT polyphony_
    // (deliberately unlike VoragoMacroMatrix::apply, S7.4): these are CONFIGURATION, prepare()
    // installs them on every slot, and a later setPolyphony() growth must not admit a slot
    // carrying a different envelope. Stated here so the difference reads as a decision.
    void setEnvelopeMode(VoragoVoice::EnvelopeMode m) noexcept;
    void setEnvelopeStageTimeMs(int stage, float ms) noexcept;
    void setEnvelopeReleaseMs(float ms) noexcept;
    void setGrowthDurationSeconds(float seconds) noexcept;
    // ...and a getter for each, reading slot 0, which the fan-out keeps identical to every
    // other slot (FR-071). NO non-const getVoice(i) is provided: a caller able to
    // desynchronise two slots would falsify exactly that sentence, and SC-030's parity
    // clause rests on it.

    // --- read access for the tests and the macro matrix ---
    [[nodiscard]] const AtmosphereEngine&   atmosphere() const noexcept;   // SC-027 reads it here
    [[nodiscard]] const SubharmonicEngine&  subharmonic() const noexcept;  // SC-031 reads it here
    [[nodiscard]] const SpectralSmear&      smear() const noexcept;

private:
    friend class VoragoMacroMatrix;                     // apply() needs non-const voice access
    friend struct detail::VoragoEngineNonFiniteProbe;   // SC-029 (B-4)
};
```

State: `std::array<VoragoVoice, kMaxVoices> voices_;` (**heap-allocate the engine in every test** —
MSVC's default main-thread stack is 1 MiB and this object is hundreds of KB, `seraphis_engine.h:201-204`),
`VoiceAllocator allocator_;`, `AtmosphereEngine atmos_;`, `SubharmonicEngine sub_;`,
`SpectralSmear smear_;`, `TapeSaturator satL_, satR_;`, `TruePeakLimiter limiter_;`,
`OnePoleSmoother sumGain_;` + `float sumGainHeld_;`, six 64-sample scratch arrays
(`busL_/busR_`, `vL_/vR_`, `atmosL_/atmosR_`), `std::uint64_t sampleCounter_`,
`float smearBase_` (the FR-026 fold's base, written by `setSmearAmount` and reported by
`getSmearAmount` — S6.4), `std::uint32_t nonFinitePending_`, `nonFiniteRecoveries_`,
`stealTeardown_` (**the RA-4 assertion flag, not a deferral mechanism** — it is raised when the
steal path frees a victim slot and cleared in `noteOn` under
`assert(stealTeardown_ == 0u && "RA-4: allocator_.noteOn did not allocate the freed victim slot")`,
`seraphis_engine.h:516-519`; the name is inherited and is the one thing about it a reader
mis-reads, so the header says this in its declaration comment), `orphanTail_`,
`std::array<std::uint64_t, kMaxVoices> voiceSerial_`, `nextSerial_`, `int lastStolenVoice_`,
`int retriggerSlot_`, `std::size_t polyphony_`, `std::uint32_t seed_`, `bool prepared_`,
`float lastSubFundamentalHz_`. Plus the `static_assert(sizeof(VoragoEngine) <= kEngineSizeBound, …)`
below the class.

### S6.5 `processStereoBlock` — the exact loop (FR-050, FR-056, FR-072, B-5)

```cpp
void processStereoBlock(float* outL, float* outR, std::size_t n) noexcept {
    if (outL == nullptr || outR == nullptr) { return; }          // FR-008's guard order
    if (n == 0) { return; }                                       // consumes no control step
    if (!prepared_) { std::fill_n(outL, n, 0.0f); std::fill_n(outR, n, 0.0f); return; }

    std::size_t done = 0;
    while (done < n) {
        const auto phase = static_cast<std::size_t>(sampleCounter_ % kControlChunkSamples);
        if (phase == 0u) { runPreRenderControlStep(); }           // S6.6
        const std::size_t slice = std::min(n - done, kControlChunkSamples - phase);

        // --- 1. voice sum -----------------------------------------------------
        std::fill_n(busL_.data(), slice, 0.0f);
        std::fill_n(busR_.data(), slice, 0.0f);
        // The bound is v < kMaxVoices UNCONDITIONALLY: a high-water bound would leave spare
        // slots receiving neither processStereoBlock nor advanceLifeOnly, and FR-046/SC-030 are
        // written against ALL of them (seraphis_engine.h:523-527).
        for (std::size_t v = 0; v < kMaxVoices; ++v) {
            if (!isRendering(v)) { voices_[v].advanceLifeOnly(slice); continue; }   // FR-046
            voices_[v].processStereoBlock(vL_.data(), vR_.data(), slice);
            for (std::size_t s = 0; s < slice; ++s) {
                const float a = vL_[s];
                const float b = vR_[s];
                if (!isFiniteBits(a) || !isFiniteBits(b)) {       // FR-072, AT the accumulation point
                    nonFinitePending_ |= voiceBit(v);             // reset DEFERRED to the next
                    break;                                        // control step (S6.6 step 2)
                }
                busL_[s] += a;  busR_[s] += b;
            }
        }
        const float g = sumGainHeld_;                             // read ONCE per control chunk
        for (std::size_t s = 0; s < slice; ++s) { busL_[s] *= g;  busR_[s] *= g; }

        // --- 2. the GLOBAL ghost tap (FR-056, B-5) ---------------------------
        // Fed from the voice sum, BEFORE the subharmonic and the smear, so the ghost hears the
        // raw ensemble; its wet return is summed back into the SAME bus at the SAME point, so
        // the ghost shares the instrument's sub-weight and fog rather than sitting outside them.
        // A PLAIN SUM, no second gain: setLevel's trim is already applied inside the component
        // (atmosphere_engine.h:982) and multiplying by getLevel() again would square it
        // (seraphis_voice.h:1136-1141).
        atmos_.processStereoBlock(busL_.data(), busR_.data(),
                                  atmosL_.data(), atmosR_.data(), slice);     // :674, wet only
        for (std::size_t s = 0; s < slice; ++s) {
            busL_[s] += atmosL_[s];  busR_[s] += atmosR_[s];
        }

        // --- 3. the Layer-3 half of roadmap line 461's chain -----------------
        sub_.processBlock(busL_.data(), busR_.data(),
                          busL_.data(), busR_.data(), slice);     // subharmonic_engine.h:545, in place
        smear_.processBlock(busL_.data(), busR_.data(), slice);   // spectral_smear.h:341, in place

        std::copy_n(busL_.data(), slice, outL + done);
        std::copy_n(busR_.data(), slice, outR + done);

        sampleCounter_ += slice;
        done += slice;
        if (sampleCounter_ % kControlChunkSamples == 0u) { runPostRenderControlStep(); }
    }
}
```

Every component in step 2 and 3 carries an **absolute** control grid across calls
(`atmosphere_engine.h:699-707`, `subharmonic_engine.h:600-607`) or is hop-based and block-size
agnostic (`SpectralSmear`'s STFT FIFO), so handing them `slice` rather than a whole 64 is safe and
SC-007's partition invariance survives. The voices absorb the partition through their own carry FIFOs
(D1).

### S6.6 The two control steps

**`runPreRenderControlStep()`** — at phase 0, before the voices render:

1. **Sum gain**, read once and held for the chunk, so the value is identical under any caller
   partition. `sumGain_.advanceSamples(kControlChunkSamples - 1); sumGainHeld_ = sumGain_.process();`
   — the `- 1` is load-bearing because `OnePoleSmoother::process()` itself advances one sample
   (`seraphis_engine.h:1153-1156`).
2. **Non-finite recovery (FR-072)**, at most `kResetsPerControlChunk` bits of `nonFinitePending_`:
   `voices_[v].resetForRecovery()` and `++nonFiniteRecoveries_`. **Not** `resetForSteal()`, which
   preserves the fade tail, and a poisoned voice must not carry a poisoned tail into the next note;
   **not** `reset()` either, which is B-7's control-thread path and would put an O(buffer) wipe on the
   audio thread in the same block as the non-finite event. `resetForRecovery()` is exactly `reset()`
   minus that one call (S3.2), which is why it satisfies both constraints.
3. **The ghost level (FR-056, FR-017)**:
   ```cpp
   float ghost = 0.0f;
   for (std::size_t v = 0; v < kMaxVoices; ++v) {
       if (isRendering(v)) { ghost = std::max(ghost, voices_[v].getGhostRequest()); }   // FR-020b
   }
   atmos_.setLevel(ghostPeak_ * ghost);     // base 0.0; the MAXIMUM across voices, the same
                                            // non-silencing combine FR-023 establishes per voice
   ```
4. **The held sub-fundamental (FR-051)**: the lowest **sounding** voice's frequency, read from
   `allocator_.getVoiceFrequency(v)` over slots whose state is not `Idle`; when no voice sounds the
   **last value is held** and `sub_.setFundamentalHz` is not called at all — never reset to a default,
   which would glissando the subs on every note. `lastSubFundamentalHz_` is the shadow SC-031 reads
   through `subharmonic().getFundamentalHz()`.
5. **The tidal fog fold (FR-026, roadmap line 257)**:
   `smear_.setSmearAmount(clamp(smearBase_ + tidalDepthAcrossVoices, 0, 1))`, where the second term is
   the maximum of `voices_[v].getTidalFogDepth()` over rendering voices — the same non-silencing
   maximum FR-023 establishes per voice. **`smearBase_` is the engine's own field, written only by
   `VoragoEngine::setSmearAmount` and reported by `getSmearAmount()` (S6.4); the component is written
   only here.** That is the whole precedence rule, and it is why the `Fog` macro row survives the next
   control chunk. At tidal depth 0 the fold is the identity and the component reads exactly
   `smearBase_` — the clause `VoragoVoice_LifeModulatorLanes` (S10.3a) asserts.

**`runPostRenderControlStep()`** — after the slice that completed a chunk:

6. **Deferred retirement (FR-047)**: for each slot whose allocator state is `Releasing` **and** whose
   `voices_[v].isFinished()` is true, `allocator_.voiceFinished(v)`. Running it on the absolute grid
   rather than "once per block" makes retirement timing partition-invariant, which is what SC-007's
   `getActiveVoiceCount()` clause reads.
7. **Orphan-tail bookkeeping**: a post-shrink orphan that has rung itself out drops its bit
   (`seraphis_engine.h:1236-1243`).

### S6.7 Notes, stealing and polyphony (FR-043, FR-044, FR-048)

`VoiceAllocator` has **no `Quietest` mode** (`voice_allocator.h:55-60`) and "[d]oes NOT own or process
any DSP", so it cannot see a level. Selection therefore lives in the engine and the chosen slot is
**freed before** the allocator call, so the allocator's own idle search has exactly one candidate —
`seraphis_engine.h:1354-1425` verbatim, including:

* three passes — (0) `Releasing` **and below** `kAmnestyLevelThreshold`, (1) `Releasing` at any level,
  (2) `Active`; within a pass the lowest `getCurrentLevel()` wins, ties broken on the engine-owned
  `voiceSerial_` (the allocator's timestamp is private);
* `!(level < kAmnestyLevelThreshold)` rather than `level >= …`, so a non-finite level is treated as
  **not eligible** rather than as the quietest voice in the pool;
* pass 1 is not redundant: it is the branch that makes "every candidate is at or above the threshold"
  still steal the quietest `Releasing` voice instead of falling through to pass 2 and stealing nothing.

This is FR-044's reading of the amnesty — **it protects the loud, it does not prefer them** — and
SC-012's enumerated victim table is written against exactly these three passes. A steal calls the
victim's `silence()` then `resetForSteal()` then `noteOn()`, so the transition is a `kSilenceRampMs`
fade and never a cut (FR-005, SC-011). **Both of those calls are on B-7's RT-safe side** — they run
`clearRunState(true)`, whose ecology arm is `silenceAudio()`'s six O(1) `clearLoopAudio` calls and
never `FeedbackEcology::reset()`'s `std::fill` over six power-of-two buffers. That is not a detail:
with a 4-voice pool and minutes-long tails **stealing is the normal allocation path** (which is also
why FR-013 puts both retirement constants far below the amnesty line and why SC-012 stays
non-degenerate), so the pre-B-7 shape would have run ~1.6 MB of `std::fill` per steal at 48 kHz and
~6.2 MB at 192 kHz inside `VoragoEngine::noteOn`, on the audio thread.

`setPolyphony(n)` clamps to `[1, kMaxVoices]`, walks `allocator_.setVoiceCount(n)`'s NoteOff events
(treating each as a **musical release**, never a retirement — the allocator has already idled the
slot), marks still-sounding excess slots as orphans, and moves the sum-gain **target**
(`sumGainForPolyphony(n) = 1/sqrt(n)`), never the value. It allocates nothing: `prepare()` prepared all
`kMaxVoices` voices regardless of polyphony (FR-042).

`setSeed(s)` re-derives **every slot's** seed as `deriveStreamSeed(seed_, kVoiceSaltBase + v)`
(FR-045). **The seed is per slot and is never advanced per note** (FR-048): `noteOn` re-derives the
slot's *run* state from the fixed slot seed and does not consume, advance, reseed or perturb it. The
three consequences are normative and stated in the header — same slot + same note reproduces its
trajectory; a different slot does not; the whole instrument's render is a pure function of (engine
seed, configuration, note sequence), which is what makes a Phase 14 preset render reproducible.
SC-026 measures all three.

`VoragoEngine::reset()` and `VoragoEngine::silence()` carry the `seraphis_engine.h:384-421` warning:
**not audio-thread operations.** Both call `VoragoVoice::reset()` — B-7's control-thread path — on all
`kMaxVoices` slots, which is `kMaxVoices` × (~786 KB at 48 kHz, ~3.1 MB at 192 kHz) of ecology
`std::fill`, plus a `std::fill` over the atmosphere's whole 20 s stereo capture ring (~7.3 MB at
48 kHz). Allocation-free and lock-free, but nowhere near a bounded per-block cost.

The **RT-safe** trio is `VoragoVoice::silence()`, `resetForSteal()` and `resetForRecovery()` (S3.2,
B-7), and those are the only clearing calls any audio-thread path — a steal, a deferred non-finite
recovery — is permitted to reach. S9 repeats the split, because a section titled "RT safety" that
omitted it is exactly what let the pre-B-7 shape read as safe.

### S6.8 `processOutputStage` (FR-053, FR-054, AR-1)

```cpp
/// @brief FR-053. In place; THE CALLER RUNS THIS AFTER ITS REVERB (AR-1).
///
/// In the composed chain the buffer is the CavernVerb return:
///     engine.processStereoBlock(l, r, n);
///     cavern.processStereoBlock(l, r, l, r, n);    // Layer 4, owned by the CALLER
///     engine.processOutputStage(l, r, n);
/// Calling it on a buffer this engine did not produce is the INTENDED usage
/// (seraphis_engine.h:610-618).
void processOutputStage(float* l, float* r, std::size_t n) noexcept {
    if (l == nullptr || r == nullptr || n == 0 || !prepared_) { return; }   // FR-054
    for (std::size_t done = 0; done < n; done += kControlChunkSamples) {
        const std::size_t slice = std::min(kControlChunkSamples, n - done);
        satL_.process(l + done, slice);          // tape_saturator.h:335 - mono, in place
        satR_.process(r + done, slice);
    }
    limiter_.processBlock(l, r, static_cast<int>(n));   // true_peak_limiter.h:104 - ALWAYS LAST
}
```

The 64-sample loop is a **cadence choice, not a size constraint**: `TapeSaturator::prepare` ignores its
block-size argument (`tape_saturator.h:141`) and `process` is per-sample stateful and
partition-invariant (`seraphis_engine.h:613-617`). The limiter is always last and takes the whole
block, which is what makes FR-073's `|out| <= 1.0` a property of the stage rather than of the caller.

`getLatencySamples()` returns `smear_.getLatencySamples()` (`spectral_smear.h:472`) and **nothing
else** (FR-052, SC-022 clause 1): it is `fftSize_` when prepared and enabled, `0` otherwise. The
atmosphere's own blur latency is *not* added — it is a parallel wet path summed in, not a through-path
delay — and the cavern's latency belongs to the caller. Both facts are in the doxygen so a Phase 11
reader does not have to re-derive them.

### S6.9 Non-finite containment, and the probe that proves it runs (FR-072, SC-029, B-4)

Detection is at the accumulation point, per voice, per sample (S6.5), with **bit-pattern** tests only —
`detail::isFinite` (`core/db_utils.h:118`), never `std::isnan`/`std::isfinite`, which `-ffast-math`
licenses the compiler to fold away (roadmap line 570). A poisoned voice contributes 0 for the rest of
the slice; its bit is raised; the reset is serviced at the **next** pre-render control step, one slot
per chunk.

The recovery therefore satisfies all four of SC-029's assertions by construction: the block stays
finite (the poisoned voice is excluded from the sum), the counter increments **exactly once** per
serviced bit, the other voices are **bit-unchanged** (nothing in the containment path touches them),
and the poisoned voice resumes rendering after its `reset()` because its allocator state is untouched.
The probe (B-4) is what makes the assertion mean something:

```cpp
namespace detail {
/// Defined in dsp/tests/unit/systems/vorago_nonfinite_test.cpp - NOT in this header, so the class
/// definition is byte-identical in every build and no KRATE_DSP_* define is needed (the
/// seraphis_engine.h:193-195 + seraphis_nonfinite_test.cpp:107-111 construction).
struct VoragoEngineNonFiniteProbe;
}
```

It writes a bit pattern (`0x7FC00000u` qNaN, `0x7F800000u` +Inf, `0xFF800000u` −Inf) into voice *v*'s
**carry FIFO** and marks the carry servable, so the next slice the engine reads out of that voice is
genuinely non-finite and the engine's own scan is what discovers it. SC-029 repeats the injection for
**every** voice index, so a mis-indexed reset is caught.

---

## S7. `VoragoMacroMatrix` (FR-060 – FR-069, AR-6)

### S7.1 The enums

```cpp
/// OQ-2, confirmed at twelve. The three concepts of roadmap line 464 that are not here are
/// FOLDED, and the fold is normative on its survivor (FR-068): Decay -> Age + Depth,
/// Instability -> Entropy, Distance -> Fog.
enum class VoragoMacro : std::uint8_t {
    Darkness = 0, Age, Density, Movement, Gravity, Entropy,
    Pressure, Weight, Fog, Life, Depth, Mass, Count
};

enum class VoragoMacroTargetOwner : std::uint8_t { Voice = 0, Engine, Cavern };

/// Every parameter any row may write. EVERY enumerator is float-valued on a shipped setter
/// (Q1): there is no discrete-target row and this phase adds no DSP to express one.
/// Declared in owner blocks; the Cavern block's order IS VoragoCavernTargets' field order.
enum class VoragoMacroTarget : std::uint8_t {
    // -- Voice-owned -------------------------------------------------------
    CloudRichness, CloudSpectralTiltDb, CloudMutation, CloudInharmonicity, CloudDriftDepthCents,
    NoiseLevelDb, NoiseWakeBase, NoiseWanderRate,
    ResonanceGravity, ResonanceMix, ResonanceWanderRate,
    EcologyMix, EcologyLoopGain,
    BodyBlend, BodyDamping, BodyResonance, BodyMix,
    EcosystemDepth, EventRateScale, BloomDepth, BloomSpawnRateHz,
    BreathingDepth, BreathingIrregularity, TidalDepth,
    // -- Engine-owned ------------------------------------------------------
    SubToneLevelOffsetDb, SubTrackingAmount,
    SmearAmount, SmearDecoherence, SmearTilt,
    GhostPeakLevel, AtmosBlur, OutputSaturation,
    // -- Cavern-owned (each MUST have a 1:1 VoragoCavernTargets field) -----
    CavernSize, CavernDarkness, CavernDecaySeconds, CavernFog,
    CavernDamperDepth, CavernMix, CavernWidth,
    Count
};
```

### S7.2 The two PODs

```cpp
/// FR-063. The Cavern-owned rows as plain floats - no Layer 4 type is named (AR-1).
/// Every default is the CavernVerb shipped literal, DUPLICATED with its source line, because
/// CavernVerb's constants are reachable but the TYPE is not nameable from Layer 3 - the
/// SeraphisAetherTargets construction (seraphis_macro_matrix.h:105-133).
/// Fields are declared IN ENUMERATOR ORDER, so the field index is a pure offset.
/// A DRIFTED LITERAL IS INVISIBLE TO A LITERAL-VS-LITERAL COMPARISON, which is why SC-009
/// clause 1 also checks these against the real defaults in the Layer-4-aware TU (S10.4).
struct VoragoCavernTargets {
    float size          = 0.50f;   // cavern_verb.h:253 kDefaultSize
    float darkness      = 0.80f;   // :254 kDefaultDarkness
    float decaySeconds  = 20.0f;   // :255 kDefaultDecaySeconds
    float fog           = 0.30f;   // :259 kDefaultFog
    float damperDepth   = 0.35f;   // :247 kDefaultDamperDepth
    float mix           = 1.00f;   // :264 kDefaultMix
    float width         = 1.00f;   // :263 kDefaultWidth
};

/// FR-061's documented neutrals. Gravity is bipolar around 0.5; the other eleven are 0.
struct VoragoMacroValues {
    float darkness = 0.0f, age = 0.0f, density = 0.0f, movement = 0.0f;
    float gravity  = 0.5f;                       // BIPOLAR: 0 = air, 0.5 = neutral, 1 = stone
    float entropy = 0.0f, pressure = 0.0f, weight = 0.0f, fog = 0.0f;
    float life = 0.0f, depth = 0.0f, mass = 0.0f;
};

struct VoragoMacroRow {
    VoragoMacro macro;
    VoragoMacroTargetOwner owner;
    VoragoMacroTarget target;
    /// FR-064: for a Voice- or Engine-owned row this is THE FR-090 PREPARE-TIME VALUE the voice
    /// or engine actually installs on that target - cited to its S8 row - NOT the owning
    /// component's own shipped default. Only Cavern rows use a component default (FR-063),
    /// because nothing in this phase prepares a CavernVerb. Rows sharing a target MUST agree on
    /// `base`; asserted below the class.
    float base;
    float amount;        ///< SIGNED; implementation tuning.
    ModCurve curve;      ///< Linear | Exponential | SCurve ONLY (FR-065).
};
```

FR-064's argument, restated in the header because it is the one thing a reader will want to
re-litigate: FR-017 sets atmosphere density to 0.30 over the component's shipped 4.0
(`atmosphere_engine.h:821-828`), so a row basing on the component default would have `apply()` **write
4.0 back at the neutral**, destroying the ghost configuration on the first block and falsifying FR-066
and SC-009 by construction. `seraphis_macro_matrix.h:160-163` defines `base` the same way for the same
reason.

### S7.3 `kRows` — the table (FR-060, FR-068)

`static constexpr std::array<VoragoMacroRow, kNumRows> kRows`, authored in macro order with a
direction comment above each row. Every `base` is cited to its S8 row; every `amount` is signed so the
row's direction is readable at the literal.

| Macro | Owner | Target | base (S8) | amount | curve | Direction, and which FR-068 clause it carries |
|---|---|---|---:|---:|---|---|
| Darkness | Voice | `CloudSpectralTiltDb` | −4.0 | **−6.0** | Linear | tilt darkens (centroid ↓, SC-008 row 1) |
| Darkness | Cavern | `CavernDarkness` | 0.80 | **+0.20** | SCurve | the space absorbs HF |
| Darkness | Engine | `SmearTilt` | 0.0 | **−0.5** | Linear | the smear leans low |
| Age | Voice | `BodyDamping` | 0.25 | **+0.55** | Linear | **carries `Decay`'s shortening half** |
| Age | Cavern | `CavernDecaySeconds` | 20.0 | **−14.0** | Linear | **`Decay` shortening half** (SC-008 fold clause) |
| Age | Voice | `CloudSpectralTiltDb` | −4.0 | **−4.0** | Linear | HF loss (SC-008 row 2: > 4 kHz energy ↓ ≥ 3 dB) |
| Density | Voice | `CloudRichness` | 0.70 | **+0.28** | Linear | active partial count ↑ |
| Density | Voice | `NoiseWakeBase` | 0.35 | **+0.65** | SCurve | awake noise-source count ↑ |
| Density | Voice | `NoiseLevelDb` | −18.0 | **+6.0** | Linear | the bed thickens with the wake (base = S8.2's `noise_.setSourceLevel(all)`) |
| Density | Voice | `BloomDepth` | 0.60 | **+0.40** | Linear | more children survive |
| Movement | Voice | `CloudDriftDepthCents` | 8.0 | **+22.0** | Linear | per-band total variation ↑ |
| Movement | Voice | `ResonanceWanderRate` | 0.03 | **+0.12** | Exponential | peaks wander faster |
| Movement | Voice | `NoiseWanderRate` | 0.03 | **+0.12** | Exponential | filters wander faster |
| Movement | Voice | `BreathingDepth` | 0.30 | **+0.40** | Linear | FR-026's gravity lane swings wider (base = S8.2's `breath_.setDepth`) |
| Movement | Cavern | `CavernDamperDepth` | 0.35 | **+0.45** | Linear | the dampers move |
| **Gravity** | Voice | `ResonanceGravity` | 0.0 | **+1.0** | Linear | **bipolar**: air ↔ stone (FR-016, Q4) |
| Entropy | Voice | `CloudMutation` | 0.15 | **+0.55** | Linear | **carries `Instability`** |
| Entropy | Voice | `CloudInharmonicity` | 0.015 | **+0.055** | Linear | spectral flatness ↑ |
| Entropy | Engine | `SmearDecoherence` | 0.20 | **+0.60** | SCurve | phase decoherence ↑ |
| Entropy | Voice | `BreathingIrregularity` | 0.30 | **+0.60** | Linear | **`Instability`**: life-mod depth ↑ |
| Entropy | Voice | `CloudDriftDepthCents` | 8.0 | **+12.0** | Linear | **`Instability`** (shares Movement's target and base) |
| Pressure | Voice | `EcologyMix` | 0.15 | **+0.35** | SCurve | crest factor ↓ |
| Pressure | Voice | `EcologyLoopGain` | 0.72 | **+0.16** | Linear | ceiling is `kMaxLoopGain` 0.90 |
| Pressure | Engine | `OutputSaturation` | 0.12 | **+0.35** | Linear | the glue closes |
| Weight | Engine | `SubToneLevelOffsetDb` | 0.0 | **+9.0** | Linear | **roadmap 465**: sub levels ↑ |
| Weight | Voice | `BodyBlend` | 0.35 | **+0.45** | SCurve | **roadmap 465**: toward the heavier body (Q1) |
| Weight | Voice | `BodyDamping` | 0.25 | **+0.25** | Linear | **roadmap 465** (shares Age's target and base) |
| Weight | Voice | `CloudSpectralTiltDb` | −4.0 | **−4.0** | Linear | **roadmap 465**: tilt darkening |
| Fog | Engine | `SmearAmount` | 0.20 | **+0.70** | SCurve | **roadmap 467**: smear ↑ |
| Fog | **Engine** | `GhostPeakLevel` | 0.60 | **+0.60** | Linear | **roadmap 467**: ghost mix ↑ |
| Fog | Engine | `AtmosBlur` | 0.85 | **+0.15** | Linear | blur to the ceiling |
| Fog | Cavern | `CavernFog` | 0.30 | **+0.55** | SCurve | **`Distance`**: distance filtering ↑ (Q1) |
| Fog | Cavern | `CavernDarkness` | 0.80 | **+0.15** | Linear | **`Distance`** (shares Darkness's target/base) |
| Fog | Voice | `CloudSpectralTiltDb` | −4.0 | **−3.0** | Linear | **`Distance`**, voice-side (Q1) |
| Fog | Voice | `TidalDepth` | 0.40 | **+0.40** | Linear | FR-026's fog lane rolls in harder (base = S8.2's `tide_.setDepth`) |
| Life | Voice | `EcosystemDepth` | 0.50 | **+0.50** | Linear | **roadmap 466**: ecosystem activity ↑ |
| Life | Voice | `EventRateScale` | 1.0 | **−0.65** | Linear | **roadmap 466**: shorter intervals = event rate ↑ |
| Life | Voice | `BloomSpawnRateHz` | 0.0042 | **+0.0208** | Exponential | **roadmap 466**: bloom probability ↑ (ceiling `kMaxSpawnRateHz` 0.05) |
| Depth | Cavern | `CavernMix` | 1.00 | **0.0** | Linear | already fully wet; claimed, unmoved — and SC-008's `Depth` metric is amended to match (A-1 below) |
| Depth | Cavern | `CavernSize` | 0.50 | **+0.45** | SCurve | a larger space (SC-008 fold clause) |
| Depth | Cavern | `CavernDecaySeconds` | 20.0 | **+25.0** | Linear | **carries `Decay`'s lengthening half** |
| Depth | Cavern | `CavernWidth` | 1.00 | **0.0** | Linear | claimed, unmoved — see the note below |
| Mass | Voice | `BodyResonance` | 0.70 | **+0.28** | Linear | modal-band energy ↑ (SC-008 row 12) |
| Mass | Voice | `BodyMix` | 1.00 | **0.0** | Linear | already fully wet; claimed, unmoved |
| Mass | Voice | `ResonanceMix` | 0.45 | **+0.30** | SCurve | the resonant network carries more of the bus |
| Mass | **Engine** | `SubTrackingAmount` | 0.60 | **+0.30** | Linear | the subs follow the body |

`kNumRows` = the number of rows above; the exact figure is fixed when the table is typed and the
`static_assert(kRows.size() == kNumRows, …)` is what keeps it honest.

**Two rows changed owner, and one row is gone. Each was a defect, and each is recorded here rather
than silently fixed:**

* **`Fog → GhostPeakLevel` and `Mass → SubTrackingAmount` are `Engine`-owned, not `Voice`-owned.**
  Both targets are declared inside S7.1's `// -- Engine-owned --` block, and
  `everyRowOwnerIsValid(kRows)` is specified (S7.4) to rule out "a row whose `owner` disagrees with
  the block its `target` sits in" — so the earlier `Voice` + "(*Engine-owned*)" footnote was a table
  that fails its own `static_assert` at compile time, and, taken at face value instead, would have
  routed both writes to `VoragoVoice` forwarders that do not exist. Only the owner column moved:
  `GhostPeakLevel`'s base 0.60 and `SubTrackingAmount`'s base 0.60 already match S8.3's engine
  defaults.
* **`Mass → BodyBlend` (+0.25) is deleted.** SC-008's `Mass` metric is "energy within ±1 semitone of
  each of **body A's** first eight mode frequencies … to broadband energy … higher ≥ 4 dB", and body A
  is `StoneChamber` while body B is `SteelTank` (S8.2) — entirely different mode sets. A row that
  raised the blend from 0.35 toward B therefore **attenuated the exact band the criterion
  integrates**, directly opposing the `BodyResonance +0.28` row on the same macro: the case could not
  reliably detect `Mass` failing and could fail a correct implementation. Of the three available
  fixes — blend-weight the metric, reverse the row's sign, or drop the row — the plan drops the row,
  because (i) `Mass` is **not** one of FR-068's six normative mappings (those are `Weight`, `Life`,
  `Fog`, `Age`, `Depth`, `Entropy`), so nothing normative requires `Mass` to touch the blend;
  (ii) leaving blend travel to `Weight` alone keeps one macro in charge of one axis; and (iii) it
  changes no threshold. `Mass`'s remaining rows all move the metric the same way: `BodyResonance`
  sharpens **both** bodies' modal peaks (the setter fans out, S2.4) and at blend 0.35 body A carries
  65 % of the sum, and `ResonanceMix` puts more of the bus through the resonant network that feeds
  them. The ≥ 4 dB endpoint threshold stands unchanged.
* **Three enumerated targets had no row at all** — `NoiseLevelDb`, `BreathingDepth` and `TidalDepth`,
  all in S7.1's Voice block — so `everyTargetIsClaimed(kRows)` would have failed at compile time
  while the table went to the trouble of adding three `amount = 0.0f` claim rows for the Cavern
  targets. Each now has a real, directional row (`Density`, `Movement`, `Fog` respectively), with its
  base cited to S8.2, rather than a claim row: all three are parameters a macro has an honest reason
  to move, and a `0.0` claim would have been the weaker answer. **The full enum is re-audited against
  the table once these three edits land** — that audit is the `everyTargetIsClaimed` `static_assert`,
  and it runs at compile time on the first build.

**Three rows carry `amount = 0.0f`** (`CavernMix`, `CavernWidth`, `BodyMix`). They exist because
`everyTargetInFr061to065IsPresent`'s Vorago analogue requires **every** enumerated target to be claimed
by at least one row, and because `VoragoCavernTargets` needs a 1:1 field for every Cavern enumerator
(FR-063). Each such row carries a comment saying it is a *claim, not a movement*, and why the target is
already at its useful extreme. This is deliberate and visible rather than an enumerator quietly missing
from the table — which is exactly the failure the predicate exists to catch.

### S7.4 Evaluation, the two application surfaces, and the predicates

`contributionOf(row)` and `evaluateAll()` are `seraphis_macro_matrix.h:929-957` verbatim, with `Gravity`
as the one bipolar macro (FR-064a):

```cpp
// unipolar (neutral 0):  contribution = amount * applyModCurve(curve, m)
// bipolar  Gravity:      g = (m - 0.5f) * 2.0f;  sign = (g < 0) ? -1 : +1;
//                        contribution = amount * applyModCurve(curve, fabs(g)) * sign
```

and **no `if (neutral) return;` fast path**: `applyModCurve(c, 0) == 0` for all three permitted curves
and `g == 0` at `m = 0.5`, so `acc == base` at every neutral follows from the arithmetic, not from a
shortcut a mis-signed row could hide behind (FR-064a, `seraphis_macro_matrix.h:940-945`).

`void apply(VoragoEngine&) const noexcept` evaluates once and writes the Voice-owned rows through
`VoragoVoice`'s forwarders for `i < engine.getPolyphony()` and the Engine-owned rows through the
engine's own setters. `Cavern`-owned rows are **never written by `apply()`** (FR-062) — the header may
not name a Layer 4 type — and are returned instead by
`[[nodiscard]] VoragoCavernTargets computeCavernTargets() const noexcept`, which carries the **raw
sum**: range clamping for those seven belongs to the Layer-4 setter the caller pushes into, which is
also the only place that knows the reverb's documented ranges (`seraphis_macro_matrix.h:814-820`).

**FR-067's idempotence is a property of the forwarders, and it is stated here because nothing else in
the plan states it.** Every Voice- and Engine-owned forwarder `apply()` writes through is **a plain
scalar store or a `setTarget`** on a ramp the owning component already runs — **never a `snapTo`,
never a smoother reset, never a re-arm of a ramp that is already at its destination**. The matrix adds
no smoother of its own (FR-067), so calling `apply()` every block with unchanged macro values must
step nothing. SC-010 cannot see a violation of this: it measures a zipper *during a ramp*, where a
re-armed ramp looks identical to a correct one, and SC-009 clause 3 covers only the neutral. The case
that can see it is `VoragoMacro_ApplyIsIdempotent` (S10.3a), which renders the same non-neutral macro
vector twice — once with `apply()` called once before the render, once with it called at every
control chunk — and requires the two renders to be **bit-identical**.

`setMacro(VoragoMacro, float)` substitutes a non-finite argument with **that macro's neutral**
(`neutralFor`), clamps to `[0, 1]`, and treats `Count` / out-of-range as a silent no-op; `getMacro` on
an out-of-range enumerator returns that macro's neutral (FR-069, SC-028). `setMacros`/`getMacros`
round-trip through the same path so a bulk write cannot bypass the clamp.
**`setTargetBase` / `resetTargetBases` / `getTargetBase` are NOT provided** — Phase 12's surface
(FR-069), and their absence is what keeps `everyRowSharesOneBasePerTarget` a compile-time guarantee and
SC-009 clause 1 unconditional.

Five `constexpr` predicates, each with a `static_assert` at namespace scope below the class (a
member-specification `static_assert` cannot call a member whose body has not been parsed yet):

| Predicate | What it rules out |
|---|---|
| `kRows.size() == kNumRows` | the table and the constant drifting apart |
| `everyRowOwnerIsValid(kRows)` | a row whose `owner` disagrees with the block its `target` sits in |
| `everyCavernRowHasAPodField(kRows)` | a Cavern target with no `VoragoCavernTargets` field (index ≥ 7) |
| `noRowUsesSteppedCurve(kRows)` | `ModCurve::Stepped`, which breaks SC-010's continuity bound by construction (FR-065) |
| `everyTargetIsClaimed(kRows)` | an enumerated target no row writes — the "fold that vanishes in the build" failure (FR-068) |
| `everyRowSharesOneBasePerTarget(kRows)` | two rows on one target disagreeing about `base` (FR-064) — the one that catches an S8 default and a `kRows` literal drifting apart at **compile** time, before SC-009 catches it at run time |

---

## S8. FR-090 — the complete numeric default table

This table is **normative**. It is reproduced verbatim in `VoragoVoice::prepare()` step 5 and
`VoragoEngine::prepare()`, one line per row including the unchanged ones, so the table *is* the code
(the `seraphis_voice.h:263-333` shape). It is also the **source of `kRows`' `base` column** (FR-064):
a default that changes must change both, in the same commit, and `everyRowSharesOneBasePerTarget` plus
SC-009 clause 1 are the two mechanisms that catch it if it does not.

Three blocks below are **reproduced, never re-derived** (FR-090's own instruction): FR-017's seven
ghost values, FR-061's twelve neutrals, FR-014's three envelope numbers.

### S8.1 `VoragoVoiceConfig` / `VoragoEngineConfig`

| Field | Shipped value | Clamp (owner) | Why |
|---|---:|---|---|
| `voice.maxBlockSamples` | 2048 | `[1, kMaxBlockSamples]` | the shared library maximum |
| `voice.numNoiseSources` | 4 | `[1, NoiseOrganism::kMaxSources]` | roadmap line 390's per-voice counts; **the L-4 lever's first candidate** (S12) |
| `voice.numResonancePeaks` | 12 | `[1, kMaxPeaks]` | ditto |
| `voice.numEcologyLoops` | 6 | `[1, kMaxLoops]` | ditto |
| `voice.ecosystemAgents` | 32 | `[kMinAgents, 48]` | the component's own default (`ecosystem_engine.h:304`) |
| `voice.ecosystemCells` | 64 | `[1, 96]` | component default |
| `voice.ecosystemStepChunks` | 8 | `[8, 64]` | component default and floor; the tuned value (Phase 8 ruling 2) |
| `voice.bloomChildSlots` | 6 | `[0, kMaxChildren]` | **not** the component's 8: B-1's floor is `6 + 8 = 14` active partials, and 8 would raise it to 16 |
| `voice.maxCombDelayMs` | 50.0 | `[5, 200]` | component default |
| `maxBlockSamples` (engine) | 2048 | `[1, 2048]` | — |
| `atmosCaptureSeconds` | **20.0** | `[1, 30]` | FR-091: a ghost grain is 12 s (FR-017); a 4 s or 8 s ring could never hold one |
| `atmosBlurEnabled` | true | — | FR-017's blur 0.85 needs it |
| `atmosFreezeEnabled` | **false** | — | no Phase-10 consumer; off saves the freeze FFT and ~2 MB |
| `atmosBlurFftSize` | 1024 | `[256, 4096]` | component default |
| `atmosFreezeFftSize` | 2048 | `[256, 8192]` | component default (unused while freeze is off) |
| `smearEnabled` | **true** | — | the component defaults to **false** (`spectral_smear.h:141-149`); Fog needs it, and SC-022 (2) needs both states reachable |
| `smearFftSize` | 2048 | `[kMinFftSize, kMaxFftSize]`, bit-floored | `kDefaultFftSize` |

### S8.2 `VoragoVoice::prepare()` step 5 — the per-voice defaults

| Setter | Vorago value | Component default | Why it differs (FR-091) |
|---|---:|---:|---|
| `cloud_.setRichness` | **0.70** | 1.0 (clamp max) | `N(0.70) = round(64^0.70) = 18` partials — dense enough for B-1's 14-slot floor with room for six children, cheap enough for S12 |
| `cloud_.setSpectralTiltDb` | **−4.0** | 0.0 | a drone leans low before any macro moves |
| `cloud_.setMutation` | **0.15** | 0.0 | the component's own zero-travel trap: at 0 the `Entropy` row would have nothing to move away from |
| `cloud_.setInharmonicity` | **0.015** | 0.0 (floor) | same, and stone is not perfectly harmonic |
| `cloud_.setSpectralGravity` | **0.10** | 0.0 | a slight pull; **not** a `Gravity`-macro target (Q4) and driven by no lane |
| `cloud_.setDriftDepthCents` | **8.0** | 0.0 | audible life at the neutral |
| `cloud_.setStereoSpread` | **0.45** | 0.0 | the partials are the width |
| `cloud_.setAttackTimeSec` | 0.05 | 0.05 (floor) | (unchanged) — the voice envelope owns the shape |
| `cloud_.setDecayTimeSec` | **8.0** | 0.5 | per-partial tails at drone scale; clamp `[0.05, 60]` |
| `noise_.setNumSources` | 4 | config | from `VoragoVoiceConfig` |
| `noise_.setSourceModel(0..3)` | `FilteredWind`, `GranularDust`, `Direct`, `MetallicHiss` | `Direct` | four different textures, so `Density` has something to wake into |
| `noise_.setSourceLevel(all)` | **−18.0 dB** | −12.0 | the noise is a bed, not a layer; clamp `[−96, +12]` |
| `noise_.setWanderRate` | 0.03 | 0.03 | (unchanged) |
| `noise_` wake base (voice-side) | **0.35** | — | FR-021's per-destination base; `Density`'s row moves it |
| `resonance_.setAnchorMode` | **`Hybrid`** | `Free` | the only mode that consumes `setGravity` (`:1412-1437`), FR-016 / Q4 |
| `resonance_.setNumPeaks` | 12 | config | — |
| `resonance_.setGravity` | **0.0** | 0.0 | (unchanged) — the base FR-016's two lanes sum onto, and `kRows`' `Gravity` base |
| `resonance_.setMix` | **0.45** | — | the network carries about half the bus |
| `resonance_.setWetGain` | 34.5 dB | 34.5 dB | (unchanged) `kDefaultWetGainDb` |
| `resonance_.setPeakLevel(all)` | **−9.0 dB** | — | clamp `[−60, +12]` |
| `resonance_.setFreqWander(all)` | **1.5 semitones** | — | slow, audible peak drift |
| `resonance_.setWanderRate` | 0.03 | 0.03 | (unchanged); `Movement`'s row moves it |
| `resonance_` peak wake base | **0.50** | — | half the peaks awake at the neutral |
| `ecology_.setNumLoops` | 6 | config | — |
| `ecology_.setMix` | **0.15** | 0.15 | (unchanged) `kDefaultMix`; `Pressure`'s row moves it |
| `ecology_.setLoopGain(all)` | 0.72 | 0.72 | (unchanged) `kDefaultLoopGain` |
| `ecology_.setCoupling` | **0.12** on the neighbour ring | 0.04 | Phase 5 measured cross-loop interaction at ≈ −84 dB at the default voicing (roadmap lines 276–278); **3× the default coupling is Phase 10's answer**, and if it is still inaudible the header records that rather than pretending otherwise |
| `ecology_` loop wake base | **0.50** | — | — |
| `bloom_.setDepth` | **0.60** | 1.0 | `Density`'s row moves it up; the ecosystem's `Partial` agents add to it |
| `bloom_.setSpawnRateHz` | **1/240 = 0.0042** | 1/240 | (unchanged); `Life`'s row moves it toward `kMaxSpawnRateHz` 0.05 |
| `bloom_.setFadeIn/Hold/FadeOut` | 45 / 120 / 180 s | same | (unchanged) — the component's drone-scale lifecycle |
| `bloom_.setConsumerTiltDb` | **−4.0** | 0.0 | FR-012: mirrors the cloud's tilt, on every step either changes |
| `ecosystem_` depth (voice-side) | **0.50** | — | FR-021's routing depth; `Life`'s row moves it to 1.0 |
| `bodies_[0].setMaterial` | **`StoneChamber`** | `Glass` | body A is the room |
| `bodies_[1].setMaterial` | **`SteelTank`** | `Glass` | body B is the object; the pair spans the darkness axis without being the two extremes |
| `bodies_[*].setResonance` | 0.70 | 0.70 | (unchanged) |
| `bodies_[*].setDamping` | **0.25** | 0.0 (floor) | the zero-travel fix Seraphis took for the same reason (`seraphis_voice.h:296`); `Age`/`Weight` move it up |
| `bodies_[*].setMix` | 1.00 | 1.00 | (unchanged) — fully wet: the excitation reaches the output only through the resonators |
| `bodies_[*].setCloudMix` | 0.25 | 0.25 | (unchanged) |
| `bodies_[*].setCloudDecaySec` | **20.0** | 4.0 | drone scale; clamp `[0.1, 30]` |
| `bodies_[*].setWidth` | 1.00 | 1.00 | (unchanged) |
| `setBodyBlend` | **0.35** | — | biased toward body A (the room); `Weight`/`Mass` push toward B |
| `sched_[0].setIntervalRange` | **20–90 s** | — | FR-022's fast scheduler (Q7) |
| `sched_[1].setIntervalRange` | **180–600 s** | — | FR-022's slow scheduler, 3–10 min (Q7); 600 s is `kMaxIntervalSeconds` |
| `sched_[*].setEnvelopeTimes` | **8 / 20 / 30 s** | — | a wake is a swell, not a trigger |
| `sched_[*].setDepthRange` | **0.4 – 1.0** | — | FR-022 (Q7) |
| `sched_[*].setBipolarProbability` | **0.0** | — | FR-022: every event is a positive wake; sleep events are out of scope |
| `sched_[*].setTargetCount` | **5** | — | the five destination families of S3.6 (c) |
| `breath_.setRate` | **0.017 Hz** | — | ≈ one breath per minute |
| `breath_.setDepth` | **0.30** | — | the `Gravity` lane's swing (FR-026) |
| `breath_.setIrregularity` | **0.30** | — | `Entropy`'s row moves it |
| `tide_.setRate` | **0.25** (normalised) | — | a tide inside the 30 s–10 min range |
| `tide_.setDepth` | **0.40** | — | `Fog`'s fold reads it through `getTidalFogDepth()` |
| `growth_.setDuration` | **120 s** | 10 s | `Growth` mode is a two-minute swell, not a ten-second one |
| envelope | attack **20 s**; stages **0.80/30 s, 0.92/45 s, 0.85/60 s**; sustain hold 0.85; release **45 s** | — | **FR-014, reproduced not re-derived** (S3.8) |
| `mse_.setRetriggerMode` | `Legato` | `Hard` | `Hard` would restart a 20 s attack on every re-articulation |

### S8.3 `VoragoEngine::prepare()` — the global defaults

| Setter | Vorago value | Component default | Why |
|---|---:|---:|---|
| `setPolyphony` | **4** (`kDefaultPolyphony`) | — | S12's recommendation; `kMaxVoices` 8 is the ceiling |
| **`atmos_.setDensity`** | **0.30** grains/s | 4.0 | **FR-017, reproduced**: a ghost is an event, not a wash (~1 per 3.3 s) |
| **`atmos_.setGrainSeconds`** | **12.0** | 4.0 | **FR-017**: a grain is a memory of the drone |
| **`atmos_.setPitchSemitones`** | **−12.0** | 0.0 | **FR-017**: ghosts sit an octave under |
| **`atmos_.setPositionSpread`** | **0.90** | 0.3 | **FR-017**: read ages scatter across the capture |
| **`atmos_.setBlur`** | **0.85** | 0.0 | **FR-017**: roadmap line 114's darker blur defaults |
| **`atmos_.setDecorrelation`** | **0.85** | 0.5 | **FR-017**: wide, unlocalised |
| **`atmos_.setLevel`** | **event-driven; base 0.0, peak `kGhostBurstPeak` 0.60** | 1.0 | **FR-017**: a base of 0 is what makes a burst a burst; written every control step from the max across voices of `getGhostRequest()` (S6.6 step 3) |
| `sub_.setToneLevelDb(0..2)` | −18 / −24 / −30 dB | same | (unchanged) `kDefaultToneLevelDb`; `Weight`'s row adds a shared offset |
| `sub_.setTrackingAmount` | **0.60** | — | the subs follow the ensemble but do not disappear when it thins |
| `sub_.setFundamentalHz` | the lowest sounding voice, **held** when none | — | FR-051, SC-031 |
| `smear_.setSmearAmount` | **0.20** | 0.0 | audible fog at the neutral; `Fog`'s row takes it to 0.90 |
| `smear_.setDecoherence` | **0.20** | 0.0 | `Entropy`'s row moves it |
| `smear_.setSmearTilt` | **0.0** | 0.0 | (unchanged); `Darkness`'s row moves it |
| `satL_/satR_.setDrive` | **0.0 dB** (`kOutputDriveDb`) | — | roadmap line 462's "low drive" |
| `satL_/satR_.setSaturation` | **0.12** (`kOutputSaturation`) | — | glue, not an effect; `Pressure`'s row moves it |
| `limiter_.setCeilingDb` | **−0.3 dB** | — | FR-073's `\|out\| <= 1.0` with true-peak headroom |
| `sumGain_` | snapped to `1/sqrt(polyphony)` at `prepare()` | — | so no test that sets polyphony before prepare sees a ramp |

### S8.4 The twelve macro neutrals (FR-061, reproduced)

`Gravity` = **0.5** (bipolar: 0 = air, 0.5 = neutral, 1 = stone). Every other macro — `Darkness`, `Age`,
`Density`, `Movement`, `Entropy`, `Pressure`, `Weight`, `Fog`, `Life`, `Depth`, `Mass` — = **0.0**
(unipolar: 0 = off, 1 = full). These are `VoragoMacroValues`' member initialisers, so a
default-constructed matrix is already at the FR-066 identity.

---

## S9. RT safety, denormals, portability

**Allocation.** `prepare()` is the only allocating path on all three classes (FR-003, FR-042). Every
member is a fixed `std::array` or a sub-component that allocates only in its own `prepare()`; there is
no `std::vector`, `std::function`, smart pointer or `std::string` anywhere in the three headers, and
SC-014's `AllocationScope` arm is the gate. `getAllocatedBytes()` sums the sub-components that publish
one and **says in its doxygen** that `HarmonicCloud`, `ContinuousBody`, `MultiStageEnvelope` and
`AtmosphereEngine` do not, so it is a partial accounting — the complete check is the detector.

**Locks, exceptions, I/O.** None on any path. **Every method is `noexcept`, `prepare()` included** —
which is the library convention, not an oversight, and S2.4 and S6.4 already declare it that way
(`void prepare(double, const VoragoVoiceConfig&) noexcept`). The precedent is explicit:
`TruePeakLimiter::prepare` allocates and is `noexcept`, with the doxygen "NOT real-time safe
(allocates)" one line above it (`true_peak_limiter.h:57-59`). `noexcept` is a statement about
exceptions; the real-time statement is the table below, and conflating the two is what let the earlier
wording here read as a safety claim. The `assert` in `noteOn` (the RA-4 "the allocator allocated the
slot we freed" check) is debug-only and is the `seraphis_engine.h:508-515` precedent.

**Not-audio-thread operations, stated rather than assumed — and the RT-safe trio that serves the
audio-thread paths instead (B-7).**

| NOT on the audio thread | Why |
|---|---|
| `VoragoVoice::prepare()`, `VoragoEngine::prepare()` | the only allocating paths (FR-003, FR-042) |
| **`VoragoVoice::reset()`** | B-7: it is the one voice path that reaches `FeedbackEcology::reset()`'s O(buffer) wipe — ~786 KB of `std::fill` at 48 kHz, ~3.1 MB at 192 kHz (`feedback_ecology.h:861-862`, `:2401-2412`) |
| `VoragoEngine::reset()`, `VoragoEngine::silence()` | the row above × `kMaxVoices`, plus a `std::fill` over the atmosphere's whole 20 s capture ring (S6.7) |

| RT-SAFE — and these are the only clearing calls an audio-thread path may reach | Caller |
|---|---|
| `VoragoVoice::silence()` | the steal fade (S6.7) |
| `VoragoVoice::resetForSteal()` | the steal teardown (S6.7) |
| `VoragoVoice::resetForRecovery()` | FR-072's deferred recovery (S6.6 step 2) |

All three run `clearRunState(true)`, whose ecology arm is `FeedbackEcology::silenceAudio()`'s six O(1)
`clearLoopAudio` calls and never `reset()` (B-7). `kResetsPerControlChunk` sits on top of **that** cost,
not `reset()`'s, so a poisoned block cannot pull `kMaxVoices` clears into one 1.33 ms chunk — which is
what S6.2's corrected rationale now says.

**Denormals.** Every sub-component already flushes its own (the `detail::flushDenormal` idiom is
pervasive: `bloom_engine.h`'s child amplitudes, `ecosystem_engine.h:250`'s cell guard). The three new
classes add exactly two arithmetic sites of their own — the blend mix and the noise all-pass pair — and
both are multiply-adds on signals that are already flushed upstream. `dsp_test_main.cpp` calls
`enableFTZDAZ()` before any case runs, so every measured figure is taken in the same FP environment
the audio thread runs in.

**Non-finite detection is always bit-pattern.** `detail::isFinite` (`core/db_utils.h:118`) via a
private `isFiniteBits` static in each class — never `std::isnan` / `std::isinf` / `std::isfinite`,
which the macOS leg's `-ffast-math` licenses the compiler to fold away (roadmap line 570;
`seraphis_voice.h:919`, `seraphis_engine.h:1092`). Tests build their non-finite inputs from **bit
patterns through a volatile**, never from `std::numeric_limits`, for the same reason
(`noise_organism_perf_test.cpp:148-150`). `node tools/lint-nonfinite-symbols.js` is the mechanical
check.

**Narrowing.** Designated initialisers everywhere a config or a table row is constructed — no
positional brace init in the headers or the tests. Clang errors on narrowing where MSVC does not, which
makes a positional init a Windows-green / CI-red construct (`ecosystem_engine.h:290-301`). Every float
literal carries an `f` suffix (C4244), every `size_t` → `int` conversion is an explicit cast (C4267),
every unused parameter is `[[maybe_unused]]` (C4100).

**SIMD.** This phase introduces none. No aligned load or store is added, so the aligned-load lint has
nothing new to see (FR-075).

**Layers.** `vorago_voice.h` and `vorago_engine.h` include Layers 0–2 and Layer 3 peers only;
`vorago_macro_matrix.h` includes `vorago_engine.h` and nothing from `effects/`. `node
tools/lint-layers.js` and `node tools/lint-odr.js` are run before the commit, together with
`node tools/check-portability.js` (FR-075, SC-024) — a green MSVC build proves nothing for the Linux
and macOS legs.

**libstdc++ syntax check.** The three new headers, the two appended-to headers and the **nine** new TUs are compiled once under
`g++ -std=c++20 -fsyntax-only` (WSL) before the commit — the Phase 3 precedent (roadmap lines 220–222)
and the one cheap gate that catches the `std::` include the MSVC STL provides transitively and libstdc++
does not.

---

## S10. Test plan

### S10.1 TU assignment (FR-084, FR-084a)

Nine new TUs. **Eight register with `dsp_systems_tests`** in the enumerated list at
`dsp/tests/CMakeLists.txt:324` and include **no** `effects/` header; **one** registers with
`dsp_effects_tests` beside `CavernVerb`'s own tests (FR-084a, OQ3). A TU that is not in the list
silently drops out of the build and its cases never run — the rule the Vorago Phase 2/3 blocks state in
that file itself.

| TU | Target | Criteria it carries |
|---|---|---|
| `unit/systems/vorago_voice_test.cpp` | systems | SC-007 (voice), SC-017, SC-017a, SC-018a, SC-019a, SC-019b, SC-020a, SC-023 (voice), SC-028 (voice) + the FR-gated cases of **S10.3a** (which include FR-026's `VoragoVoice_LifeModulatorLanes` and B-7's `VoragoVoice_SilenceClearsEcologyAudio`) |
| `unit/systems/vorago_voice_longrun_test.cpp` | systems | SC-018, SC-019, SC-020 — the `[long]` voice set |
| `unit/systems/vorago_engine_test.cpp` | systems | SC-004a, SC-006, SC-007 (engine), SC-011, SC-012, SC-013a, SC-014, SC-021a, SC-022, SC-023 (engine), SC-026, SC-027, SC-028 (engine), SC-030, SC-031 |
| `unit/systems/vorago_engine_longrun_test.cpp` | systems | SC-004b, SC-005, SC-013b, SC-021b — the `[long]` engine set |
| `unit/systems/vorago_macro_test.cpp` | systems | SC-008 (eleven macros), SC-009, SC-010, **SC-023 (matrix)**, SC-028 (matrix) + FR-067's `VoragoMacro_ApplyIsIdempotent` (S10.3a) |
| `unit/systems/vorago_dark_materials_test.cpp` | systems | SC-015, SC-016 clauses 3–4 |
| `unit/systems/vorago_nonfinite_test.cpp` | systems | SC-029 + the `detail::VoragoEngineNonFiniteProbe` definition (B-4) |
| `unit/systems/vorago_perf_test.cpp` | systems | SC-001a, SC-001b, SC-002, SC-003 + B-7's `VoragoVoice_ClearingPathCost` — all `[.perf]` |
| `unit/effects/vorago_composed_chain_test.cpp` | **effects** | the composed `engine → CavernVerb → processOutputStage` chain: FR-073's bound on the real chain, SC-008's **`Depth`** row, SC-009's Cavern-literal differential |

Three **existing** TUs are edited rather than added (FR-038): `continuous_body_perf_test.cpp` (sites
1–4, and SC-025's 44-measurement table), `continuous_body_test.cpp` (site 5), `seraphis_perf_test.cpp`
(sites 6–7). SC-016 clause 1's `git diff --numstat --diff-filter=M dsp/tests/unit/systems/` must name
**exactly those three** and nothing else.

### S10.2 Shared test code — `tests/test_helpers/vorago_fixtures.h`

`tests/test_helpers` is an INTERFACE target exposing the whole directory
(`tests/test_helpers/CMakeLists.txt:7-12`), so a new header needs **no CMake edit** and is reachable
from both executables — which matters, because the composed TU lives in the other one.

It holds, and nothing else:

* **`kFastAttackEnvelopeConfig` (FR-014a)** — the one named fixture: six `{level, ms}` pairs with
  attack 50 ms, every stage time 50 ms, release 100 ms, plus `applyFastAttack(VoragoVoice&)` /
  `applyFastAttack(VoragoEngine&)`. Cited **by name** by SC-021a, SC-008 and SC-022 (2). A comment
  states that SC-004b must not use it (FR-014a). **`applyFastAttack(VoragoEngine&)` is implementable
  without a `const_cast` only because S6.4 exposes the four envelope fan-out forwarders** — the
  engine's sole mutable route to its voices, since `getVoice(i)` is const and the Voice-owned macro
  rows travel through `friend class VoragoMacroMatrix`. The same four forwarders are what makes
  SC-013a's "every macro, **every exposed setter**" fuzz enumerable on the engine.
* **`makeEngine(double sr, const VoragoEngineConfig&)`** returning a `std::unique_ptr<VoragoEngine>` —
  because the engine is hundreds of KB and MSVC's default main-thread stack is 1 MiB
  (`seraphis_engine.h:201-204`). Every case obtains its engine this way; a stack local is a defect.
* **Analysis helpers** used by more than one TU: `spectralCentroidHz(span, sr)`,
  `bandEnergyDb(span, sr, lo, hi)`, `spectralFlatness(span, sr)`, `crestFactorDb(span)`,
  `perBandTotalVariation(span, sr)`, `perBinMagnitudeFlux(span, sr)`, `blockRmsDb(span, blockLen)`,
  `spearmanRho(const std::array<double,5>& x, const std::array<double,5>& y)`, and
  `maxDeltaInWindow(span, windowSamples)` (the SC-010/SC-011/SC-017a/SC-018a click statistic, one
  implementation shared by all four).
* **`renderEngine(VoragoEngine&, std::vector<float>& l, std::vector<float>& r, std::size_t samples,
  std::size_t blockSize)`** — the standard render loop, so no case rolls its own partition by accident.

It must **not** include `<allocation_operator_overrides.h>`: the single owner in `dsp_systems_tests` is
`unit/systems/selectable_oscillator_test.cpp:388` and in `dsp_effects_tests` it is
`unit/effects/aether_reverb_test.cpp:38`; a second include is a duplicate-symbol link error. Cases that
need allocation detection include `<allocation_detector.h>` only.

### S10.3 Criterion by criterion

Every row names the TU, the `TEST_CASE` name, and the assertion — with its tolerance, its seed and its
threshold. "Fingerprint" means `compareFingerprints` at the shared constants
(`kMetricTolerance = 2.5e-4`, `kSampleTolerance = 5.0e-4`); **no bit-exact float golden is stored
anywhere** (FR-074, roadmap line 568).

| Criterion | TU / `TEST_CASE` | Assertion strategy |
|---|---|---|
| **SC-001a** measure & report | perf / `VoragoEngine_CpuSurvey` `[.perf]` | Best-of-25 × 500 blocks at 512/48 kHz, warm-up 400 blocks, P-core-pinned via `node tools/run-cpu-tests.js`. Polyphony sweep {1, 2, 4, 6, 8} with every sub-component at the S8 defaults, macros at neutral, **and** the composed chain — the `CavernVerb` figure is taken from `specs/vorago-phase9-cavern-space/compliance.md` and **added arithmetically** rather than instantiated, because this TU may not name a Layer 4 type; the composed TU cross-checks the sum once. Prints ns/block, % of one core, the S10.3 SC-002 breakdown, the **measured `L`** — the per-slot life-only cost, read from SC-002's standalone `advanceLifeOnly` arm — and FR-081's ladder recomputed from the measured `V`, `L` **and** `G` against the corrected solve `N·V + (kMaxVoices − N)·L + G ≤ budget` (S12.2, B-6). `kMaxVoices` is a **compile-time constant and cannot be swept at run time**, so its cost is evaluated arithmetically from the measured `L` and the ladder is printed for `kMaxVoices ∈ {4, 6, 8}` at every swept polyphony — that table is the artefact Q-A's `kMaxVoices` half is ruled from. Assertions limited to finite-and-positive; everything else `WARN`. **Gates nothing.** |
| **SC-001b** the gate | perf / `VoragoEngine_CpuBudget` `[.perf]` | **Written only after the OQ-1(a) ruling is recorded in the spec as an amendment** (FR-083). At the ruled polyphony and lever set: measured ≤ `kReferenceNs` (3 200 000) and the checked-in baseline ≤ `kMaxAdmissibleNs` (2 133 333), with the paired `static_assert(kBaseline <= kMaxAdmissibleNs, …)` in the TU. **No baseline may be checked in before the amendment exists.** |
| **SC-002** stage breakdown | perf / `VoragoVoice_StageCostProbe` `[.perf]` | Each stage measured **standalone in the same TU** — cloud, noise+decorrelation, resonance, ecology, bloom, ecosystem, body A, body B, envelope/blend for the voice; voice sum, **atmosphere**, subharmonic, smear, output stage for the engine — **plus one arm that is not a stage: `VoragoVoice::advanceLifeOnly(512)` standalone, which is the `L` of B-6 and S12.2's solve and is what SC-001a's ladder consumes.** *Spec SC-002's own list places `atmosphere` in the **voice** column; that list predates OQ-1 ruling (b), which moved `AtmosphereEngine` to the engine (FR-002, FR-041, FR-056) — the relocation is recorded everywhere else and the engine column above is the correct one. Carried to S14 as amendment A-6 so the comply stage does not read the stale list.* Prints each figure's share of the directly measured whole. Assertions finite-and-positive only; the breakdown is `WARN` (FR-082). No closure bound — SC-003 gates composition once, at 1.15. |
| **SC-003** composition overhead | perf / `VoragoVoice_CompositionOverhead` `[.perf]` | `measured(VoragoVoice) <= 1.15 × sum(standalone sub-components)`, both measured in this TU in the same run so the machine state is shared. |
| **SC-004a** soak sentinel | engine / `VoragoEngine_SoakSentinel` | 60 s full-polyphony render, one held note, 512-sample blocks: every sample finite (bit-pattern), `\|out\| <= 1.0` after `processOutputStage`, `getNonFiniteRecoveryCount() == 0`, `getAllocatedBytes()` identical to the value read immediately after `prepare()`. **Untagged** — this is the cross-platform sentinel and must not ride inside the `[long]` soak. |
| **SC-004b** the 8 h soak | engine_longrun / `VoragoEngine_OvernightSoak` `[long]` | **Unaccelerated** (FR-086: a block-RMS trajectory does not survive clock scaling), full polyphony, one held note, **FR-014's real envelope** — never the fast-attack fixture. `T_settle` = attack (20 s) + stage 0 (30 s) + the longest bloom fade-in (45 s, `kDefaultFadeInSeconds`) + the atmosphere capture fill (20 s, S8.1) = 115 s → **rounded up to 120 s** and stated numerically in the test. In `[0, T_settle)`: block RMS monotone non-decreasing on a 10 s moving average. After: RMS within **[−60, −6] dBFS**, and the last 60 s within **±6 dB** of the 60 s window starting at `T_settle + 300 s`. Throughout: no non-finite sample, `getAllocatedBytes()` unchanged, recovery count 0. |
| **SC-005** non-static | engine_longrun / `VoragoEngine_OvernightEvolution` `[long]` | Same unaccelerated render, **three engine seeds**, measured after `T_settle`. Per seed: centroid sampled every 30 s; CV of that series **≥ 0.05**; autocorrelation at every lag from 60 s to 30 min **< 0.9**. The criterion gates the **mean across the three seeds**; each seed's own value is printed. Averaging the centroid *series* across seeds is forbidden and the test says so in a comment. |
| **SC-006** determinism | engine / `VoragoEngine_DeterminismHarness` | Two engines, same seed/config/note sequence, 60 s: `compareFingerprints` **within tolerance**. Two engines differing **only** in seed: `worstMetricRelativeError > 100 × kMetricTolerance`. Untagged (FR-085). |
| **SC-007** partition invariance | voice / `VoragoVoice_PartitionInvariance`; engine / `VoragoEngine_PartitionInvariance` | 4096 samples rendered as 1 × 4096, 8 × 512, and the pathological `36, 28, 1, 2047, 1984` split — which sums to **exactly 4096**, asserted in the case by a `static_assert` over the split array's sum, because the earlier `…, 984` figure summed to 3 096 and would have made the three arms render different sample counts and the `std::memcmp` "over the whole render" undefined. **Bit-identical** buffers (`std::memcmp` over the whole render) — fingerprint tolerance is deliberately **not** used, because FR-007 demands exactness and both arms are the same build in the same process. Plus **exactly equal** `EcosystemEngine::getControlStepCount()` (via `voice.ecosystem()`) and `getActiveVoiceCount()`. |
| **SC-008** macro sweeps | macro / `VoragoMacro_SweepAxes` `[long]`; the `Depth` row in composed / `VoragoComposed_DepthMacroAxis` `[long]` | Twelve macros × five points {0, 0.25, 0.5, 0.75, 1} × **three engine seeds**, each a 60 s render using **`kFastAttackEnvelopeConfig`**, every other macro at its FR-061 neutral. Per macro: **Spearman rank correlation** of the primary metric against macro value, computed per seed and averaged over the three, **≥ 0.9 in magnitude and of the documented sign**, **and** the endpoint difference meets the row's threshold from the spec's table (centroid ≥ 20 %, HF ≥ 3 dB, count ≥ 50 %, TV ≥ 20 %, gravity ≥ 30 %, flatness ≥ 25 %, crest ≥ 3 dB, sub ≥ 6 dB, flux ≥ 20 %, edges ≥ 2×, reverb share ≥ 6 dB, modal band ≥ 4 dB). All metrics measured on the **engine's own output** over `[10 s, 60 s]` stated in samples. `Gravity`'s five points straddle its 0.5 neutral.<br><br>**Two of the twelve rows are amended, because as the spec states them they cannot pass on a correct implementation (A-1, A-2 in S14):**<br>**(a) `Depth`.** Spec SC-008 measures "reverb-return energy share … as a ratio of the wet path to the total". `CavernVerb`'s `kDefaultMix = 1.00f` (`cavern_verb.h:264`) and `setMix` is documented "CavernVerb owns the mix; the owned engine is permanently fully wet" (`:746-749`), and the `Depth → CavernMix` row carries `amount = 0.0` — so the composed render is 100 % wet at **both** endpoints and the wet/total share is pinned, saturated at both ends before a line is written. The metric is therefore re-stated as an **absolute** measure that moves with size and decay at constant mix: the ratio, in dB, of the composed render's RMS to the RMS of the **same render with `CavernVerb::setMix(0)`**, both over `[10 s, 60 s]`. Because **every** `Depth` row is Cavern-owned, the `setMix(0)` arm is *identical* at both endpoints, so the ratio moves exactly as the reverb return's energy moves — which is what "reverb-return energy share" was reaching for and what `mix = 1.00` made unmeasurable. **The ≥ 6 dB endpoint threshold is carried over unchanged.** If the measured arm misses it, the lever is `CavernSize`'s / `CavernDecaySeconds`' `amount` column (explicitly "implementation tuning", S7.2) or an L-6-style stop-and-surface — **never** the threshold.<br>**(b) `Mass`.** The metric integrates body **A**'s mode bands, so the `Mass → BodyBlend +0.25` row (which moved the blend toward body **B**, a different mode set) opposed it; that row is **deleted** (S7.3), the metric and its ≥ 4 dB threshold stand exactly as the spec writes them, and the remaining `Mass` rows all move the metric the same way. **Plus** the four folded-concept clauses (Age/Depth `decaySeconds` and `CavernSize` read from `computeCavernTargets()`; `ContinuousBody::getDamping()` read through `voice.bodyA()`; `HarmonicCloud::getMutation()` and the life-modulator depths) **and** the non-silence clause: every macro at 0 simultaneously (`Gravity` at 0, its air extreme) → broadband RMS **> −60 dBFS** over `[10 s, 60 s]`. |
| **SC-009** neutrality | macro / `VoragoMacro_NeutralIsIdentity`; clause-1 Cavern half also in composed / `VoragoComposed_CavernDefaultsMatch` | (1) **Per-row base check:** for every `Voice`/`Engine` row, `row.base` equals the value read back from that target's getter on a voice/engine **immediately after `prepare()`**, exactly — this is what catches an S8 default and a `kRows` literal drifting apart. For `Cavern` rows, `row.base` equals the corresponding `VoragoCavernTargets` field default; and in the composed TU, each of those seven literals is additionally compared against the **real** `CavernVerb` constant (`kDefaultSize` … `kDefaultWidth`), because a drifted duplicate is invisible to a literal-vs-literal comparison. (2) **Arithmetic inertness:** at every neutral, `apply()` leaves every writable target at exactly `base` and `computeCavernTargets()` returns exactly the defaults — asserted **per target**, so a mis-signed row cannot hide behind a cancellation. (3) **Render identity:** 10 s render with the matrix never applied vs applied at the neutral → **bit-identical**. Untagged. |
| **SC-010** macro continuity | macro / `VoragoMacro_NoZipper` `[long]` | Each macro automated 0 → 1 over 5 s: `maxDeltaInWindow(20 ms)` on the ramp **≤ 1.5 ×** the same statistic measured 64 ms clear of the ramp (`seraphis_engine.h:232-244`'s shape). |
| **SC-011** steal click-free | engine / `VoragoEngine_StealRamp` | Saturate the pool, steal mid-chunk: no sample-to-sample delta above **1.5 ×** the pre-steal maximum inside the steal window, and the stolen voice is silent within `kSilenceRampMs` — read through `detail::VoragoVoiceSilenceRampProbe` (`fadeRemaining_`, `silenceRampSamples_`) so the ramp is observed, not inferred. `getLastStolenVoiceIndex()` identifies the victim. |
| **SC-012** amnesty policy | engine / `VoragoEngine_StealPolicy` | **Enumerated victim table** driving the three passes of S6.7: (a) an idle slot exists → it is taken, no steal; (b) one `Releasing` slot below −30 dBFS → it is the victim; (c) several `Releasing`, all below → the lowest level wins; (d) all `Releasing` **above** the threshold → the quietest `Releasing` still wins (pass 1, not pass 2); (e) only `Active` slots → the quietest `Active`; (f) exact level tie → the lower `voiceSerial_`. Levels are set by rendering the voices to known amplitudes and read through `getVoiceLevel(i)`. |
| **SC-013a** fuzz sentinel | engine / `VoragoEngine_ConfigurationFuzzSentinel` | **32** seeded random configurations (every macro, every exposed setter, every polyphony in `[1, kMaxVoices]`) × **2 s**, **unaccelerated**: `\|out\| <= 1.0`, no non-finite sample, `getNonFiniteRecoveryCount() == 0`. Untagged — a cross-platform sentinel. |
| **SC-013b** the remainder | engine_longrun / `VoragoEngine_ConfigurationFuzz` `[long]` | The other **968** configurations × 10 s, accelerated per FR-086 with `A` and the wall-clock cost stated in the test. Same three assertions. |
| **SC-014** zero allocation | engine / `VoragoEngine_NoAllocationAfterPrepare` | `AllocationScope` around a 10-minute-equivalent accelerated render that exercises every wake/sleep edge, every bloom spawn/retire, every steal and every macro extreme: **zero** allocations, and `getAllocatedBytes()` identical before and after. Accelerated is legal here — allocation accounting is an event-and-accounting property (FR-086). **FR-070 asks for the invariant "over an 8 h-equivalent accelerated render" while SC-014 says 10-minute-equivalent, and the gap is closed by division of labour, not by narrowing FR-070:** SC-014 is the **per-push** arm and carries the `AllocationScope` detector, which is the stronger instrument and cannot be a per-push cost at 8 h (FR-086 forbids scaling the sample clock, so 8 h-equivalent means 8 h of rendered samples); **SC-004b already asserts `getAllocatedBytes()` unchanged throughout the full unaccelerated 8 h soak**, which is *exactly* the "`getAllocatedBytes()`-invariance pattern … over an 8 h-equivalent render" FR-070's sentence names. Recorded as A-3 in S14: FR-070's clause is carried by SC-004b in the nightly lane and by SC-014 per push; no threshold moves. |
| **SC-015** darker materials | dark_materials / `ContinuousBody_DarkMaterialsSpectral` `[long]` | One `ContinuousBody` per material, identical excitation (a fixed pink-noise burst from a fixed seed) and identical resonance/damping, 20 s steady-state window: each new material's centroid **below the minimum** of the five shipped, and each new material's centroid **≥ 5 % from every other new one**. Prints all eleven centroids before any `REQUIRE` fires. Timbre only — CPU is SC-025's job. |
| **SC-016** Seraphis stays green | `node tools/check-seraphis-green.js` (S11.2) + a full `dsp_systems_tests` run; clauses 3–4 in dark_materials / `ContinuousBody_MaterialTableIsAppendOnly` | (1) `git diff --numstat --diff-filter=M dsp/tests/unit/systems/` names **exactly** the three files of S10.1. (2) `git diff --numstat` on `continuous_body.h` shows **exactly two deleted lines**, and `git diff -U0` confirms they are the `BodyMaterial` and `kNumMaterials` lines. (3) `seraphis_perf_test.cpp`'s survey still runs over `kNumSeraphisMaterials` and reports the **same worst material** as the pre-change log. **The pre-change log is captured by S11.2 step 0, on the clean tree, before `continuous_body.h` is touched** — without it the clause is unexecutable and the build-time resolution is to quietly weaken it to "the worst material is one of the five", which is not what SC-016 asks. The survey case is `[.perf]` and must be requested explicitly; the capture path is named in S11.2 and repeated in `compliance.md`. (4) `crossfadePartner` still maps Glass→Strings→MetalPlate→Chamber→Ice→Glass and the six new materials cycle among themselves — asserted as a `static_assert`-able table in the dark-materials TU so it cannot silently widen. **Scope note:** this phase modifies a **second** library header, `feedback_ecology.h` (B-7 / S3.2a), and SC-016 is deliberately **not** widened to cover it — clause 1 scans `dsp/tests/unit/systems/` and clause 2 scans `continuous_body.h`, and `FeedbackEcology` has no Seraphis consumer, so its gate is its own suites (S11.2 step 3a) plus the same zero-deleted-lines bar (`git diff --numstat` on that file must show **0** deletions). `tools/check-seraphis-green.js` must therefore not flag it, and the script's comment says so. |
| **SC-017** blend endpoints | voice / `VoragoVoice_BodyBlendEndpoints` | At `b = 0`: render with body B set to material X vs material Y (X ≠ Y) → **bit-identical**; and across two different body-B seeds → **bit-identical**. Symmetrically at `b = 1` for body A. Plus: a render at `b = 0` reached by ramping down from `b = 1` is **within fingerprint tolerance** of a render at `b = 0` from the start. |
| **SC-017a** blend zipper | voice / `VoragoVoice_BodyBlendNoZipper` | `b` automated 0 → 1 over 5 s: `maxDeltaInWindow(20 ms)` on the ramp ≤ **1.5 ×** the same statistic 64 ms clear of it. Without this a chunk-stepped blend passes SC-017 untouched. |
| **SC-018** bloom accounting | voice_longrun / `VoragoVoice_BloomSlotAccounting` `[long]` | 30-minute render accelerated per FR-086 (`A` and the wall-clock cost stated). **At every control step**: the count handed to `setSpectralTarget` ≤ `HarmonicCloud::kMaxPartials`; `bloom().reserveBase() >= kMinParentSlots` (B-1); `setSpectralTarget` never rejected — asserted positively via `cloud().hasSpectralTarget()` tracking the voice's own `targetActive_` expectation. |
| **SC-018a** spectral-target edge | voice / `VoragoVoice_SpectralTargetEdge` | Force the spawn edge (`triggerBloom()` from zero live children) and the retire edge (let the last child retire): `maxDeltaInWindow(20 ms)` across each edge ≤ **1.5 ×** the pre-edge maximum. |
| **SC-019** ecosystem routing | voice_longrun / `VoragoVoice_EcosystemRouting` `[long]` | (1) Ecosystem depth 0 **and every scheduler depth 0** → the routed destinations read **exactly** their configured bases over a 10-minute accelerated render, read back through `getSourceWakeAmount` (`noise_organism.h:880`), `getPeakWakeAmount` (`:855`), `getLoopWakeAmount` (`feedback_ecology.h:1393`), `getGhostRequest()` **and — the `Partial` family, which the spec's clause-1 list omits — `cloud().getMutation()` (`harmonic_cloud.h:493`) and `bloom().getDepth()` (`bloom_engine.h:679`)**, at polyphony 1. FR-020's first destination row is `Partial → HarmonicCloud::setMutation` **and** `BloomEngine::setDepth`, and clause 3 gates "per family" over **five** families, so without those two read-backs an implementation that never wired the `Partial` agents at all passes SC-019, SC-019a and SC-019b. (2) Schedulers at their S8 depths and ecosystem depth 1 → each destination's trace **never falls below** the scheduler-only trace on the same seed. **For the `Partial` family that arm reduces to "never below base": the schedulers' five families are `BloomTrigger`, `NoiseWake`, `PeakWake`, `LoopWake`, `GhostBurst` (S3.6 (c)), and `BloomTrigger` calls `bloom_.triggerBloom()` on the onset edge — it writes neither `setMutation` nor `setDepth`, so the `Partial` destinations have no scheduler contribution.** Stated explicitly so the reduction is a decision, not an omission. (3) Attribution: ≥ **3 distinct value changes per minute** per family attributable to the ecosystem — the loop runs over all five families including `Partial` — and setting a kind's agents dormant makes that family's ecosystem contribution static while the scheduler contribution continues. |
| **SC-019a** combine rule | voice / `VoragoVoice_WakeCombineRule` | Enumerated (scheduler, ecosystem) pairs — both orderings, both zeros, both ones, the equal case — each shared destination reads back **exactly** `max(scheduler, ecosystem)`. No render; the getters of SC-019 (1). |
| **SC-019b** many-to-one | voice / `VoragoVoice_AgentReductionRule` | Enumerated per-slot agent-energy tuples (a clear winner, a tie, a three-way) → the slot's routed contribution equals **exactly** the argmax-energy agent's output, never a blend or an average. The tie case asserts the **lower agent index** wins (S3.6 (b)'s strict `>`). Untagged, no render. |
| **SC-020** slow events | voice_longrun / `VoragoVoice_SlowEventRouting` `[long]` | 30-minute accelerated render: measured inter-event interval distribution inside the configured range; **every** target index in `[0, 5)` selected at least once; each event produces a measurable change on its destination's observable. |
| **SC-020a** slot draws | voice / `VoragoVoice_SlotDrawDeterminism` | Two voices, same seed and config → **identical** slot sequences over a 30-minute accelerated render; two differing **only** in seed → at least one differing draw. Untagged (a determinism case, FR-085). |
| **SC-021a** rate sentinel | engine / `VoragoEngine_SampleRateSentinel` | 48 kHz and 192 kHz, **`kFastAttackEnvelopeConfig`**, 1 s full-poly render: non-silent, finite, bounded, and 192 kHz's broadband RMS within **±3 dB** of 48 kHz's. Untagged. |
| **SC-021b** the rest | engine_longrun / `VoragoEngine_SampleRateSweep` `[long]` | 44.1 / 88.2 / 96 / 176.4 kHz, 10 s, same four assertions against the 48 kHz reference. |
| **SC-022** latency | engine / `VoragoEngine_ReportedLatency` | (1) `getLatencySamples() == smear().getLatencySamples()` at every prepare-time configuration **including `smearEnabled = false`, where both are 0**. (2) Two engines identical but for the prepare-time smear flag, one note, `kFastAttackEnvelopeConfig` so the onset is sharp: the difference in onset sample index (first sample above a stated threshold) **equals `getLatencySamples()` within ±1 sample**. **The enabled arm is configured with `setSmearAmount(0)` and `setSmearDecoherence(0)` before the render, and the clause is void without it.** The *disabled* instance is the documented bit-identical bypass (`spectral_smear.h:15-16`, `:342-347`); an **enabled** instance at the S8.3 defaults (amount 0.20, decoherence 0.20) actively smears, so the two renders are not "the same signal, delayed" — the onset spreads over the STFT window and a ±1-sample onset-index equality is not a property a correct implementation has. At amount 0 and decoherence 0 the component is instead an **exact identity** delayed by `fftSize` — its own banner says so, *"Exact identity at smearAmount == 0 and decoherence == 0 (FR-021, FR-041)"* (`spectral_smear.h:13`) — which is exactly the arm this clause needs. Clause 1's forwarding equality at every configuration is unchanged. |
| **SC-023** degenerate calls | voice / `VoragoVoice_UnpreparedAndDegenerate`; engine / `VoragoEngine_UnpreparedAndDegenerate`; **matrix / `VoragoMacro_UnpreparedAndDegenerate`** | Every public method on an unprepared voice, engine and matrix returns its documented neutral and writes nothing out of bounds. `n = 0`, null pointers (each channel independently), polyphony 1, zero agents, zero noise sources, zero ecology loops, zero resonance peaks, `n = 65536` (far above `maxBlockSamples`), and `n = 37` repeatedly at a never-aligning phase → silence or pass-through, no non-finite sample, and control steps still occur **once per 64 elapsed samples, not once per call**. **The matrix arm is named and owned** (it had no TU before, although SC-023's text says "voice, engine **and matrix**"): `apply()` against an **unprepared** engine writes nothing and does not fault; `computeCavernTargets()` on a **default-constructed** matrix returns exactly the FR-063 defaults, field by field; an out-of-range `VoragoMacro` is a no-op on `setMacro` and returns the neutral on `getMacro`. **Plus FR-076's floor-not-reject behaviour, which no other case asserts:** `prepare()` at a **NaN** sample rate (built from a bit pattern through a `volatile`, never `std::numeric_limits`), at **0.0**, and at **4000 Hz** (below the components' shared `kMinUsableSampleRate = 8000.0`) → `isPrepared()` is true in all three, and a subsequent render is finite and bounded. SC-021a/b sweep 44.1–192 kHz only, so without this the second sentence of FR-076 is designed for (S3.1 step 1) and asserted nowhere. |
| **SC-024** portability | recorded in `compliance.md`, not asserted in-suite | `node tools/check-portability.js` clean; the three headers added to `dsp/lint_all_headers.cpp`; the nine TUs compiled under libstdc++ (`g++ -std=c++20 -fsyntax-only`) as well as MSVC. |
| **SC-025** material budgets | `continuous_body_perf_test.cpp` (extended) / `ContinuousBody_DarkMaterialBudgets` `[.perf]` | The existing case, widened to eleven materials × four configurations = **44 measurements**, each against the shipped `kSteadyBaselineNsPerBlock` / `kOperatingBaselineNsPerBlock` / `kCrossfadeBaselineNsPerBlock` / `kCloudOnlyBaselineNsPerBlock` at `kRegressionFactor = 1.5`. The full table prints **before** any `REQUIRE` fires, preserving that file's own stated discipline (`:875-880`). A material over budget is re-voiced or dropped (FR-038b); the baselines do not move. |
| **SC-026** slot seeds | engine / `VoragoEngine_SlotSeedReproducibility` | (a) note *n* on slot *s*, let it retire, play it again → **within fingerprint tolerance**. (b) force a steal of slot *s*, then play note *n* on it → same. (c) note *n* on slot *s* and on slot *s′* simultaneously → `worstMetricRelativeError > 100 × kMetricTolerance`. Untagged. |
| **SC-027** ghost configuration | engine / `VoragoEngine_GhostConfiguration` | (1) Read back on `engine.atmosphere()` immediately after `prepare()`: `getDensity() == 0.30f`, `getGrainSeconds() == 12.0f`, `getPitchSemitones() == −12.0f`, `getPositionSpread() == 0.90f`, `getBlur() == 0.85f`, `getDecorrelation() == 0.85f` — **exact** comparisons against the S8.3 rows. (2) Event gating: polyphony 1, 10-minute accelerated render with the ghost-destined scheduler live → `getLevel()` shows **≥ 6 burst edges** (0.0 → ≥ half `kGhostBurstPeak` → back); with that scheduler's depth at 0 → **exactly 0** edges and `getLevel()` holds its base. |
| **SC-028** setter contracts | voice / engine / macro `*_SetterContract` | For every public float setter on all three classes: a non-finite argument (built from a bit pattern through a `volatile`) leaves the previous value standing; an out-of-range argument is clamped and the getter reports the clamp; an out-of-range **index** is a silent no-op that writes nothing. `setMacro`/`getMacro` and `setMacros`/`getMacros` round-trip over an enumerated table including both endpoints, every neutral and non-finite inputs; an out-of-range `VoragoMacro` is a no-op on set and returns the neutral on get. Untagged. |
| **SC-029** containment runs | nonfinite / `VoragoEngine_NonFiniteContainment` | With `k ≥ 2` voices sounding, `detail::VoragoEngineNonFiniteProbe::poisonVoiceCarry(engine, i, bits)` mid-render, for **each** voice index *i* and each of the three bit patterns: the block output is finite throughout; `getNonFiniteRecoveryCount()` increments by **exactly 1**; the **other** voices' contribution is **bit-unchanged** against a reference render with no injection; voice *i* resumes rendering (non-silent within a stated number of chunks after its reset). Per-push, untagged (FR-085). |
| **SC-030** life parity | engine / `VoragoEngine_AdvanceLifeOnlyParity` | Two slots from the same slot-seed derivation, one rendering and one only `advanceLifeOnly`-advanced over the **same sample count** across a partitioned schedule: **exactly equal** `ecosystem().getControlStepCount()` and equal scheduler event counts; then `noteOn` the idle one and compare its first-chunk agent outputs against the rendering one **within fingerprint tolerance** — it must not start from a cold ecosystem. |
| **SC-031** held sub-fundamental | engine / `VoragoEngine_HeldSubFundamental` | `noteOn` the lowest note, render, `noteOff` **all**, render through a ≥ 10 s silence gap, `noteOn` a different note: `subharmonic().getFundamentalHz()` **unchanged across the whole gap**, and the render across the gap contains no sample-to-sample delta above SC-011's click threshold. |

### S10.3a FR-gated cases with no criterion of their own

| FR | TU / `TEST_CASE` | Assertion |
|---|---|---|
| **FR-002** | voice / `VoragoVoice_SizeAndOwnership` | Prints `sizeof(VoragoVoice)` (the figure `kVoiceSizeBound` is derived from) and asserts it is ≤ the bound; the bound's own `static_assert` lives in the header. Prints `sizeof(VoragoEngine)` likewise. |
| **FR-015 / B-3** | voice / `VoragoVoice_NoiseDecorrelationMonoSum` | Sweep 20 Hz – 8 kHz through the two all-passes: **per-channel** magnitude flat within 0.01 dB (the all-pass property); **mono-sum** magnitude deviation ≤ **3.0 dB**, and at least **6 dB better** than a one-sample-delay pair measured in the same case. Prints the measured worst deviation, which is the figure the header quotes. |
| **FR-011 / B-2** | voice / `VoragoVoice_SpectralTargetIsNeutral` | With the bloom held inert (`setDepth(0)`, `setWake(0)`), a render with the voice's supplied target is **bit-identical** to the same render with `clearSpectralTarget()` forced — at four richness values, four tilts, four gravities and four inharmonicities (the "at every setting" claim, sampled). This is the case that would fail if `std::pow` were used instead of the cloud's own `exp2`/table. |
| **FR-012 / B-1** | voice / `VoragoVoice_BloomCapacityTracksCloud` | Sweep richness across `[0, 1]`: `bloom().capacity() == clamp(cloud().getActivePartialCount(), 14, 64)` at every step; `bloom().reserveBase() >= kMinParentSlots`; `bloom().getConsumerTiltDb() == ` the cloud's tilt. |
| **FR-014 / FR-090** | voice / `VoragoVoice_EnvelopeShapeIsShipped` | Stage times and release read back exactly as S3.8's table; `Growth` mode zeroes **every** pre-sustain stage time and a Standard → Growth → Standard round trip restores all six. |
| **FR-024** | voice / `VoragoVoice_DormancyIsTheComponents` | Wake at exactly 0 with `dormant == false` behaves as the component defines it; a wake edge during a bloom fade and during a steal ramp leaves neither the 50 ms re-entry fade nor the 1 ms silence ramp truncated. |
| **FR-039** | dark_materials / `ContinuousBody_AppendOnlyEnumerators` | `static_assert`s that `Glass == 0, Strings == 1, MetalPlate == 2, Chamber == 3, Ice == 4` and that the six new enumerators are 5…10, so no stored Seraphis material index moved. |
| **FR-048** | engine / `VoragoEngine_SeedIsPerSlotNotPerNote` | `getSeed()` and each slot's derived seed are unchanged across `noteOn`/`noteOff`/retire/steal cycles — the mechanical half of SC-026. |
| **FR-073** | composed / `VoragoComposed_OutputIsBounded` | The full `engine → CavernVerb → processOutputStage` chain at every macro extreme and every polyphony: `\|out\| <= 1.0` on every sample. This is the only place FR-073 is asserted **on the real chain**, which is what the seam exists for. |
| **FR-026** | voice / `VoragoVoice_LifeModulatorLanes` | **FR-026's only assertion anywhere.** Untagged, no render on the gravity arm. (1) With `setBreathingDepth(0)`: `resonance().getGravity()` (`resonance_drift_network.h:817`) equals `getResonanceGravity()` — the base — **exactly**, at every control step over 2 000 steps, and `getBreathingGravityLane()` is exactly `0.0f`. (2) With `setBreathingDepth(0.30)` and `breath_.setRate` at S8.2's 0.017 Hz: over one full breath period (≈ 59 s of control steps) `getGravity() − base` traverses a range of **≥ 0.20** and its extremes are `±0.30` within tolerance, i.e. the lane is *summed onto* the base, not ignored and not squared. (3) Tidal half: `getTidalFogDepth()` is exactly `0.0f` at `setTidalDepth(0)` and reaches **≥ 0.25** somewhere inside one tidal period at S8.2's depth 0.40, and is **never negative** (the `max(0, ·)` net). Without this case, an implementation that advanced both modulators once per chunk and then discarded both outputs passes every criterion in S10.3. |
| **FR-026** (engine half) | engine / `VoragoEngine_TidalFogFold` | The precedence rule of S6.4/S6.6 step 5, which no criterion covers: with every voice at tidal depth 0, `smear().getSmearAmount()` equals `engine.getSmearAmount()` (= `smearBase_`) **exactly** at every control step, including after an `apply()` at a non-neutral `Fog`; with tidal depth 0.40 on one voice it equals `clamp(smearBase_ + max_v getTidalFogDepth(), 0, 1)` at every control step. This is the case that fails if `setSmearAmount` writes the component instead of the base — in which case the `Fog` macro is overwritten on the next chunk. |
| **FR-067** | macro / `VoragoMacro_ApplyIsIdempotent` | Untagged. Set all twelve macros to a stated **non-neutral** vector (each at 0.75, `Gravity` at 0.85), then render 10 s twice from identical prepared engines: once with `apply()` called **once** before the render, once with `apply()` called at **every** 64-sample control chunk. The two renders must be **bit-identical** (`std::memcmp`). SC-010 cannot see this — it measures a zipper *during* a ramp, where a forwarder that re-arms a ramp or snaps a smoother on every unchanged write looks identical to a correct one — and SC-009 clause 3 covers only the neutral. A `setBodyBlend`/`setSmearAmount`/`setNoiseLevelDb` forwarder that called `snapTo` instead of `setTarget`, or reset a component's internal smoother, would otherwise pass the entire suite while making the instrument step on **every block** at any non-neutral macro setting. |
| **B-7** | voice / `VoragoVoice_SilenceClearsEcologyAudio` | The behavioural half of B-7: the O(1) clear must still **clear**. Render a voice to a loud steady state with the ecology mix up, call `silence()` then `resetForSteal()`, then render with the excitation held off (bloom depth 0, noise level at its floor, no `noteOn`): the output over the next `FeedbackEcology::kMaxDelayMs`-long window stays below **−80 dBFS**. That bound and shape are the component's own — its sleep-edge clause is measured at −80 dBFS over the first 500 ms after a silent wake (`feedback_ecology.h:1890-1896`), and *"a build that only skips the chain fails by 60 dB or more"*. This is the case that fails if `silenceAudio()` is implemented as a no-op or if the read-mute window is not installed. |
| **B-7** | perf / `VoragoVoice_ClearingPathCost` `[.perf]` | The cost half. Best-of-25, P-core-pinned, at **48 kHz and 192 kHz**, printing all four figures before any `REQUIRE`: `reset()`, `resetForRecovery()`, `resetForSteal()` and `silence()`. Asserts (a) each of the three RT-safe paths is **≥ 10 × cheaper than `reset()`** at both rates — the non-vacuous clause, and the one that regresses the day someone puts `ecology_.reset()` back on the steal path — and (b) a `silence()` + `resetForSteal()` pair (what one steal actually costs, S6.7) fits inside **one 64-sample control chunk at the measured rate** (1 333 333 ns at 48 kHz, 333 333 ns at 192 kHz). The figure from (b) is what `kResetsPerControlChunk`'s corrected rationale (S6.2) is sized against and is recorded in `compliance.md`. |

### S10.4 Runtime ledger — so a slow suite is a known number, not a surprise

| Case | Arm | Estimated wall clock |
|---|---|---|
| `VoragoEngine_SoakSentinel` | per-push | ~15 s (60 s of audio at ~4× real time, polyphony 4) |
| `VoragoEngine_ConfigurationFuzzSentinel` | per-push | ~25 s (32 × 2 s) |
| `VoragoEngine_NonFiniteContainment` | per-push | < 5 s |
| `VoragoMacro_NeutralIsIdentity` | per-push | ~10 s |
| `VoragoMacro_ApplyIsIdempotent` (FR-067) | per-push | ~6 s (two 10 s renders at polyphony 4) |
| `VoragoEngine_TidalFogFold` (FR-026, engine half) | per-push | < 5 s (control-step read-back, no long render) |
| `VoragoMacro_UnpreparedAndDegenerate` (SC-023, matrix) | per-push | < 1 s (no render) |
| `VoragoVoice_*` (all per-push voice cases, now including `VoragoVoice_LifeModulatorLanes` and `VoragoVoice_SilenceClearsEcologyAudio`) | per-push | < 90 s total |
| `VoragoEngine_OvernightSoak` + `OvernightEvolution` | `[long]` | **8 h of audio × 3 seeds, unaccelerated** — the dominant cost of the phase. Rendered **once**: the two cases share one render through a Catch2 fixture, and the figure is recorded in `compliance.md`. At ~4× real time that is ~6 h of wall clock; **this runs in the nightly lane only** (`long-tests-nightly.yml`). |
| `VoragoEngine_ConfigurationFuzz` | `[long]` | 968 × 10 s accelerated → ~40 min |
| `VoragoMacro_SweepAxes` | `[long]` | 12 × 5 × 3 × 60 s accelerated-envelope renders → ~45 min |
| `VoragoVoice_*` `[long]` set | `[long]` | ~20 min |
| `vorago_perf_test` (SC-001a/b, SC-002 incl. the `advanceLifeOnly` arm, SC-003, `VoragoVoice_ClearingPathCost`) | `[.perf]`, run **alone** | ~11 min |

The `[long]` tag is applied **only** where the cost is > ~15 s **and** the failure mode is
toolchain-independent (FR-085, `CLAUDE.md`). SC-006, SC-009, SC-013a, SC-021a, SC-026, SC-029 and
SC-030 are untagged **in full** — they are the cross-platform sentinels, and a `[long]`-only
boundedness case surfaces its Linux/macOS failure a day late, which is exactly the failure mode the
project rule names.

---

## S11. Build integration — the exact edits

### S11.1 Files that change

| File | Edit |
|---|---|
| `dsp/include/krate/dsp/systems/vorago_voice.h` | **new** |
| `dsp/include/krate/dsp/systems/vorago_engine.h` | **new** |
| `dsp/include/krate/dsp/systems/vorago_macro_matrix.h` | **new** |
| `dsp/include/krate/dsp/systems/continuous_body.h` | append-only but for the two enumerated widenings (S4.3, FR-039) |
| `dsp/include/krate/dsp/systems/feedback_ecology.h` | **append-only, zero deleted lines** — one public method, `void silenceAudio() noexcept`, beside `reset()` (S3.2a, B-7). No new class, member, enumerator or constant. No Seraphis consumer, so SC-016's gate does not widen; the green gate is this component's own suites (S11.2 step 3a). |
| `dsp/lint_all_headers.cpp` | three `#include`s appended to the Layer-3 block, after `ecosystem_engine.h` (`:194`), with the Phase-10 comment the Phase 9 block at `:198` uses |
| `dsp/CMakeLists.txt` | the three headers added to the `KRATE_DSP_SYSTEMS_HEADERS` IDE/source-group list (cosmetic, but the list is maintained) |
| `dsp/tests/CMakeLists.txt` | eight TUs appended to the enumerated `add_executable(dsp_systems_tests` list (`:324`, after the Phase 8 block at `:493`), one TU appended to `add_executable(dsp_effects_tests` (`:504`, after the Phase 9 block at `:533`) — each with a Vorago-Phase-2/3-style comment block naming which criteria each TU carries. **No `target_compile_definitions` line is added** (B-4). |
| `dsp/tests/unit/systems/continuous_body_perf_test.cpp` | FR-038 sites 1–4 + SC-025's 44-measurement table |
| `dsp/tests/unit/systems/continuous_body_test.cpp` | FR-038 site 5 |
| `dsp/tests/unit/systems/seraphis_perf_test.cpp` | FR-038 sites 6–7 (re-pointed at `kNumSeraphisMaterials`) |
| `tests/test_helpers/vorago_fixtures.h` | **new** — no CMake edit needed (the directory is an INTERFACE target) |
| `tools/gen-vorago-material-tables.js` | **new** — Node, prints the three ratio tables (S4.1) |
| `tools/check-seraphis-green.js` | **new** — Node, runs SC-016 clauses 1–2's `git diff --numstat` / `-U0` checks and exits non-zero with the offending paths. It also enforces the **zero-deleted-lines** bar on `feedback_ecology.h` (B-7's append), and its header comment states why that file is in scope for that bar but **out** of scope for the Seraphis gate: the component has no Seraphis consumer, so its green gate is its own suites (S11.2 step 3a). |
| `specs/_architecture_/` | regenerated by `node tools/gen-specs-index.js` (FR-084) |

The CMake block to append to the systems list, in the file's own house style:

```cmake
    # Vorago Phase 10 (specs/vorago-phase10-voice-engine): VoragoVoice, VoragoEngine,
    # VoragoMacroMatrix + the six dark ContinuousBody materials.
    # This list is ENUMERATED, not globbed - an unregistered TU silently drops out of
    # the build and its cases never run.
    #   vorago_voice_test.cpp           SC-007(voice), SC-017, SC-017a, SC-018a, SC-019a,
    #                                   SC-019b, SC-020a, SC-023(voice), SC-028(voice)
    #   vorago_voice_longrun_test.cpp   SC-018, SC-019, SC-020            (the [long] set)
    #   vorago_engine_test.cpp          SC-004a, SC-006, SC-007(engine), SC-011, SC-012,
    #                                   SC-013a, SC-014, SC-021a, SC-022, SC-023, SC-026,
    #                                   SC-027, SC-028, SC-030, SC-031
    #   vorago_engine_longrun_test.cpp  SC-004b, SC-005, SC-013b, SC-021b (the [long] set)
    #   vorago_macro_test.cpp           SC-008 (eleven macros), SC-009, SC-010, SC-023(matrix),
    #                                   SC-028, FR-067's VoragoMacro_ApplyIsIdempotent
    #   vorago_dark_materials_test.cpp  SC-015, SC-016 (3)(4)
    #   vorago_nonfinite_test.cpp       SC-029 only, + the probe definition
    #   vorago_perf_test.cpp            SC-001a, SC-001b, SC-002 (+ the advanceLifeOnly/L arm),
    #                                   SC-003, VoragoVoice_ClearingPathCost      [.perf]
    unit/systems/vorago_voice_test.cpp
    unit/systems/vorago_voice_longrun_test.cpp
    unit/systems/vorago_engine_test.cpp
    unit/systems/vorago_engine_longrun_test.cpp
    unit/systems/vorago_macro_test.cpp
    unit/systems/vorago_dark_materials_test.cpp
    unit/systems/vorago_nonfinite_test.cpp
    unit/systems/vorago_perf_test.cpp
```

and to the effects list (FR-084a — **the only** Phase 10 TU there):

```cmake
    # Vorago Phase 10 (specs/vorago-phase10-voice-engine): the composed
    # engine -> CavernVerb -> processOutputStage chain. It lives HERE, not in
    # dsp_systems_tests, so it compiles under this target's KRATE_DSP_AETHER_TEST_HOOKS
    # consistently with every other TU in this executable (FR-084a / OQ3).
    unit/effects/vorago_composed_chain_test.cpp
```

### S11.2 Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# 0. SC-016 CLAUSE 3'S PRE-CHANGE CAPTURE. RUN THIS FIRST, ON THE CLEAN TREE,
#    BEFORE continuous_body.h IS TOUCHED. Clause 3 compares the Seraphis material
#    survey's reported WORST MATERIAL against the pre-change log; with no capture the
#    clause is unexecutable and the build-time resolution is to weaken it to "the worst
#    material is one of the five", which is not what SC-016 asks. The survey case is
#    [.perf] and must be requested explicitly - a default run skips it.
mkdir -p specs/vorago-phase10-voice-engine/artifacts
node tools/run-cpu-tests.js dsp_systems_tests 2>&1 \
  | tee specs/vorago-phase10-voice-engine/artifacts/seraphis-material-survey-prechange.log
# The path above is quoted verbatim in compliance.md, and step 3 diffs against it.

# 1. build the two layers that own the new TUs
"$CMAKE" --build build/windows-x64-release --config Release \
         --target dsp_systems_tests dsp_effects_tests

# 2. the per-push lane (excludes the timing-sensitive and [long] sets)
build/windows-x64-release/bin/Release/dsp_systems_tests.exe \
    "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_effects_tests.exe \
    "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]" 2>&1 | tail -5

# 3. SC-016: the untouched consumers of the extended header, in full
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ContinuousBody*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "Seraphis*"      2>&1 | tail -5
node tools/check-seraphis-green.js          # clauses 1-2, the git-diff scope checks
# clause 3: the survey's worst material, diffed against step 0's capture
node tools/run-cpu-tests.js dsp_systems_tests 2>&1 \
  | tee specs/vorago-phase10-voice-engine/artifacts/seraphis-material-survey-postchange.log
diff <(grep -i "worst material" specs/vorago-phase10-voice-engine/artifacts/seraphis-material-survey-prechange.log) \
     <(grep -i "worst material" specs/vorago-phase10-voice-engine/artifacts/seraphis-material-survey-postchange.log)

# 3a. B-7's second shared-component change: FeedbackEcology's own suites, in full.
#     silenceAudio() is append-only and has no Seraphis consumer, so THIS is its gate.
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology*" 2>&1 | tail -5

# 4. the [long] set, explicitly (per-push CI excludes it; it runs nightly on all three OS legs)
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[long]" 2>&1 | tee /tmp/vorago-long.log | tail -5

# 5. the portability and lint gates - a green MSVC build proves nothing for Linux/macOS
node tools/check-portability.js
node tools/lint-layers.js
node tools/lint-odr.js
node tools/lint-nonfinite-symbols.js
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_lint_stub
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja

# 6. perf, ALONE, nothing else executing, after the machine has idled
node tools/run-cpu-tests.js dsp_systems_tests
```

A whole-suite run is `ctest --test-dir build/windows-x64-release -C Release --output-on-failure`;
a single suite is always the executable directly (`ctest -R dsp_systems_tests` matches **nothing** —
`catch_discover_tests` registers Catch2 case names, not executable names).

---

## S12. CPU budget, and the polyphony recommendation (FR-080 – FR-083, OQ-1(a))

### S12.1 Basis, inherited verbatim

```cpp
constexpr double kSr48          = 48000.0;
constexpr std::size_t kBlockSize = 512;
constexpr double kBlockBudgetNs = (512.0 / 48000.0) * 1.0e9;   // 10 666 666.67
constexpr double kRegressionFactor = 1.5;
constexpr double kReferenceNs   = kBlockBudgetNs * 0.30;        // 3 200 000.0  (roadmap line 470)
constexpr double kMaxAdmissibleNs = kReferenceNs / kRegressionFactor;  // 2 133 333.3
```

Best-of-25 × 500 blocks after 400 warm-up blocks, P-core-pinned, run alone via
`node tools/run-cpu-tests.js` (`cavern_verb_perf_test.cpp:105-125`). Every checked-in baseline is
`⌈measured × 1.05⌉` with a paired `static_assert(baseline <= kMaxAdmissibleNs, …)` (FR-083).

### S12.2 The projection, with OQ-1(b)/(c) already applied

The architecture is fixed by the spec's own clarifications: the ghost/atmosphere tap is **global**
(L-1 applied, FR-056) and the ecosystem stays **per-voice** (L-2 rejected, FR-002). That is FR-081's
"L-1" row: **`V = 474 395` ns/voice/block, `G = 411 330` ns**.

**The solve is not `N·V + G`, and correcting it is what changes the recommendation.** S6.5 fixes the
render-loop bound at `v < kMaxVoices` **unconditionally**, and every non-rendering slot takes
`advanceLifeOnly(slice)`, whose S3.4 step-1 body is the full identity layer:
`ecosystem_.processChunk(n)` — **29 074.0 ns/block** on its own (spec FR-081's table, sourced to
`specs/vorago-phase8-ecosystem/compliance.md:62`) — plus two `SlowEventScheduler::processBlock`,
`breath_.processBlock`, `tide_.processBlock` and `publishIdentity()`'s reduction scan. The four
control-rate sources measured together in Vorago Phase 1 came to **6 893 ns/block worst-of-six** for
*four* Perlin sources + *four* schedulers + a chaos source
(`specs/vorago-phase1-events-modulation/compliance.md:68`), so two schedulers and two life modulators
are a small fraction of that; seeding a per-slot life-only cost of

> **`L ≈ 31 000` ns/block** — ecosystem 29 074 + ~1 500 for the schedulers and modulators + the
> reduction scan. **A projection, and SC-001a measures it directly** (SC-002's standalone
> `advanceLifeOnly` arm). If the measured `L` differs, every number below is recomputed from it
> before Q-A is ruled.

the solve becomes

```
N·V + (kMaxVoices − N)·L + G ≤ budget
```

`N_ref` = the largest `N` satisfying it at 3 200 000; `N_gate` = the largest at 2 133 333; both
columns capped at `kMaxVoices`.

**At `kMaxVoices = 8`:**

| Levers on top of L-1 | `V` | `V × 1.15` | `N_ref` bare / ×1.15 | `N_gate` bare / ×1.15 |
|---|---:|---:|---:|---:|
| none (as specified) | 474 395 | 545 554 | 5 / **4** | 3 / **2** |
| + L-3 (one body) | 421 792 | 485 061 | 6 / 5 | 3 / 3 |
| + L-4 (counts 4→2, 6→4, 12→8) *projected* | 348 506 | 400 782 | 8 / 6 | 4 / **3** (3.99) |
| + L-3 + L-4 *projected* | 295 903 | 340 288 | 8 / 8 | 5 / 4 |

**At `kMaxVoices = 6`:**

| Levers on top of L-1 | `N_ref` bare / ×1.15 | `N_gate` bare / ×1.15 |
|---|---:|---:|
| none (as specified) | 5 / 5 | 3 / 2 |
| + L-3 (one body) | 6 / 5 | 3 / 3 |
| + L-4 *projected* | 6 / 6 | **4 / 4** (4.84 / 4.15) |
| + L-3 + L-4 *projected* | 6 / 6 | 5 / 4 |

**The finding that decides the recommendation, restated against the corrected solve:** **L-4 alone
carries polyphony 4 past the regression-gated line in both columns — but only with `kMaxVoices` at 6.**
At 8 the four never-allocated slots cost ~124 000 ns/block and the gated ×1.15 column falls to
**3.99**, i.e. 4 stops being reachable, on a margin of **645 ns per spare slot** (the break-even is
`L ≤ 29 719`, below the ecosystem figure alone). That is far inside the projection's error bars in
either direction, which is exactly why SC-001a must measure `L` before Q-A is ruled. **L-3 remains
unnecessary** and is still the one lever that would delete a roadmap deliverable (line 122's
multi-body blend). Polyphony **6** is reachable at `kReferenceNs` with L-4 but **never** at the
regression-gated line on these numbers.

**The one alternative to lowering `kMaxVoices`, considered and refused at plan stage.** The `L` term
exists only because the loop bound is unconditional; bounding it at `polyphony_` (plus the tracked
orphans) would delete the term outright. The plan does **not** take that route: FR-046 exists so that
"a voice that is stolen into after minutes of silence does not start from a cold ecosystem", and a
slot admitted later by `setPolyphony()` growth would be exactly such a cold slot, while SC-030's
parity clause is written against slots that have been advanced all along. Seraphis makes the same call
for the same reason (`seraphis_engine.h:544-547`). It is recorded here as **L-7's alternative** so the
option is visible rather than rediscovered; taking it would need a user ruling and an FR-046
amendment, and it trades a correctness property for CPU, which is the trade this project's ladder
puts last.

### S12.3 The recommendation put to the user (OQ-1(a))

> **Ship polyphony 4. Lower `kMaxVoices` to 6 (L-7). Keep both bodies (no L-3). Apply L-4 if
> FR-082's measured probe says the gated line needs it — on the projection, it does.**

* **4** is the roadmap's own floor (line 460, "4–8 voices") and is the only value the arithmetic
  supports at the regression-gated line without giving up a deliverable.
* **`kMaxVoices = 6`, not 8.** This is a **change from the previous recommendation** and it follows
  from B-6's corrected arithmetic: a spare slot is not free — it costs `L` every block — and at 8 the
  spare slots alone are ~5.8 % of `kMaxAdmissibleNs`, which is what takes polyphony 4 + L-4 from 4.30
  to 3.99 in the gated ×1.15 column. 6 keeps two slots of headroom for Phase 12 to raise the shipped
  default without a structural change, and it is the cheapest lever on the ladder because it costs no
  deliverable and no musical property — only reach. **It does narrow roadmap line 460's "4–8 voices"
  to 4–6, which is why it is Q-A's second half and not a plan-stage decision.** The constant stays at
  **8 while SC-001a's survey runs**, so the whole {1, 2, 4, 6, 8} polyphony curve is measurable; it is
  lowered in the same commit that checks in the SC-001b baseline.
* **L-3 is refused** at plan stage: it removes roadmap line 122's two-body blend, which FR-036, FR-037,
  SC-017 and SC-017a all depend on. If the measurement says 4 is unreachable *even with* L-4, that is
  an L-6 "stop and surface", not a quiet L-3.
* **L-4 is held in reserve and is a projection of a projection** (FR-081's own words). FR-082's
  stage-cost probe and SC-001a's survey run **first**; the ladder is then recomputed from measured
  numbers and the ruling taken against those.

**Nothing is checked in until the ruling is written back into the spec as SC-001b** (FR-083): a
baseline transcribed against an unruled configuration pins the wrong workload and is the one way the
ladder gets skipped in practice.

### S12.4 The lever ladder if a measured arm misses — applied in this order

1. **L-7 (new at plan stage) — lower `kMaxVoices`.** Each slot removed above the shipped polyphony
   returns `L ≈ 31 000` ns/block for free: one constant, no deliverable, no musical property, no
   default change anywhere else (`kEngineSizeBound` follows it automatically). It is first on the
   ladder because it is the only step that costs nothing but *reach*. Bounded below at the shipped
   polyphony, and **narrowing roadmap line 460's "4–8 voices" needs the user's ruling** (Q-A), which
   is why it is a lever and not already applied. *FR-081 enumerates L-1…L-6; adding L-7 is recorded
   as amendment A-5 in S14.* Its alternative — bounding the render loop at `polyphony_` instead of
   `kMaxVoices` — is refused in S12.2 and would need an FR-046 amendment.
2. **L-4** — per-voice counts (`NoiseOrganism` 4 → 2, `FeedbackEcology` 6 → 4,
   `ResonanceDriftNetwork` 12 → 8), which are `VoragoVoiceConfig` fields and therefore a **default**
   change, not a code change.
3. **Cheaper material voicing** — `WoodenHull`/`GlassSphere`-style mode-count reductions on body A/B's
   shipped defaults (S8.2), which is again a default change.
4. **L-3** — one body. **Only with a recorded user ruling**, because it deletes a roadmap deliverable.
5. **L-5** — reduce shipped polyphony. **Bounded below at 4** (roadmap line 460); anything below is a
   roadmap amendment, not a lever this plan may pull.
6. **L-6 — stop and surface.** Take the measurement, the ladder and the residual to the user.

**Never** relax `kReferenceNs`, shrink the measured workload, or raise a checked-in baseline
(roadmap line 558, `CLAUDE.md`, and the Phase 8 precedent at roadmap lines 375–378).

---

## S13. Risks and mitigations

| # | Risk | Why it is real here | Mitigation |
|---|---|---|---|
| R-1 | **The budget misses even at polyphony 4.** | The projection is a sum of isolated figures and SC-003 allows 15 % composition overhead on top. | FR-082's probe runs **before** the ruling; S12.4's ladder is ordered and every step is a default change until step 3. The plan refuses L-3 rather than discovering at build time that it was taken silently. |
| R-2 | **`kNumMaterials` widening rots a Seraphis site instead of breaking it.** | Four of the seven sites are `constexpr std::array<T, kNumMaterials>` with five initialisers: at 11 they **still compile** and zero-fill, giving six `Glass` entries surveyed and six **null** `const char*` streamed — UB, not a build break. | All seven enumerated in S4.3; `kNumSeraphisMaterials` re-points the Seraphis survey at its own prefix (FR-038a); SC-016 clauses 1–2 are mechanical `git diff --numstat` checks run by `tools/check-seraphis-green.js`; clause 3 compares the reported worst material against the captured pre-change log. |
| R-3 | **A new material is over the shipped body budgets.** | Two of the four `continuous_body_perf_test` baselines are **capped, not measurement-pinned** (`:160-180`), so the inherited headroom is 29 %, not 50 %. | S4.0's all-Modal ruling means every new material presents the workload the baselines were already set against (the 32-mode plate is the pinned worst); two of the six are cheaper still (24 and 20 modes). If one still misses: re-voice, then drop and surface (FR-038b). Never a baseline move. |
| R-4 | **FR-011's neutrality is not actually neutral**, and the failure is a slow level drift nothing names. | `std::pow(n, -p)` differs from the cloud's `exp2(-p·log2N[i])` in the last bits, and `-ffast-math` makes `pow(n, 1.0f)` return 31.999998 for n = 32 — the cloud's own header says so. | B-2 pins the expression to the cloud's own namespace-scope table; `VoragoVoice_SpectralTargetIsNeutral` asserts **bit-identity** against a forced `clearSpectralTarget()` render across a 4 × 4 × 4 × 4 setting grid. |
| R-5 | **The bloom writes into slots the cloud never sounds**, so blooms are silently inaudible. | `activeCount_` is a function of richness alone; a child above it is dropped before the spectral-target branch (`bloom_engine.h:339-347`). | B-1's capacity rule + `VoragoVoice_BloomCapacityTracksCloud` sweeping richness across `[0, 1]`; SC-018 asserts `reserveBase() >= kMinParentSlots` at **every** control step. |
| R-6 | **The overlap counter saturates on every chunk**, destroying the component's own overrun signal. | An engaged `processChunk` with `pc > reserveBase()` increments it once per call (`:1541-1549`). | B-1 publishes `reserveBase()` as the parent count, so `pc == reserveBase()` and the counter stays a real signal. An assertion on `getOverlapEngagementCount() == 0` rides in `VoragoVoice_BloomCapacityTracksCloud`. |
| R-7 | **Denormals in the two-body blend and the all-pass pair.** | Both are multiply-adds on decaying tails; a drone spends most of its life there. | FTZ/DAZ is set process-wide by `dsp_test_main.cpp` and by the plugin at Phase 11; every upstream component already flushes. The soak's `getNonFiniteRecoveryCount() == 0` plus the CPU survey at polyphony 8 would expose a denormal stall as a measurement, which is why SC-001a sweeps polyphony rather than measuring one point. |
| R-8 | **A `[long]`-only boundedness case hides a Linux/macOS failure for a day.** | Exactly the failure mode `CLAUDE.md` names. | FR-085's split is implemented as three pairs (SC-004a/b, SC-013a/b, SC-021a/b) and S10.4's ledger states which arm is which. SC-006, SC-009, SC-026, SC-029, SC-030 are untagged in full. The build **may not** re-merge a pair. |
| R-9 | **The 8 h soak is the phase's dominant cost and is easy to run twice.** | SC-004b and SC-005 both want the same render, at three seeds. | One render, shared by both cases through a Catch2 fixture; the wall-clock figure is recorded in `compliance.md`; the `[long]` lane is nightly-only. |
| R-10 | **`VoragoEngine` on the stack** overflows MSVC's 1 MiB main-thread stack. | `std::array<VoragoVoice, 8>` is hundreds of KB and `VoragoVoice` is far larger than `SeraphisVoice` (two bodies + ecosystem + ecology). | `makeEngine()` in the fixtures header is the only construction path in the tests; the header carries `seraphis_engine.h:201-204`'s warning verbatim; `kEngineSizeBound`'s `static_assert` fires on a ninth slot. |
| R-11 | **A non-finite contained but never *exercised*.** | Every other criterion asserts the counter is **zero**, so a dead, mis-indexed or wrong-voice branch passes the whole suite. | SC-029 injects for **every** voice index and **every** bit pattern, and asserts the other voices are bit-unchanged — the clause that catches a wrong-voice reset. |
| R-12 | **The macro table and the S8 defaults drift apart.** | Two literals, two files, one meaning. | `everyRowSharesOneBasePerTarget` catches disagreement **at compile time**; SC-009 clause 1 catches base-vs-getter drift at run time, per row; S8 states the rule that both change in the same commit. |
| R-13 | **A positional brace init slips into a config or a table row.** | MSVC accepts narrowing; Clang errors — a Windows-green / CI-red construct. | Designated initialisers everywhere, stated in S2.3, S4.2, S6.3 and S7.3; `node tools/check-portability.js` and the libstdc++ syntax pass before the commit. |
| R-15 | **A later edit puts `FeedbackEcology::reset()` back on an audio-thread path.** | It is the obvious call, the name reads harmless, and nothing about a green suite would change — the cost is a `std::fill`, not a wrong number. | B-7's split is stated in three places (S3.2's table, S6.2's constant comment, S9's two tables), `resetForRecovery()`/`resetForSteal()`/`silence()` are *named* so a reviewer sees which side a call sits on, and `VoragoVoice_ClearingPathCost` `[.perf]` asserts the RT paths stay **≥ 10 ×** cheaper than `reset()` — which is exactly the assertion that fails on the day it happens. |
| R-16 | **The polyphony ruling is taken against the wrong arithmetic**, because the spare-slot term is invisible in a per-voice projection. | `N·V + G` is the natural-looking solve and it is what every earlier draft used; the `(kMaxVoices − N)·L` term only appears if you read S6.5's loop bound, and it is worth 5.8 % of the gated budget at `kMaxVoices = 8` — enough to flip the recommendation. | S12.2 carries the corrected solve and **two** `kMaxVoices` columns; B-6 withdraws the "nothing gates" claim by name; SC-002 measures `L` standalone and SC-001a recomputes the ladder from the measurement; Q-A puts `kMaxVoices` to the user as a budget number rather than a free ceiling. |
| R-14 | **The mono-sum claim in Q8 is taken literally** and someone "fixes" the all-pass pair to chase a flatness that is unattainable with two sections. | Two different all-passes cannot have a constant phase difference. | B-3 states the achievable property, the header records the **measured** deviation, and `VoragoVoice_NoiseDecorrelationMonoSum` pins it with a comparison against the delay-pair alternative so the trade is visible rather than folkloric. |

---

## S14. Open questions for the user — the four this plan cannot decide on its own authority

### Q-A. OQ-1(a): shipped polyphony **and `kMaxVoices`**. **Recommendation: 4, with `kMaxVoices` lowered to 6.**

The full arithmetic is S12.2–S12.3 and the correction behind it is B-6. **This question now has two
halves, because `kMaxVoices` turned out not to be free.**

*The polyphony half.* At the architecture the spec already fixed, `kReferenceNs` admits 5 voices bare
/ 4 with the SC-003 overhead and the regression-gated line admits 3 / 2. **L-4** (per-voice
source/loop/peak counts 4 → 2, 6 → 4, 12 → 8, all `VoragoVoiceConfig` defaults) carries **4** past the
gated line, and **6 is not reachable at the gated line at all**. L-3 (one body) is refused because it
deletes roadmap line 122's deliverable.

*The `kMaxVoices` half, which is new.* The render loop advances **every** slot every block (S6.5,
FR-046), so each slot above the shipped polyphony costs the life-only figure `L ≈ 31 000` ns/block —
`EcosystemEngine` alone is 29 074. At `kMaxVoices = 8` and polyphony 4 that is ~124 000 ns/block,
~5.8 % of `kMaxAdmissibleNs`, and it moves polyphony 4 + L-4 from **4.30 to 3.99** in the gated
×1.15 column: from passing to failing. At `kMaxVoices = 6` the same configuration sits at **4.15** and
passes. The recommendation is therefore **6**, which keeps two slots of Phase-12 headroom and costs no
deliverable and no musical property — **but it narrows roadmap line 460's "4–8 voices" to 4–6, which
is a roadmap-touching choice and is why it is here rather than taken.** The alternative (bound the
render loop at `polyphony_` and delete the term) is refused in S12.2: it would admit a cold-ecosystem
slot on a `setPolyphony()` growth, contra FR-046, and would need its own amendment.

*What runs before either half is ruled.* FR-082's probe and SC-001a's survey — including the
standalone `advanceLifeOnly` arm that **measures `L`** rather than projecting it — run first; the
ladder is recomputed from the measurement; and the ruling is written back into the spec as
**SC-001b** before any baseline is checked in (FR-083). `kMaxVoices` stays at 8 while that survey
runs, so the whole {1, 2, 4, 6, 8} curve is measurable, and is lowered in the same commit as the
baseline.

### Q-B. OQ-3: the two unshipped `AtmosphereEngine` behaviours. **Recommendation: (A) + (C).**

**Ruled 2026-09-17: (A) + (C).** FR-017's configuration ships now; Phase 10a — AtmosphereEngine ghost
extension is added to the roadmap after Phase 10 and before Phase 14. T031 does not need to surface
this item again.

Ship FR-017's configuration now — it is complete, numeric and verified by SC-027 — and **name the
phase that owns reverse grains and event-triggered grain scheduling** rather than absorbing them here.

The reasons are cost and risk, not preference. Option (B) puts a source change to a *Seraphis-shipped*
Layer 3 component, with its own Seraphis-green gate in SC-016's shape, inside the one phase that is
already tight against its budget (S12) and already carries a second shared-component change
(`ContinuousBody`, AR-4). The substrate exists (`primitives/reverse_buffer.h`) and the component
already snapshots pitch, position and drift at grain birth (`atmosphere_engine.h:805-812`), so the work
is real but not large — it simply does not belong in the convergence phase.

**Concretely recommended:** a new **Phase 10a — AtmosphereEngine ghost extension**, sequenced after
Phase 10's budget ruling and before Phase 14's presets (so presets can use it), with roadmap line 114's
🔶 row re-pointed at it. If the user prefers (B), the plan additions are bounded and known: a per-grain
reverse flag read at birth, an event-trigger entry point beside the density scheduler, new FRs/SCs, a
Seraphis-green gate, and the CPU it adds inside the **global** stage (cheaper than it would have been
per voice, because OQ-1(b) already moved the tap).

### Q-C. FR-087 vs the Seraphis probe shape. **Recommendation: amend FR-087.**

**Ruled 2026-09-17: amend FR-087** to the friend-struct shape; T002 writes the amendment.

B-4 in full. FR-087 mandates a target-wide `KRATE_DSP_VORAGO_TEST_HOOKS` define "in the shape
`dsp/tests/CMakeLists.txt:547-555` uses". That define exists because `AetherReverb`'s hook is a
**member function** and the macro changes the class definition. A `detail` friend struct declared in
the header and **defined in the test TU** — which is precisely what Seraphis's own engine probe is
(`seraphis_engine.h:193-195` + `seraphis_nonfinite_test.cpp:107-111`) — changes no class definition,
needs no define, and creates no ODR obligation. The probe still does everything SC-029 requires,
including poisoning a voice's served audio so the **detection** path runs.

**Proposed amendment:** FR-087's second sentence becomes *"It is a `detail`-namespace friend struct
forward-declared in `vorago_engine.h` and defined in `vorago_nonfinite_test.cpp`, in the shape
`seraphis_engine.h:193-195` uses — no compile definition is added, because the class definition does
not change."* If the user declines, the macro shape is implemented instead and nothing else in this
plan moves.

### Q-D. FR-010 step 2 / FR-011 / FR-012 wording vs `BloomEngine`'s own contract.

B-1 in full, and this one is a **clarification of spec text rather than a design choice** — the shipped
component's doxygen decides it — but it is flagged because a reader comparing the spec to the code will
otherwise think the plan diverged. FR-011's "`count = getActivePartialCount()`" and FR-010 step 2's
"`returnedCount`" are the same number **only** when `capacity == activeCount`, and the count handed to
`processChunk` must be `reserveBase()` (not `activeCount`) or the component's overlap counter saturates
every chunk. The plan implements B-1's three lines; the spec text is worth one sentence of alignment at
the tasks stage so the compliance table is not arguing with itself.

---

## S14.1 Amendments and corrections this plan records

These are **not** open questions — each is a place where the spec's own text and the shipped code (or
the spec's own other text) disagree, the plan resolves the disagreement, and the resolution must be
written back at the tasks stage so the compliance table is not arguing with itself. **No threshold is
relaxed by any of them.**

| # | Where | What changes, and why |
|---|---|---|
| **A-1** | SC-008, the `Depth` row | The metric "reverb-return energy share … ratio of the wet path to the total" is **saturated at both endpoints**: `CavernVerb::kDefaultMix = 1.00f` (`cavern_verb.h:264`), `setMix`'s doxygen is "CavernVerb owns the mix; the owned engine is permanently fully wet" (`:746-749`), and the `Depth → CavernMix` row carries `amount = 0.0`, so the composed render is 100 % wet at Depth 0 and Depth 1 alike. Re-stated as an **absolute** measure that moves with size and decay at constant mix: the dB ratio of the composed render's RMS to the RMS of the same render with `CavernVerb::setMix(0)`, both over `[10 s, 60 s]`. Every `Depth` row is Cavern-owned, so the `setMix(0)` arm is identical at both endpoints and the ratio moves exactly as the return's energy moves. **≥ 6 dB endpoint threshold unchanged.** |
| **A-2** | SC-008, the `Mass` row | **No spec change.** The defect was in this plan's `kRows`: the `Mass → BodyBlend +0.25` row moved the blend toward body **B** while the metric integrates body **A**'s mode bands. The row is deleted (S7.3); the metric and its ≥ 4 dB threshold stand exactly as written. Recorded here so the comply stage can see the row's absence is deliberate. |
| **A-3** | FR-070 vs SC-014 | FR-070 asks for the `getAllocatedBytes()`-invariance "over an 8 h-equivalent accelerated render"; SC-014 says 10-minute-equivalent. **Neither is narrowed.** SC-014 is the per-push arm and carries the stronger `AllocationScope` detector (which cannot be an 8 h per-push cost, since FR-086 forbids scaling the sample clock); **SC-004b already asserts `getAllocatedBytes()` unchanged over the full unaccelerated 8 h soak**, which is literally the clause FR-070 names. Division of labour, recorded. |
| **A-4** | FR-014, "the **4-stage** `MultiStageEnvelope`" | Means **four pre-sustain stages** — attack plus three body stages, exactly the three numbers FR-014 pins. The shipped `kEnvelopeStages = 6` adds a 0 ms sustain-hold at `kEnvelopeSustainPoint = 4` and a 0 ms post-sustain at 5, both **required** by `advanceToNextStage()`'s contract (`seraphis_voice.h:565-579`, `multi_stage_envelope.h:215`) and neither adding a millisecond of envelope. (S3.8.) |
| **A-5** | FR-081's lever list | Gains **L-7 — lower `kMaxVoices`** as the ladder's first step (S12.4). It is the only lever that returns CPU (`L` per removed slot) at the cost of neither a deliverable nor a default, and it exists only because B-6's corrected solve makes the ceiling a budget number. Bounded below at the shipped polyphony; narrowing roadmap line 460's "4–8 voices" is Q-A's second half. |
| **A-6** | SC-002's stage list | Puts `atmosphere` in the **voice** column. That list predates OQ-1 ruling (b), which moved `AtmosphereEngine` to the engine (FR-002, FR-041, FR-056) — the relocation is recorded everywhere else in the spec. The stage probe measures it in the **engine** column (S10.3). |
| **A-7** | SC-016's opening, "After the FR-030 – FR-039b change" | **There is no FR-039b** — the FR-030 series ends at FR-039 (`spec.md:724`), and a repo-wide search for `FR-039b` finds only this one occurrence. Corrected to "FR-030 – FR-039". |
| **A-8** | SC-023 | Gains (i) the **matrix** TU its own text already demands ("voice, engine **and matrix**") as `VoragoMacro_UnpreparedAndDegenerate`, and (ii) a **degenerate sample-rate** arm — NaN (bit pattern through a `volatile`), 0.0 and 4000 Hz — which is the only place FR-076's "substituted, then floored at `kMinUsableSampleRate`" second sentence is asserted at all; SC-021a/b sweep 44.1–192 kHz only. |
| **A-9** | SC-019 clauses 1 and 3 | Gain the **`Partial`** family's read-backs, `HarmonicCloud::getMutation()` (`harmonic_cloud.h:493`) and `BloomEngine::getDepth()` (`bloom_engine.h:679`). FR-020's first destination row is `Partial → setMutation` **and** `setDepth`, and clause 3 gates "per family" over five families, so without them an implementation that never wired the `Partial` agents passes SC-019, SC-019a and SC-019b. Clause 2's arm reduces to "never below base" for that family, because no scheduler family writes those two targets. |
| **A-10** | SC-022 clause 2 | The smear-**enabled** arm is configured with `setSmearAmount(0)` and `setSmearDecoherence(0)` before the render. The documented **bit-identical bypass** is the *disabled* instance (`spectral_smear.h:15-16`); an enabled instance at the S8.3 defaults actively smears, so the ±1-sample onset-index equality is not a property a correct implementation has. At 0/0 the component is an **exact identity delayed by `fftSize`** — its own banner, `:13` — which is the arm the clause needs. |
| **A-11** | FR-087 | Carried separately as **Q-C** (the `detail` friend-struct probe shape, B-4). Listed here only so the amendment set is complete in one place. |
| **A-12** | FR-026 | Clarification, not a change: neither lane carries a voice-side depth factor, because both modulators scale by their own depth internally (`breathing_modulator.h:286`, `tidal_modulator.h:317`), and the tidal lane is published as a **net**, `max(0, ·)`, so a trough cannot pull fog below the engine's base. Both lanes become observable through `getBreathingGravityLane()` and `getTidalFogDepth()` (S2.4) — without which FR-026 has no assertion anywhere. |
| **A-13** | The spec's deliverable list and AR-4's neighbourhood | This phase makes **two** append-only shared-component changes, not one: `continuous_body.h` (six materials, AR-4) **and** `feedback_ecology.h` (one public `silenceAudio()`, B-7). The second is forced by FR-005/FR-072 meeting `FeedbackEcology::reset()`'s documented control-thread-only contract (`feedback_ecology.h:861-862`); it deletes no line, adds no name, and has no Seraphis consumer, so SC-016's gate does not widen. |

---

## S15. What the tasks stage inherits

* Nine new TUs, one new helper header, two new Node tools, three new headers, **five** edited library
  or test files (`continuous_body.h`, **`feedback_ecology.h`**, and the three Seraphis-owned test TUs),
  and the CMake list edits of S11.1 — all enumerated, none globbed.
* Four decisions carried to the user (S14), of which **Q-A blocks SC-001b and every checked-in
  baseline** and the other three do not block implementation.
* **Seven** facts (S0.4) the implementer must read before writing a line, of which **B-1 and B-2
  change observable behaviour** and **B-7 changes the shape of the voice's clearing paths and adds the
  phase's second shared-component change**.
* **Thirteen amendments and corrections (S14.1)** to be written back into the spec at the tasks stage,
  none of which relaxes a threshold.
* One measurement that must be taken before the phase can claim its budget (FR-082's probe, SC-001a's
  survey) and one that must be taken before it can claim its materials (SC-015, SC-025).
