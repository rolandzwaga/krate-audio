# Feature Specification: Vorago Phase 3 — Resonance Drift Network

**Spec slug:** `vorago-phase3-resonance-drift`
**Roadmap source:** `specs/Vorago-roadmap.md` → Part A → Phase 3 (lines 212–228); reuse-inventory row
`L3 Resonance Network` (line 111); ODR note (lines 127–129); cross-cutting constraints (lines 479–500),
including the **Dormancy** rule (lines 489–493) that names Phase 3 peaks explicitly.
**Layer:** one new Layer 3 component, `dsp/include/krate/dsp/systems/resonance_drift_network.h`.
No amendments to any shipped component are planned (see FR-013 for the one contingency).
**Test target:** `dsp_systems_tests` (enumerated source list, `dsp/tests/CMakeLists.txt:306-402`;
the `-fno-fast-math` block for the non-finite TU is `dsp/tests/CMakeLists.txt:780-800`).
**Depends on:** Vorago Phase 1 — nothing directly; Vorago Phase 2 — nothing directly. The wander
source is `BrownianDrift` (`processors/brownian_drift.h:94`, Seraphis Phase 1) and the audio engine is
`ResonatorBank` (`processors/resonator_bank.h:174`), both shipped and read this session.
**Plugin work:** none. The Vorago plugin starts at Phase 11; phases 1–10 are KrateDSP-only.

## Overview

The Resonance Drift Network is the stage that turns Vorago's two sound sources into a *place*.
Twelve resonant peaks sit across the spectrum and never hold still: each peak's centre frequency, Q,
gain and stereo position wander independently on a seconds-to-minutes Ornstein–Uhlenbeck walk, bounded
around an anchor — minute-scale correlation times being reachable only because FR-037 decimates the
lane clock, since a bare `BrownianDrift` tops out at a 30 s correlation time (`brownian_drift.h:99`).
Peaks sleep and wake. The engine itself is mono (one twelve-peak bank fed the mono sum of the two
stereo sources upstream), but each peak's gated output is placed in the stereo field by its own
wandering equal-power pan position before the dry path — carried stereo throughout — is crossfaded back
in (FR-003, FR-038, FR-044). The effect is a cave with standing waves that slowly change shape — the
same held note passing through it is coloured differently every minute, without anything ever
sweeping, cycling or repeating.

Almost none of this is new DSP. `ResonatorBank` already provides sixteen independently tunable
constant-peak bandpass resonators with per-resonator frequency, Q, gain and enable
(`resonator_bank.h:328/384/367/401`), and `BrownianDrift` already provides the bounded mean-reverting
walk (`brownian_drift.h:94`). What does not exist is the composition: a fixed peak table with three
anchor modes (free / keyed / hybrid-gravity), four wander lanes per peak (frequency, Q, gain and pan),
a per-peak sleep/wake gate that obeys the roadmap's Dormancy rule, and the write-ordering and smoothing
discipline that make a drifting biquad bank silent about its own retuning.

Two composition facts are load-bearing and were established by reading the shipped headers rather than
assumed, because both would otherwise be discovered at implementation time as silent defects.
**(1) The engine is twelve single-resonator `ResonatorBank` instances, not one twelve-resonator bank**
— `process(float)` returns only the summed output and there is no per-resonator accessor
(`resonator_bank.h:470-516`), so a shared bank cannot give each peak the per-sample gate the roadmap's
Dormancy rule demands (FR-011). **(2) A frequency write clobbers Q** —
`qValues_[index] = rt60ToQ(frequencies_[index], decays_[index])` (`:333`) off a decay table this
network never writes — so the Q write is exempt from change detection and always follows a frequency
write (FR-014, FR-015), and SC-017 measures the *realised* bandwidth rather than the network's own
echo of what it meant to set.

**Nothing in this phase re-implements existing DSP.** Every "exists" claim below carries a
`file:line` citation from a header opened in this session, including the two rejections (the SIMD
modal bank and the sympathetic-resonance system) — both were read before being ruled out. The ODR
sweep was run this session and its full output is recorded.

## Scope

In scope:

- One new Layer 3 component, `ResonanceDriftNetwork`, at
  `dsp/include/krate/dsp/systems/resonance_drift_network.h` (roadmap line 217).
- **Twelve** resonant peaks (roadmap line 219), each composed from its own shipped `ResonatorBank`
  instance by default (FR-011), with per-peak `BrownianDrift` on **frequency, Q, gain and stereo pan**
  (roadmap lines 219–220 for the first three; pan is FR-038, minted in the Phase 3 clarification pass —
  see Clarifications Q4). A probe-gated, purely-additive amendment to `ResonatorBank` (FR-013) is the
  pre-approved response if FR-060's stage probe shows the twelve-bank composition is too costly —
  see Clarifications Q3.
- **Stereo I/O with a mono engine.** `processBlock` takes and produces independent left/right channels
  (FR-003); the engine renders the mono sum of the two input channels through one bank per peak, and
  each peak's gated mono output is placed in the stereo field by its own wandering equal-power pan
  position (FR-038) before being summed, normalised, trimmed (FR-045) and crossfaded against the
  unmodified stereo dry path (FR-044) — see Clarifications Q4.
- **A network-owned static wet-level trim** (FR-045): `setWetGain`/`getWetGain`, so `mix` is a usable
  blend across its whole range rather than a near-mute below `mix ≈ 0.95` — see Clarifications Q2.
- **Three anchor modes** — free, keyed, hybrid (roadmap lines 221–222) — where hybrid reuses the
  `HarmonicCloud` spectral-gravity *concept* (pull toward/away from a harmonic grid), not its code, and
  is **index-paired**: peak *i* in Hybrid mode moves only between its own Free anchor and its own Keyed
  anchor (FR-023) — see Clarifications Q1.
- **Per-peak life cycle** (roadmap lines 223–224): peaks sleep and wake, driven by an external scalar
  so an externally owned `SlowEventScheduler` can hook them, obeying the roadmap's Dormancy rule
  (lines 489–493) — gain at zero means the peak's filter is skipped **and its state cleared** so the
  peak wakes silent rather than releasing seconds of stored ring (FR-042), its lanes keep running, and
  re-entry is a 50 ms per-sample ramp on the peak's own output (FR-041, FR-044).
- Unit tests covering the roadmap's four Phase-3 success criteria (lines 227–228: stability at max Q
  under sustained input, no zipper under drift, wander-rate spectral tests, CPU ≤ 0.75 % per voice)
  plus the cross-cutting gates from roadmap lines 479–500 (boundedness soak, seed determinism,
  sample-rate change, zero allocation, layer/ODR lints, portability, no bit-exact goldens).

## Non-Goals (owned by later phases)

- **Anything that instantiates the network.** `VoragoVoice` (Phase 10) decides where the network sits
  in the voice chain, how many peaks a voice runs, and what drives its wake hooks. This phase produces
  a block of audio and a control surface; nothing drives it yet.
- **Owning a `SlowEventScheduler`.** The roadmap says peak sleep/wake is "event-hookable" (line 224),
  not that the network schedules events. Following the Phase 2 precedent
  (`noise_organism.h:841-843`: "A plain scalar input, not a scheduler reference"), the network exposes
  `setPeakWake(index, amount)` and the caller writes the scheduler's envelope value into it.
- **Modulation routing.** `ResonanceDriftNetwork` is not a `ModulationSource` and adds no `ModSource`
  enumerator. Vorago Phase 1 established that implementing the `ModulationSource` ABC
  (`core/modulation_source.h:31`) does not make a source routable — `ModulationEngine` dispatches
  through a fixed switch. The network **owns** its wander lanes internally, following the
  `HarmonicCloud` precedent (private drift lanes, `harmonic_cloud.h:1863-1964`) that `NoiseOrganism`
  reaffirmed.
- **Stereo processing beyond per-peak equal-power pan.** The engine itself is mono (one bank per peak,
  fed the mono sum of the two input channels) and the dry path is carried stereo unchanged; the only
  stereo placement this phase owns is each peak's own wandering equal-power pan position (FR-038,
  FR-044). Anything beyond that — correlated stereo reverberant width, mid-side processing, cross-
  channel effects — is Phase 9's cavern space engine or a Phase 10 amendment. See "Decisions taken
  where the roadmap is silent", D-3 (superseded by Clarifications Q4), for the derivation.
- **Comb filters.** The reuse-inventory row (line 111) lists `timevar_comb_bank`; combs are the
  *noise organism's* per-source chain (Phase 2 FR-050 series) and are deliberately not part of this
  component — see the Existing-components table, row `TimeVaryingCombBank`.
- **Feeding the network's output back anywhere.** Cross-coupling between resonances is Phase 5
  (`FeedbackEcology`). Every peak here is strictly parallel and forward-only.
- **Ecosystem control.** Phase 8 will drive peak gains from agent energy through this same
  `setPeakWake`/`setPeakLevel` surface. No hooks beyond those setters are added speculatively.

## Existing components (verified this session)

Every row below was opened and read in this session; signatures are quoted from the file.

| Component | Header (verified) | What Phase 3 reuses / relies on |
|---|---|---|
| `ResonatorBank` (L2) | `processors/resonator_bank.h:174` | **The audio engine.** `prepare(double) :184`, `reset() :213`, `setCustomFrequencies(const float*, size_t) :295`, `setFrequency(size_t,float) :328`, `setDecay(size_t,float) :348`, `setGain(size_t,float dB) :367`, `setQ(size_t,float) :384`, `setEnabled(size_t,bool) :401`, `setDamping(float) :421`, `setExciterMix(float) :432`, `setSpectralTilt(float) :443`, `process(float) :470`, `processBlock(float*,size_t) :522`, `getNumActiveResonators() :319`. Constants: `kMaxResonators = 16` (`:39`), `kMinResonatorFrequency = 20.0f` (`:42`), `kMaxResonatorFrequencyRatio = 0.45f` (`:45`), `kMinResonatorQ/kMaxResonatorQ = 0.1/100` (`:48,51`), `kMinDecayTime/kMaxDecayTime = 0.001/30` (`:54,57`), `kResonatorSmoothingTimeMs = 20.0f` (`:69`). **Seven verified facts this spec is built on:** (a) the per-sample loop skips disabled slots — `for (i < kMaxResonators) { if (!enabled_[i]) continue; }` (`:487-488`) — the in-bank half of what makes the Dormancy rule cheap here; the other half is that FR-042 stops calling the slept peak's bank at all, since under FR-011's per-peak composition an entered-but-idle bank would still run its three global smoothers per sample (`:474-476`); (b) each resonator is an RBJ **constant-0 dB-peak bandpass**, coefficients computed in `updateFilterCoefficients` (`:560-591`) with `alpha = sin(omega)/(2Q)`, so changing Q does **not** change peak height — gain and Q are genuinely independent controls (FR-031); (c) `setFrequency` **re-derives Q from the stored decay** — `qValues_[index] = rt60ToQ(frequencies_[index], decays_[index])` (`:333`, the Vorago Phase 2 FR-099 fix) — so a frequency write silently overwrites a previous `setQ`; FR-014 fixes the write order because of this; (d) coefficients are **hard-swapped**, no interpolation (`:591`), which is the entire subject of SC-002; (e) `process(float)` returns only the **summed** wet output (`:510-514`) and there is **no per-resonator output accessor** anywhere in the class (swept this session: the only writes to `filterOutput` are `:498-510`, all consumed by `wetSum`). The only per-peak gain surface is `setGain(size_t, float dB)` (`:367`), a control-rate write consumed inside the loop as `filterOutput *= gains_[i]` (`:504`) with **no smoother**. A single shared bank therefore **cannot** give each peak a per-sample gate — that verified fact, not preference, is why FR-011 owns **twelve single-resonator banks** rather than one twelve-resonator bank (see FR-011 for the derivation and FR-060 (a) for its cost). Base gain still moves on the control grid with a bounded per-step delta (FR-033); the per-sample gate is the network's own linear multiply (FR-041/FR-044); (f) `reset()` is a **configuration wipe**, not a state clear: it sets every resonator to 440 Hz, `kDefaultDecayTime`, unity gain, `kDefaultResonatorQ` and **`enabled_[i] = false`** (`:226-231`), and the header says so — "User must reconfigure tuning after calling reset()" (`:212`) — which is why FR-004 mandates a re-apply. It *does* clear every filter's biquad state first (`:214-217`, `filter.reset()`), and that is the **only** public path to a state clear: there is no state-only method. FR-042 therefore uses `reset()` + immediate re-apply as the per-peak state clear on the sleep edge, and it is cheap precisely because FR-011 gives each peak its own bank; (g) `calculateTiltGain` early-returns `1.0f` when tilt is exactly `0.0f` (`:121-123`) and otherwise costs a `std::log2` **plus** a `dbToGain` per resonator per sample (`:124-125`, called at `:507`), so FR-016 pins tilt at 0. `rt60ToQ` is `(π·f·RT60)/ln1000` clamped to `[0.1, 100]` (`:92-99`): Q saturates at 100 for every `f·RT60 > 219.87` (FR-032). `exciterMix_` defaults to `0.0f` (`:626`, re-zeroed at `:235`) which in `output = input*mix + wetSum*(1-mix)` (`:514`) means **fully wet** — a bank with nothing enabled emits silence, not bypass (FR-043). |
| `BrownianDrift` (L2) | `processors/brownian_drift.h:94` | **The wander lane**, instantiated three times per peak. `prepare(double) :121`, `reset() :133`, `setSeed(uint32_t) :145`, `setSmoothness(float) :152`, `setDepth(float) :159`, `setMean(float) :165`, `process() :178`, `processBlock(size_t) :194`, `getCurrentValue() override :212`, `getSourceRange() override :217`. `kTauMin/kTauMax = 0.2/30.0` s (`:97,99`), `kInternalStd = 0.5f` (`:101`), `kDriftOutputSmoothMs = 150.0f` (`:103`), `kControlRateInterval = 32` (`:105`). Ornstein–Uhlenbeck with mean reversion, a hard `kWalkLimit = 4.0f` divergence guard (`:226`) and an output clamped to `[-1, +1]` (`:212-214`) — bounded by construction, which is why FR-030's frequency bounds are cheap to guarantee. **Load-bearing range fact:** `setSmoothness` clamps to `[0, 1]` (`:152-156`) and the tau it selects is capped at `kTauMax = 30 s` (`:99`), so **the longest correlation time one lane can express is 30 s** — a bare `tau = 1/rate` mapping saturates at and below `1/30 = 0.0333 Hz`, which is *below* this spec's own 0.03 Hz default. FR-037's decimation exists solely to reach the roadmap's minute-scale time constants (roadmap line 31) through this lane without amending it. **Load-bearing cost fact:** `processBlock` advances the output smoother via `OnePoleSmoother::advanceSamples` (`:204`), which costs one `std::pow` per call (`primitives/smoother.h:243-255`) unless the smoother has converged (`isComplete()` early-return, `:244`). A 64-sample control chunk straddles two 32-sample lane steps, so 36 lanes cost up to **two `std::pow` each per 64 samples = 54 000 `std::pow`/s at 48 kHz** — the dominant uncertainty in the SC-004 projection and the reason FR-060 requires a stage probe before implementation. Not modified. |
| `ModalResonatorBank` + `processModalBankSampleSIMD` (L2) | `processors/modal_resonator_bank.h:71`, `processors/modal_resonator_bank_simd.h:36` | **Read and rejected as the engine, despite the roadmap's "SIMD path preferred" (line 219).** Per-mode frequency and amplitude *are* settable — `updateModes(const float* frequencies, const float* amplitudes, int numPartials, DampingLaw damping, float stretch, float scatter)` (`:294-303`), state-preserving, with `snapCoefficients()` (`:342`) for a caller on its own control grid. But **per-mode Q is not expressible**: the decay radius comes from a *global* two-term damping law, `radiusTarget_[k] = exp(-(b1 + b3·f_w²)/fs)` (`updateDampingLaw`, `:356-372`; `struct DampingLaw { float b1; float b3; }`, `:111-118`), so every mode's Q is a fixed function of its frequency. There is no per-mode decay setter and `getRadiusTargets()` (`:339`) is a `const float*` read view. Roadmap line 220 requires Q to wander **per peak**, independently of frequency; that is unreachable here without a shared-component amendment that would touch Membrum, Innexus and Seraphis. Rejected on that ground alone, recorded so the deviation from "SIMD preferred" is evidenced rather than assumed. |
| `processSympatheticBankSIMD` (L3 kernel) | `systems/sympathetic_resonance_simd.h:39` | **The named fallback** if FR-060's probe shows the scalar path is too expensive (FR-013). `void processSympatheticBankSIMD(float* y1s, float* y2s, const float* coeffs, const float* rSquareds, const float* gains, int count, float scaledInput, float* sums, float releaseCoeff, float* envelopes) noexcept` — a Highway-dispatched batch of **independent** driven two-pole resonators, `y[n] = coeff·y[n-1] − r²·y[n-2] + scaledInput·gain` (`:25`), with **per-resonator** `coeff = 2r·cos(ω)`, `r²` and input `gain` (`:31-33`): per-peak frequency, Q *and* gain, vectorised 4- or 8-wide. **`sums` is a single accumulator, not a per-resonator array** (`:37`, "Out: pointer to accumulated output sum"); per-peak outputs come from the state array instead — `y1s[i]` is `y[n]` for resonator *i* after the call (`:29-30`) — which is what makes FR-011's per-peak gate expressible on this path too (FR-013). Three consequences if taken: the caller owns coefficient derivation and peak-gain normalisation (an all-pole resonator's peak magnitude scales with `1/(1−r)`, unlike `ResonatorBank`'s constant-0 dB-peak form), the kernel's per-resonator envelope follower runs unconditionally; and the FR-041 gate must be applied to the read-back `y1s[i]` *outside* the recurrence rather than through `gains`, which multiplies the **input** (`:26`) and so would change the filter's state rather than scale its output. Same layer as this component, so the include is legal (`systems/continuous_body.h:42` precedent; `tools/lint-layers.js` fails only on reaching *up*). |
| `SympatheticResonance` (L3) | `systems/sympathetic_resonance.h:96` | **Read and rejected as a component** (the roadmap row names it, line 111). Its pool is driven by note lifecycle — `noteOn`/`noteOff` with `SympatheticPartialInfo` frequency tables (`:71-74`) — and its Q is **global**: `setDecay(float)` maps one normalised value to `userQ_ ∈ [100, 1000]` for all resonators (`:165-169`), with `setAmount` (`:152-163`) a single coupling gain. No per-resonator frequency, Q or gain surface exists. Only its SIMD kernel (row above) is reusable. |
| `TimeVaryingCombBank` (L3) | `systems/timevar_comb_bank.h:81` | **Listed in the reuse row (line 111), deliberately unused.** `prepare(double, float maxDelayMs = 50.0f) :154`, `setNumCombs(size_t) :178`, `setCombDelay(size_t,float) :189`, `process(float) :328`; `kMaxCombs = 8` (`:88`). It is the Phase 2 noise chain's second stage and models a comb, not a resonant peak; adding it here would duplicate a stage the signal has already passed through in the voice chain. Recorded so a later reader does not read its absence as an oversight. |
| `IResonator` (L2 interface) | `processors/iresonator.h:32` | **Read and not implemented.** It models one resonator with a single `setFrequency(float f0)`, `setDecay(float t60)`, `setBrightness(float)` and `process(float)` (`:42-55`) plus energy followers; a twelve-peak bank has no single `f0` and no single `t60`. `ResonatorBank` itself does not implement it either. |
| `HarmonicCloud` (L3) | `systems/harmonic_cloud.h:127` | **Concept source and shape template, not a dependency** — this component does not include it. The gravity concept the roadmap points at (line 222) is `void setSpectralGravity(float g) noexcept` (`:478`), documented at `:464-477` as "pulls partial ratios toward/away from the harmonic grid, `[-1, +1]`", implemented as `ratio_g(n) = pow(n, 1 + g·kGravityExponentRange)` with `kGravityExponentRange = 0.1f` (`:198`, law at `:1284` and `:1329`), `g = 0` being exactly the untouched grid. FR-023 defines the network's own hybrid law in the same spirit (log-domain pull, identity at `g = 0`) but **not** the same formula — that formula warps a *harmonic index*, and this component's free anchors are arbitrary Hz values with no index. `kControlChunkSamples = 64` (`:144`) is the shared control clock FR-007 adopts; `processStereoBlock` (`:878-891`) is the guard ladder FR-003 copies. |
| `NoiseOrganism` (L3) | `systems/noise_organism.h:136` | **The shape template**, and the component this one sits after in the voice chain. Copied wholesale: `struct PrepareConfig` as the prepare-time sizing surface (`:190-194`), `kControlChunkSamples = 64` (`:150`), `kGainRampMs = 50.0f` (`:178`) with `LinearRamp` gates configured per slot (`:291-294`), `kOutputClamp = 4.0f` (`:180`) with a `clampEngagements_` counter (`:277`, read at `:992`), the compile-time salt table with overflow `static_assert`s (`:1018-1042`), `gateSteady()` (`:1669-1677`) returning `0.0f` for a dormant *or* out-of-count slot, `setSourceDormant`/`setSourceWake` (`:833`, `:844`) as plain scalars, `getAllocatedBytes()` (`:999`), and the "out-of-range slot returns the documented neutral" read-surface rule (`:855-859`). Its own resonator lanes (`setResonatorWander`, `:686`; `setResonatorQWander`, `:703`) are the **per-source chain** inside one noise slot — 2–4 resonators tied to that slot's noise — and are a different object from this phase's twelve bus-wide peaks; the two are not redundant (D-6). |
| `SlowEventScheduler` (L2) | `processors/slow_event_scheduler.h:143` | **Not owned by this component** — read to fix the wake-hook contract (FR-040 series). `struct Event { std::uint8_t target; float depth; std::int8_t polarity; }` (`:187-191`), `kNoTarget = 0xFFu` (`:150`), `kMaxTargets = 16u` (`:152`), `getActiveTarget() :356`, `isEventActive() :361`, `getEnvelopeValue() :391` (unipolar `[0,1]`), `getActiveDepth() :397`, `getActivePolarity() :400`; default interval range 20–90 s (`:164-165`) and default envelope 5 s attack / 3 s hold / 8 s release (`:166-168`). `kMaxTargets = 16 ≥ 12`, so one scheduler can address every peak — the reason FR-040 uses a peak index in `[0, 11]` as the target id. |
| `OnePoleSmoother` / `LinearRamp` (L1) | `primitives/smoother.h:134` / `:305` | Gate and parameter ramps. `configure(float ms, float sampleRate)` (`:160` / `:329`), `setTarget` (`:170` / `:342`), `process()` (`:197` / `:370`), `snapTo` (`:263` / `:421`), `getCurrentValue` (`:191` / `:364`). `OnePoleSmoother::advanceSamples(size_t)` (`:243-255`) is O(1) but costs a `std::pow` per call — the SC-004 cost term quoted in the `BrownianDrift` row. `LinearRamp` gives the exact-duration 0→100 % ramp `kGainRampMs` needs (`noise_organism.h:494-497` states the same reason). |
| `Xorshift32` / `deriveStreamSeed` (L0) | `core/random.h:41` / `:102` | Per-lane seed derivation. `constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept` (`:102-103`) — lowbias32 finaliser with a guaranteed-non-zero result, load-bearing because `Xorshift32::seed()` silently substitutes its default for 0 (`:73-74`), so two lanes hashing to 0 would collapse onto one stream. |
| `detail::isNaN` / `detail::isInf` (L0) | `core/db_utils.h:99` / `:260` | The `-ffast-math`-proof finiteness test (bit pattern behind an opaque barrier, `:250-259`). FR-008 forbids `std::isnan`/`std::isinf`. |
| `ModulationSource` (L0) | `core/modulation_source.h:31` | Read only to confirm the network does **not** implement it (Non-Goals): the ABC declares `getCurrentValue()` and `getSourceRange()` (`:36-41`), neither of which has a meaning for a twelve-peak audio processor. |
| Perf-test idiom | `dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp:22-70`; `noise_organism_perf_test.cpp:1-90`, `:201-272`, `:1409-1562` | The measurement basis SC-004 inherits: **ns per 512-sample block at 48 kHz**, best-of-25 × 500 blocks after 400 warm-up blocks, gated against a checked-in baseline × 1.5, with `static_assert(kBaseline * kRegressionFactor <= kBudgetNs)` (ceiling) and `static_assert(kBaseline >= kBudgetNs / 50.0)` (anti-no-op floor) binding the absolute figure at compile time. Also the source of the "IF A MEASUREMENT IS OVER BUDGET: REDUCE COST, NEVER RAISE THE BASELINE" rule and of the stop-and-surface protocol FR-060 reuses. |
| Ring-out harness | `dsp/tests/unit/systems/continuous_body_test.cpp:3339-3400` (`ContinuousBody_DecaysToSilence`), tracing to `plugins/membrum/tests/unit/processor/test_kit_switch_infinite_ring.cpp:59` | The infinite-ring pattern roadmap line 227 names: drive for 60 s, stop, require the tail below **−80 dBFS** (`1.0e-4`) inside a bound **computed from the shipped constants**, not transcribed. SC-001 restates it with `RT60 = Q·ln1000/(π·f)`. |
| Test helpers | `tests/test_helpers/` | `render_fingerprint.h` (`kSampleTolerance = 5.0e-4f` `:58`, `kMetricTolerance`) for SC-010/SC-013; `allocation_detector.h:48 AllocationDetector` / `:111 AllocationScope` for SC-005; `audio_features.h:23 AudioFeatures` (`peakDbfs`, `rmsDbfs`, `centroidHz`, five band fractions) and `:37 extractAudioFeatures` for SC-001/SC-003; `statistical_utils.h:41-140` for window statistics; `spectral_analysis.h` for the SC-003 band trajectories. |

## New components

ODR sweep run **this session** over `dsp/`, `plugins/` and `tools/` with
`grep -rn "\(class\|struct\|enum class\|using\) <Name>\b"`:

| Class / symbol | Layer | Header path | ODR sweep result |
|---|---|---|---|
| `ResonanceDriftNetwork` | 3 | `dsp/include/krate/dsp/systems/resonance_drift_network.h` (new) | **0 hits.** Also swept and clean (0 hits each): `ResonanceNetwork`, `ResonanceDrift`, `DriftNetwork`, `ResonancePeak`, `DriftPeak`, `PeakAnchor`, `PeakState`, `PeakLifecycle`, `ResonancePeakState`, `ResonanceMode`, `ResonanceDriftConfig`. Near-name hazards that **do** exist and must not be shadowed: `ResonatorBank` (`processors/resonator_bank.h:174`), `ModalResonatorBank` (`processors/modal_resonator_bank.h:71`), `SympatheticResonance` (`systems/sympathetic_resonance.h:96`). None collide. |
| `ResonanceDriftNetwork::AnchorMode` (nested enum class) | 3 | same header | **0 hits** for `AnchorMode`. Nested regardless, so no new top-level name is claimed. Deliberately **not** named `TuningMode`: that name exists (`resonator_bank.h:133`, `enum class TuningMode : uint8_t { Harmonic, Inharmonic, Custom }`) in the same namespace and this component includes that header — a same-name enum would be an outright ODR violation, and the roadmap's own hazard list (line 128) warns about exactly this class of near-name. |
| `ResonanceDriftNetwork::PrepareConfig` (nested struct) | 3 | same header | Nested, following `NoiseOrganism::PrepareConfig` (`noise_organism.h:190`) and `AtmosphereEngine::PrepareConfig` (`atmosphere_engine.h:369`). Three unrelated nested `PrepareConfig` structs already coexist (`aether_reverb.h:1577`, `atmosphere_engine.h:369`, `noise_organism.h:190`), so nesting is the established, collision-free form. |
| `ResonanceDriftNetwork::Peak` (nested POD) | 3 | same header | `Peak` as a top-level name is **not** free — `struct Peak` exists twice in test translation units (`dsp/tests/unit/systems/seraphis_macro_test.cpp:841`, `plugins/seraphis/tests/integration/macro_wiring_test.cpp:821`), both file-local. Nesting inside the class removes the question entirely (the `SlowEventScheduler::Event` precedent, `slow_event_scheduler.h:187-191`). |

No new free functions, no new enumerators on existing enums, no new `ModSource` values.
`tools/lint-odr.js` and `tools/lint-layers.js` must pass on the result (SC-012).

## Functional Requirements

### FR-001 series — Component contract and lifecycle

- **FR-001** — `ResonanceDriftNetwork` is a Layer 3 class in `namespace Krate::DSP`, declared in
  `dsp/include/krate/dsp/systems/resonance_drift_network.h`, header-only, with a
  `// Layer: 3 (Systems)` banner matching `systems/timevar_comb_bank.h:8`. Its includes reach only
  Layers 0–2 (`processors/resonator_bank.h`, `processors/brownian_drift.h`, `primitives/smoother.h`,
  `core/random.h`, `core/db_utils.h`); any same-layer include added under FR-013 carries the
  `continuous_body.h:42` justification comment. Trace: roadmap line 217.
- **FR-002** — `void prepare(double sampleRate, const PrepareConfig& config) noexcept` is the only
  method allowed to allocate. `PrepareConfig` (nested, `NoiseOrganism::PrepareConfig` shape,
  `noise_organism.h:190-194`) carries `std::size_t maxBlockSamples` (default 2048, clamped
  `[64, 8192]`) and `std::size_t numPeaks` (default `kMaxPeaks`, clamped `[1, kMaxPeaks]`).
  `sampleRate` is floored at `kMinUsableSampleRate = 8000.0` Hz — **not** at 1 Hz, because at 1 Hz the
  derived clamp pair `[kMinResonatorFrequency, 0.45·fs]` inverts and `std::clamp` becomes UB; the
  Edge Cases section states the derivation in full. `prepare` is the only method *allowed* to
  allocate, and under FR-062 it in fact allocates nothing at all. Re-preparing is legal, fully
  re-initialises, and is the **only** path back to the FR-016 defaults.
- **FR-003** — `void processBlock(const float* inL, const float* inR, float* outL, float* outR,
  std::size_t numSamples) noexcept` renders **stereo** audio and **overwrites** `outL`/`outR` (it does
  not accumulate). **Corrected premise (Clarifications Q4):** the network is stereo in, stereo out —
  not mono, as an earlier draft of this spec assumed. Both `HarmonicCloud::processStereoBlock`
  (`harmonic_cloud.h:878`) and `ContinuousBody::processStereoBlock` (`continuous_body.h:1369`), the two
  stages the roadmap's chain places immediately before this one, are stereo, so a mono network would
  force an undocumented mono-sum somewhere in Phase 10. Internally the engine is mono — one bank per
  peak, fed `0.5·(inL[n] + inR[n])` (FR-011, FR-044) — but the component's public render boundary is
  stereo throughout: the dry path is the sanitised stereo input carried unchanged (FR-009, FR-043), and
  each peak's mono engine output is placed in the field by its own wandering equal-power pan (FR-038)
  before the twelve peaks are summed into a stereo wet signal (FR-044).
  In-place operation (`inL == outL` and/or `inR == outR`) is legal and produces the same result as
  distinct buffers; `outL`/`outR` may alias each other's *opposite* input (`inL == outR`) with the same
  guarantee — the implementation must capture both dry samples before any bank write. Guard ladder, in
  order, copied from `harmonic_cloud.h:880-891` (already a stereo guard ladder) and
  `noise_organism.h:414-430`: **any** of the four pointers null ⇒ **nothing is written on either
  channel and no state advances**; `numSamples == 0` ⇒ no-op consuming no control step; not prepared ⇒
  `std::fill_n(outL, numSamples, 0.0f)` and `std::fill_n(outR, numSamples, 0.0f)` and no state advance.
  Any `numSamples` is legal, including values far above the control chunk and above `maxBlockSamples`.
- **FR-004** — `void reset() noexcept` is **configuration-preserving**: it clears audio state (every
  resonator's biquad state, the gate and mix ramps, the dry path), rewinds every wander lane via
  `BrownianDrift::reset()` (`brownian_drift.h:133`), and then **mandatorily re-applies the network's
  current configuration** to every one of the twelve per-peak `ResonatorBank` instances (FR-011), in
  the FR-014 write order. The re-apply is load-bearing, not an optimisation:
  `ResonatorBank::reset()` is a configuration wipe leaving every resonator at 440 Hz, default Q and
  **`enabled_[i] = false`** (`resonator_bank.h:226-231`), so a forwarded `reset()` without re-apply
  renders **silence**. Phase 10 calls `reset()` on note-off and voice-steal against a fully configured
  network. **`reset()`'s meaning is unchanged by the Phase 3 clarification pass and stays identical to
  `NoiseOrganism::reset()`** (Clarifications Q5), so Phase 10 can call `reset()` on both siblings at the
  same moments. FR-046 adds a separate, lighter `clearAudioState()` for the freewheeling-lanes case;
  it is a different method with different behaviour, not an alternate meaning of `reset()`.
- **FR-005** — `void setSeed(std::uint32_t seed) noexcept` seeds the whole network. Every lane
  receives `deriveStreamSeed(seed, salt)` (`core/random.h:102`) with a unique, documented, stable salt
  from a compile-time table with `static_assert` overflow guards (`noise_organism.h:1018-1042`
  pattern). The seeded lanes are exhaustively:

  | Lane kind | Count | Salt base | Seeded via |
  |---|---|---|---|
  | per-peak frequency `BrownianDrift` | `kMaxPeaks` | `kSaltFreqLane = 0` | `setSeed` (`brownian_drift.h:145`) |
  | per-peak Q `BrownianDrift` | `kMaxPeaks` | `kSaltQLane = 16` | `setSeed` (`brownian_drift.h:145`) |
  | per-peak gain `BrownianDrift` | `kMaxPeaks` | `kSaltGainLane = 32` | `setSeed` (`brownian_drift.h:145`) |
  | per-peak pan `BrownianDrift` (FR-038, Clarifications Q4) | `kMaxPeaks` | `kSaltPanLane = 48` | `setSeed` (`brownian_drift.h:145`) |
  | per-peak default pan position (FR-038, one-shot draw, not a `BrownianDrift` lane) | `kMaxPeaks` | `kSaltPanPosition = 64` | `Xorshift32` one-shot draw at `prepare`/`setSeed` |
  | (next free) | — | `kSaltNextFree = 80` | — |

  Total wander lanes: **48** (four per peak × twelve peaks), up from the three per peak (36 total) an
  earlier draft of this spec carried before the Phase 3 clarification pass added the pan lane
  (Clarifications Q4).

  A `seed` of 0 is legal (`Xorshift32` and `deriveStreamSeed` both guarantee non-zero streams).
  Trace: roadmap lines 219–220 (peaks wander *independently*) plus the determinism practice the
  roadmap sets for every stochastic component — Phase 1's scheduler is "Deterministic under seed;
  RT-safe" (line 171) and Phase 8's ecosystem "Deterministic under seed for golden testing"
  (line 342).
- **FR-006** — The render path is real-time safe: `noexcept`, no allocation, no lock, no exception,
  no I/O, no `std::function`, no virtual dispatch on the per-sample path. Trace: roadmap line 480.
- **FR-007** — Control-rate work runs on a **64-sample absolute grid** (`kControlChunkSamples = 64`,
  matching `harmonic_cloud.h:144`, `continuous_body.h:96` and `noise_organism.h:150`). A control chunk
  split by a block boundary (e.g. 36 + 28) produces the same control step as an unsplit 64: **block
  size must not change the output** (SC-010).

  **Recorded interpretation of the roadmap's "per-block coefficient smoothing" (line 227).** The
  roadmap names a mechanism this spec deliberately does not implement, so the reinterpretation is
  written down rather than left silent (the FR-012 precedent for the other roadmap deviation). The
  phrase is read as *"coefficients are recomputed on a per-block control grid, not per sample"* — which
  is exactly FR-007. Coefficient **interpolation** is not implemented, because `ResonatorBank`
  hard-swaps: `updateFilterCoefficients` ends in `filters_[index].setCoefficients(coeffs)`
  (`resonator_bank.h:591`) with no crossfade or target smoother, and adding one would mean amending a
  shipped component (SC-013 forbids it) or re-implementing its biquad. The enforcing measurement for
  the property the roadmap actually wants — *no zipper* — is SC-002's boundary/interior discontinuity
  ratio together with SC-002 (c)'s injection arm and FR-035's slew ceiling, not a smoothing mechanism.
  If SC-002 cannot be met on the drift rates this spec admits, coefficient interpolation becomes a
  user decision under FR-060's stop-and-surface rule, never a quiet threshold relaxation.
- **FR-008** — Every value written into `ResonatorBank` is finite and inside that component's
  documented range before the call. Non-finite input to a setter is rejected (the setter is a no-op
  and the previous value stands); non-finite lane output — impossible by construction
  (`brownian_drift.h:212-214` clamps to `[-1,+1]`), guarded anyway — is replaced by the lane's neutral
  value. Finiteness is tested with `detail::isNaN`/`detail::isInf` (`core/db_utils.h:99`, `:260`),
  never with `std::isnan`/`std::isinf` (macOS CI builds `-ffast-math`). Trace: roadmap line 495.
- **FR-009** — Non-finite **audio input** cannot poison the network: each input sample, **on both
  channels independently**, is sanitised (non-finite replaced by `0.0f`) before it reaches the mono
  engine sum, any resonator state or the stereo dry path, so a NaN/Inf burst on either channel leaves
  both output channels finite throughout and finite input renders normally on the very next sample.

### FR-010 series — Peaks and the audio engine

- **FR-010** — `static constexpr std::size_t kMaxPeaks = 12` (roadmap line 219). Peak storage is a
  fixed `std::array`; the active count is `PrepareConfig::numPeaks`, changeable at runtime by
  `void setNumPeaks(std::size_t n) noexcept` clamped to `[1, kMaxPeaks]`. Reducing the count silences
  the dropped peaks through the FR-041 gate ramp, never abruptly, and never reallocates.
  `1 ≤ kMaxPeaks = 12` and `1 ≤ ResonatorBank::kMaxResonators = 16` (`resonator_bank.h:39`) are
  asserted at compile time; within each per-peak bank (FR-011) only slot `0` is ever enabled and slots
  `[1, 16)` stay permanently disabled and cost one predicted branch each (`:487-488`).
- **FR-011** — The audio engine is **`kMaxPeaks` separate `ResonatorBank` instances, one per peak**,
  each with resonator slot `0` and only slot `0` configured and enabled. Peak *i*'s sample is
  `bank_[i].process(x)`, taken **individually** and multiplied by that peak's per-sample gate (FR-041,
  FR-044) *before* the network sums the twelve results and applies the FR-019 normalisation. Peaks are
  strictly parallel and forward-only.

  **Why not one twelve-resonator bank (the shape this spec carried before review).**
  `ResonatorBank::process(float)` returns only the summed wet output (`resonator_bank.h:470-516`) and
  the class exposes **no per-resonator output accessor**; the only per-peak gain surface is the
  control-rate `setGain(size_t, float dB)` (`:367`). With a single shared bank the finest achievable
  gate step is therefore one FR-007 control chunk: a 50 ms ramp at 48 kHz spans 2400 samples = 37.5
  chunks, so the smallest step the gate can take is `64/2400 = 0.0267` of full scale — **61× above**
  SC-002 (d)'s per-sample bound of `1.05/(kGainRampMs·0.001·fs) = 4.375e-4` — so the roadmap's Dormancy rule ("re-entry is
  a **50 ms per-sample** linear fade", lines 490–491) and the Phase 2 precedent that states the same
  reason in code (`noise_organism.h:1819-1821`: "The ramps advance PER SAMPLE and are never held across
  the control chunk: a 1.33 ms staircase … is not acceptable") are **unreachable** through it. Twelve
  banks are the cheapest composition that keeps the roadmap rule intact without amending a shipped
  component (SC-013). The cost of the choice — twelve 16-slot skip loops and twelve sets of three
  global smoothers per sample instead of one — is measured, not assumed: it is FR-060 (a), and
  FR-013's **two-tier, probe-gated** fallback is the pre-approved response if it breaks the budget
  (Clarifications Q3 corrects Assumption 1 and orders the tiers: a purely additive `ResonatorBank`
  method first, the SIMD kernel only as the last resort).
- **FR-012** — **Deviation from roadmap line 219's "SIMD path preferred", recorded with evidence.**
  `ModalResonatorBankSimd` cannot express per-peak Q: its decay radius comes from a global two-term
  damping law shared by all modes (`modal_resonator_bank.h:356-372`, `struct DampingLaw` `:111-118`),
  and roadmap line 220 requires Q to wander per peak. The scalar `ResonatorBank` is the only shipped
  bank with independent per-resonator frequency **and** Q **and** gain. The SIMD preference is
  therefore satisfied only through FR-013's fallback, and only if the budget demands it.
- **FR-013** — **Pre-approved fallback, contingent on measurement only, and two-tiered
  (Clarifications Q3).** FR-060's stage probe runs **first, before either tier is considered**. If it
  measures the scalar engine (twelve single-resonator `ResonatorBank` instances per FR-011, 512-sample
  blocks at 48 kHz) at **48 000 ns/block or less — 60 % of the SC-004 ceiling** — FR-011 stands exactly
  as written and **nothing** in this FR is exercised: no shipped component is touched.

  **Corrected premise.** An earlier draft of this spec's Assumption 1 claimed `ResonatorBank` has no
  consumer outside its own test. That claim is **false** and is corrected in the Assumptions section:
  `ResonatorBank` is consumed by every Membrum body (`plugins/membrum/src/dsp/bodies/*.h`,
  `body_bank.h`, `drum_voice.h`, `tone_shaper.h`, `voice_pool.cpp`), by Innexus
  (`plugins/innexus/src/dsp/physical_model_mixer.h`, `processor/innexus_voice.h`), and by
  `ContinuousBody`, `NoiseOrganism`, `iresonator.h` and `modal_resonator_bank_simd.h`. Tier 1 below is
  therefore gated on a consumer-suite regression check (SC-013), not on the blast radius being small.

  **Tier 1 — a purely additive `ResonatorBank` method (preferred fallback).** If the probe exceeds
  48 000 ns/block, the implementation **may** add exactly **one** new, purely additive method to
  `ResonatorBank` — for example a per-resonator-output process variant such as
  `void processIndividual(float in, float* outPerResonator) noexcept` — and compose the network from
  **one** twelve-resonator bank (`setEnabled(i, bool)` giving each peak its own per-sample dormancy
  gate through the same per-resonator output array), in place of FR-011's twelve single-resonator
  banks. Conditions, all mandatory: every existing `ResonatorBank` method's **body, signature and
  semantics are byte-for-byte unchanged** — the new method is additive only, never a rewrite; FR-011's
  per-peak per-sample gate and FR-044's composition survive the substitution because the new method
  hands back each resonator's individual output; the substitution is recorded in `plan.md` with the
  measured figures. This tier needs less new code than Tier 2 (one accessor vs. hand-rolled
  coefficients and peak-gain normalisation) and is tried first for that reason.

  **Tier 2 — the `processSympatheticBankSIMD` kernel (last resort, behind Tier 1).** Taken only if
  Tier 1 is unavailable or itself insufficient. The implementation may substitute a
  `processSympatheticBankSIMD`-based engine (`systems/sympathetic_resonance_simd.h:39`) for the
  per-peak `ResonatorBank` engine **without a spec amendment**, provided: the entire public surface and
  every other FR is unchanged; FR-011's per-peak per-sample gate and FR-044's composition survive the
  substitution because the kernel exposes per-resonator outputs through its **state** array — `y1s[i]`
  holds `y[n]` for resonator *i* after the call (`sympathetic_resonance_simd.h:29-30`, "In/out y[n-1]
  state array") — whereas its `float* sums` out-parameter is a **single accumulator** (`:37`, "Out:
  pointer to accumulated output sum") and must be ignored; the gate is applied to the read-back
  `y1s[i]` *outside* the recurrence, never through the kernel's `gains` array, which is an **input**
  gain inside the recurrence (`:26`, `scaledInput * gain`) and would corrupt the filter state; the
  per-peak response is normalised to constant peak gain so FR-031 still holds (the kernel's all-pole form has peak magnitude ∝ `1/(1−r)`, unlike `ResonatorBank`'s
  constant-0 dB-peak bandpass, `resonator_bank.h:560-591`); and the substitution is recorded in
  `plan.md` with the measured figures.

  **Regression gate for either tier (SC-013, restated).** Taking Tier 1 or Tier 2 must produce **no
  behavioural change to any shipped component**. The enforcing check is running the full consumer-suite
  set — `membrum_tests`, `innexus_tests`, `dsp_processors_tests`, `dsp_systems_tests`, `seraphis_tests`
  — before and after the change. Any moved result in those suites is a **regression to investigate and
  surface**, never a golden to update.

  Any *other* response to a budget miss — lowering `kMaxPeaks`, raising the budget, relaxing a
  threshold — is a **user decision**, never an implementer's
  (`noise_organism_perf_test.cpp:45-57` stop-and-surface rule, inherited verbatim).
- **FR-014** — **Control-step write order is fixed and mandatory**: for each peak, within one control
  step, the network writes **frequency first, then Q, then gain** — `setFrequency(i, hz)`
  (`resonator_bank.h:328`), then `setQ(i, q)` (`:384`), then `setGain(i, dB)` (`:367`). Verified
  reason: `setFrequency` re-derives `qValues_[index] = rt60ToQ(frequencies_[index], decays_[index])`
  (`:333`), so a frequency write **after** a Q write silently discards the Q. The network **never**
  calls `ResonatorBank::setDecay` (`:348`), which would overwrite Q from the same stale decay table.
  Q is owned exclusively by FR-031. Since correction C-16 the third write is a **constant `0 dB`** —
  the bank's per-slot gain is pinned at unity and the peak's dB rides FR-044 step 1's two per-sample
  factors instead — but it keeps its mandated third position, because it is what re-pins a slot whose
  configuration the FR-013 seam may have wiped.
- **FR-015** — A control step writes a peak's parameter **only when it changed** since the last write
  (per-parameter change detection against the last applied value), **with one mandatory exemption: Q.**
  Change detection applies to **frequency only** — correction C-16 removed the control-rate gain write
  the earlier "frequency and gain" clause named (see FR-014). Whenever a frequency write happens, the Q
  write for that peak happens **immediately after it in the same control step, whether or not Q
  changed**, because `setFrequency` clobbers Q — `qValues_[index] = rt60ToQ(frequencies_[index],
  decays_[index])` (`resonator_bank.h:333`) — from a decay table the network never writes (FR-014), so
  `decays_[i]` stands at `kDefaultDecayTime = 1.0 s` (`:60`, written in `prepare` `:201` and `reset`
  `:227`) forever. Skipping the "unchanged" Q write after a frequency write would therefore leave the
  filter at `rt60ToQ(f, 1.0) = 0.4548·f` — Q ≈ 18 at 40 Hz and clamped to 100 above ≈ 220 Hz — for a
  peak the caller configured at Q = 12, silently and permanently. This is the exact trap D-9 names, and
  it is the reason SC-017 measures **realised** bandwidth rather than the network's own echo.
  Equivalently and preferably, the implementation may treat frequency-and-Q as one indivisible
  write pair whose change detection is the **OR** of the two comparisons.
  Cost: `setFrequency` costs a `sin`, a `cos` and a divide plus a coefficient store
  (`resonator_bank.h:572-591`) and `setQ` costs the same recompute (`:387`); with wander disabled
  (FR-034) neither fires, so the network approaches zero control-rate cost, which SC-004 (b) measures.
  **This write order and change-detection rule applies whenever a peak's control-rate values reach the
  bank.** FR-042 (Clarifications Q6) defines the one case that skips the write entirely — a dormant
  peak — deferring it to a single exempt write on the wake edge; that is a different mechanism from
  this FR's change detection, not an exception to the frequency-then-Q order itself, which the deferred
  write still obeys.
- **FR-016** — Global bank settings are **pinned** for the life of the component and are not exposed:
  `setDamping(0.0f)` (default, `:421`), `setSpectralTilt(0.0f)` (default — non-zero tilt costs a
  `std::log2` + `dbToGain` per resonator per sample, `:121-125`, `:507`), `setExciterMix(0.0f)`
  (fully wet, `:626`/`:235`). The network's dry/wet control is FR-043, not `setExciterMix`: the bank's
  mix law `output = input·mix + wetSum·(1−mix)` (`:514`) attenuates the wet path as it raises the dry
  one, so the wet level would depend on the mix setting. The component's parameter defaults are:

  | Parameter | Default | Range |
  |---|---|---|
  | `numPeaks` | 12 | `[1, 12]` |
  | `anchorMode` | `Free` | — |
  | free anchors (peaks 0…11) | 40, 55, 75, 103, 141, 193, 265, 363, 497, 681, 933, 1278 Hz (geometric, ratio ≈ 1.37, deliberately low-biased — roadmap line 17 "subterranean") | `[20, 0.45·fs]` |
  | keyed ratios (peaks 0…11) | 0.5, 1, 1.5, 2, 2.98, 4, 5.04, 6, 7.02, 8, 9.98, 12 (harmonic-**ish**: integers detuned by up to **±0.8 %** — the extremes are 5.04, +0.80 % from 5, and 2.98, −0.67 % from 3 — so a keyed patch does not collapse onto an exact harmonic comb). **The table is normative, the tolerance is descriptive:** an implementation or a later clarification pass uses these twelve numbers verbatim and does not regenerate them to fit a tighter prose bound, because SC-014 (b)/(d)/(e) assert against exactly these values | `[0.25, 64]` |
  | `noteFrequency` | 55 Hz | `[8, 0.45·fs]` |
  | `gravity` | 0.0 | `[-1, +1]` |
  | `peakLevel` | −6 dB | `[-60, +12]` dB |
  | `peakQ` | 12 | `[0.1, 100]` |
  | `freqWander` (**per-peak default** — Clarifications Q7 relabels this and the next two rows; the setter is per peak, `setFreqWander(peak, ...)`, FR-030) | 3 semitones | `[0, 24]` |
  | `qWander` (**per-peak default**, `setQWander(peak, ...)`, FR-031) | 0.5 (octaves of Q) | `[0, 2]` |
  | `gainWander` (**per-peak default**, `setGainWander(peak, ...)`, FR-033) | 6 dB | `[0, 24]` |
  | `peakPan` (**per-peak default**, FR-038, Clarifications Q4) | drawn once per peak from the network seed at `prepare`/`setSeed` (`kSaltPanPosition`, FR-005), spread across the range | `[-1, +1]` |
  | `panWander` (**per-peak default**, FR-038, Clarifications Q4) | 0.2 (fraction of full pan range) | `[0, 1]` |
  | `wanderRate` | 0.03 Hz | `[0.002, 1.0]` |
  | `freqSlewCeiling` | 0.02 octaves/control step | `[0.001, 24]` |
  | `qSlewCeiling` | 0.05 octaves/control step | `[0.001, 24]` |
  | `wetGain` (FR-045, Clarifications Q2) | `kDefaultWetGainDb` — **measured during implementation**, provisional **+30 dB** (see FR-045) | `[-24, +48]` dB |
  | `wanderEnabled` | `true` | — |
  | `wake` | 1.0 | `[0, 1]` |
  | `dormant` | `false` | — |
  | `mix` | 1.0 | `[0, 1]` |

- **FR-017** — `void setPeakLevel(std::size_t peak, float dB) noexcept` sets the peak's **base** gain
  in dB, clamped `[-60, +12]`. The peak's composed dB value is `baseDb + gainWanderOffsetDb` (FR-033),
  clamped to the same `[-60, +12]`; that composed value is what FR-052's `getPeakCurrentGainDb`
  reports.

  **The base level is owned by a per-peak, per-sample `LinearRamp` at `kGainRampMs` (50 ms)** — the
  `Slot::levelRamp` shape of the shipped sibling (`noise_organism.h:1189`, documented there as "the
  SOLE owner of the user's slot level", advanced per sample at `:502-507`). Its target is
  `dbToGain(baseDb)` and is set **only from `setPeakLevel`** (plus a snap in `prepare`, `reset` and
  `clearAudioState`), which makes it immune by construction to the `LinearRamp` re-target trap the
  FR-041 gate is guarded against. It advances per sample for **every** peak — dormant and
  out-of-count included — so SC-010's block-size invariance holds. The ramp is not optional: without
  it a mid-render `setPeakLevel(i, −60 → +12)` steps that peak 72 dB in a single sample, because
  `ResonatorBank::setGain` writes `gains_[index] = dbToGain(dB)` (`resonator_bank.h:367-371`) and
  `:504` consumes it with a bare multiply and no smoother of any kind.

  **The peak's bank is therefore told NOTHING about the peak's dB at all: its per-slot gain is pinned
  at unity and both dB factors are applied per sample** (**correction C-16**) — the base level on the
  `LinearRamp` above, and the wander part `appliedGainDb − baseDb` on a second per-peak factor
  interpolated linearly across one control chunk, retargeted every control step from wherever the
  previous segment ended. Since
  `dbToGain(appliedGainDb − baseDb) · dbToGain(baseDb) == dbToGain(appliedGainDb)`, the composition
  above is arithmetically unchanged; the split only decides which factors are ramped per sample, and
  since C-16 the answer is all of them. The earlier form wrote the wander part to the bank once per
  control step via `setGain` (`resonator_bank.h:367`); FR-035 records what SC-002 measured of that.
  The FR-041 gate is **not** part of that dB value and is never expressed in dB — see FR-044 for the
  full composition. The read surface reports the dB value and the gate separately (FR-052).
- **FR-018** — The network applies a final safety clamp of `±kOutputClamp` (`= 4.0f`, the
  `noise_organism.h:180` value) to **each of its two output channels independently** and counts
  engagements in a single saturating `std::uint32_t` shared across both channels, readable via
  `getClampEngagementCount()` (`noise_organism.h:992` pattern). The clamp
  is a last resort, not a level control: SC-001 requires **zero engagements** in every in-spec
  configuration, so a non-zero count is a diagnosis, not a pass.

  **Non-finite guard, immediately before the FR-043 crossfade (correction C-15).** The clamp bounds
  `±inf` (`inf > kOutputClamp` is true) but **cannot bound a NaN**: every comparison against a NaN is
  false, so a NaN walks through the clamp untouched and out of the component. And a NaN is reachable
  **without a non-finite input**, through the public control surface alone: a resonator whose centre
  frequency is retuned fast and far is a parametrically pumped oscillator, and SC-002 (c)'s own
  injection is the textbook 2f case — measured, the shipped build's wet sum went non-finite 1.83 s
  into that render and stayed non-finite for 97 % of it. The network therefore tests the stereo wet
  sum for finiteness once per sample and, on a sample that fails, **clears the ring of every peak
  whose engine output is non-finite** (a state clear only — configuration, gate, level ramp and lanes
  are untouched, so the peak resumes rather than latching silent), substitutes a zero wet sum for that
  sample and counts one engagement. Two finite tests per sample on the fast path; the repair loop runs
  only on the sample that diverged.
- **FR-019** — Twelve peaks summed together can add constructively. The network applies a
  `1/sqrt(N)` normalisation, identically to both channels of the stereo wet sum (FR-044), where
  **`N` is defined to be exactly `numPeaks`**
  (FR-010's runtime count, always in `[1, kMaxPeaks]`) — **not** the number of awake peaks, **not** the
  number of enabled resonators, and never zero. The definition is load-bearing in both directions and
  each half is enforced:
  - **Keying it to `numPeaks` is what stops wake/sleep events from moving the bed.** If `N` counted
    awake peaks instead, a `SlowEventScheduler` waking one peak of twelve would rescale the other
    eleven by `20·log10(sqrt(12/11)) = 0.19 dB`, and a bed that ran from one awake peak to twelve would
    shift by `20·log10(sqrt(12)) = 5.4 dB` — a level step on every scheduled event, in the one
    component whose entire premise is that nothing steps. SC-015 (g) asserts the bed does not move
    across a scripted wake/sleep sequence.
  - **`N ≥ 1` always, so the normalisation is always finite.** With every peak dormant the wet sum is
    exactly `0.0f` (FR-044) and `1/sqrt(numPeaks)` is a finite number, so the product is exactly
    `0.0f` — digital silence, which is what SC-015 (c) and SC-004 (c) require. An "active = awake"
    reading would compute `1/sqrt(0) = +inf`, and `inf × 0.0f = NaN`: the opposite of silence, and a
    trip of SC-001 (a)/SC-009/SC-016 (d)'s non-finite gates.

  It is recomputed only when `numPeaks` changes and ramped over `kGainRampMs` so the change is
  click-free. **A `setNumPeaks` change therefore does move the level of the surviving peaks, by
  design**: dropping 12 → 4 raises them by `20·log10(sqrt(12/4)) = 4.77 dB` over the 50 ms ramp. That
  is the intended meaning of the normalisation — it makes "the same patch with 4 vs 12 peaks"
  comparable in level, which SC-001 (c) asserts — and SC-015 (g) asserts the size of that move against
  the derived figure rather than leaving it unmeasured.

  Because the name `activePeakCount` invited exactly the ambiguity above, the read surface calls this
  quantity `getNormalisationPeakCount()` (FR-052); there is no getter named `getActivePeakCount`.

### FR-020 series — Anchors and anchor modes (roadmap lines 221–222)

- **FR-020** — `enum class AnchorMode : std::uint8_t { Free = 0, Keyed = 1, Hybrid = 2 }`, selected by
  `void setAnchorMode(AnchorMode mode) noexcept`, default `Free`. Enumerators are **appended only**,
  never reordered (they become a persisted plugin parameter at Phase 12).
- **FR-021** — **Free** (roadmap line 221, "peaks wander anywhere in range"): the anchor for peak *i*
  is the absolute frequency set by `void setPeakAnchorHz(std::size_t peak, float hz) noexcept`,
  sanitised then clamped to `[kMinResonatorFrequency, 0.45·fs]` (`resonator_bank.h:42`, `:45`,
  `clampFrequency` at `:542-545`). Defaults are the low-biased table in FR-016.
- **FR-022** — **Keyed** (roadmap line 221, "anchors track note pitch × harmonic-ish ratios"): the
  anchor for peak *i* is `noteHz × ratio[i]`, where `void setNoteFrequency(float hz) noexcept`
  (clamped `[8, 0.45·fs]`) is written by the caller on note-on and
  `void setPeakRatio(std::size_t peak, float ratio) noexcept` (clamped `[0.25, 64]`) holds the
  per-peak ratio, defaulting to the harmonic-ish table in FR-016. Changing `noteHz` moves every keyed
  anchor on the next control step; the resulting frequency change is subject to the FR-035 slew limit,
  so a note change cannot step the bank's coefficients.
- **FR-023** — **Hybrid** (roadmap line 222, "gravity-style pull, reusing the HarmonicCloud gravity
  concept"): `void setGravity(float g) noexcept`, `g ∈ [-1, +1]`.
  **Index-paired (Clarifications Q1): peak *i* pulls only toward its OWN keyed anchor, never toward the
  nearest grid point.** Gravity is a closed-form crossfade, in the log-frequency domain, between peak
  *i*'s Free anchor `f_free[i]` (FR-021) and peak *i*'s own Keyed anchor `f_keyed[i] = noteHz × ratio[i]`
  (FR-022):

  `log f[i] = log(f_free[i]) + g · (log(f_keyed[i]) − log(f_free[i]))`

  - `g = 0` ⇒ the anchor is exactly `f_free[i]`, bit-equal to Free mode — the identity property
    `HarmonicCloud`'s law also has at `g = 0` (`harmonic_cloud.h:466-467`);
  - `g = 1` ⇒ the anchor is exactly `f_keyed[i] = noteHz × ratio[i]` — **exactly Keyed mode for that
    peak**, and because every peak pulls toward its own ratio the twelve resulting frequencies are the
    twelve **distinct** `noteHz × ratio[i]` values, never collapsed onto a shared grid;
  - `g = −1` ⇒ `log f[i] = 2·log(f_free[i]) − log(f_keyed[i])` — the log-mirror of the keyed anchor
    around the free anchor: peak *i*'s own maximally **anti**-keyed point, replacing the earlier
    shared-grid "maximally inharmonic" concept with a per-peak closed form;
  - every intermediate `g` is the same closed-form log-linear interpolation/extrapolation, monotone in
    `g` for a fixed `f_free[i]`/`f_keyed[i]` pair — no search, no branch on which side of a grid the
    free anchor falls.

  Because every anchor mode routes through the same two per-peak numbers, **Free, Keyed and Hybrid are
  the same twelve peaks moving** — bijective and collision-free by construction, with no shared grid, no
  nearest-neighbour search and **no grid-extension rule of any kind** (an earlier draft of this spec
  pulled peak *i* toward the keyed anchor *nearest in log frequency*, which collapsed distinct peaks
  onto shared grid points at `g = 1` for the FR-016 defaults — see Clarifications Q1 for the collision
  and why it is now structurally impossible; that draft's separate "`f_anti` outside the grid's span"
  repeated-extension rule is deleted along with it, superseded by the closed form above).

  This is **not** `HarmonicCloud`'s formula and does not claim to be: that law warps a harmonic index
  `n` (`ratio_g(n) = pow(n, 1 + g·kGravityExponentRange)`, `harmonic_cloud.h:1284`) while this
  component's free anchors are arbitrary Hz values with no index. The reused property is the concept
  and the identity-at-zero contract.

  SC-014 (c)/(d)/(e) assert the three named points of this closed form directly.
- **FR-024** — Anchor recomputation happens on the control grid, never per sample, and only when an
  input to it changed (mode, free anchor, ratio, note frequency, gravity). A full recomputation costs
  at most twelve `std::log`/`std::exp` pairs and runs at most once per control step. **Clarifications
  Q1 removes FR-023's earlier grid-search and grid-extension machinery entirely** (it existed only to
  select `f_grid`/`f_anti` under the superseded nearest-neighbour law), so this bound is now exact for
  every configuration rather than an amortised estimate: each peak's Hybrid anchor costs exactly two
  `std::log` calls and one `std::exp` call, with no unbounded or configuration-dependent loop.
- **FR-025** — Anchors, after mode resolution, are clamped to `[kMinResonatorFrequency, 0.45·fs]`
  **before** the drift offset is applied, and the drifted result is clamped again (FR-030). Clamping
  is silent (no engagement counter): it is a legitimate configuration outcome at low sample rates — a
  2 kHz anchor is valid at 48 kHz and equally valid at 44.1 kHz.

### FR-030 series — Drift lanes (roadmap lines 219–220)

- **FR-030** — Each peak owns a **frequency** lane: one `BrownianDrift`, mapped
  `f = anchor · 2^(depthSemitones · lane / 12)` with `lane ∈ [-1, +1]`
  (`brownian_drift.h:212-214`). `void setFreqWander(std::size_t peak, float semitones) noexcept`
  clamps depth to `[0, 24]`. The result is clamped to `[kMinResonatorFrequency, 0.45·fs]`. Because the
  lane is bounded by construction, the realised frequency is provably inside
  `anchor · 2^(±semitones/12)` intersected with the clamp — SC-003 (c) asserts exactly that
  ("bounded around anchor ratios", roadmap line 220).
- **FR-031** — Each peak owns a **Q** lane: one `BrownianDrift`, mapped multiplicatively
  `Q = baseQ · 2^(amount · lane)`, with `void setPeakQ(std::size_t peak, float q) noexcept` (clamped
  `[kMinResonatorQ, kMaxResonatorQ] = [0.1, 100]`, `resonator_bank.h:48,51`) and
  `void setQWander(std::size_t peak, float amount) noexcept` (clamped `[0, 2]`, in octaves of Q). Q
  reaches the bank only through `setQ` (`:380`), only after the frequency write (FR-014), and is
  clamped to the bank's range before the call. `ResonatorBank`'s bandpass is constant-0 dB-peak
  (`:560-591`), so Q changes bandwidth without changing peak height — the property that keeps Q and
  gain independent controls.
- **FR-032** — The network **never** expresses Q as a decay time. `rt60ToQ` saturates at
  `kMaxResonatorQ = 100` for every `f·RT60 > 219.87` (`resonator_bank.h:92-99`: `Q = π·f·RT60/ln1000`,
  and `100·ln1000/π = 219.87`), so a decay-time surface would make Q wander invisible for every peak
  below ~70 Hz at a musically ordinary 3 s decay. Q is set directly; the equivalent RT60 is
  **reported** by the read surface (FR-052) and never written.
- **FR-033** — Each peak owns a **gain** lane: one `BrownianDrift`, mapped additively in dB —
  `gainDb = baseDb + depthDb · lane` — with `void setGainWander(std::size_t peak, float dB) noexcept`
  clamped `[0, 24]`. The sum is clamped to `[-60, +12]` before `dbToGain`.
- **FR-034** — One network-level rate scalar governs all four lane kinds (frequency, Q, gain and pan —
  FR-038 adds the fourth):
  `void setWanderRate(float hz) noexcept`, clamped `[0.002, 1.0]`, default
  `kDefaultWanderRateHz = 0.03f` (the `noise_organism.h:163` value, so the two Vorago components drift
  on the same clock unless a caller separates them). **This is the same Vorago vocabulary as
  `NoiseOrganism`'s primary rate control, not a unit mismatch (Clarifications Q7):**
  `NoiseOrganism::setWanderRate(hz)` (`noise_organism.h:781`) is that component's organism-wide primary
  rate control, with its per-slot `smoothnessSeconds` argument (`setResonatorWander`,
  `noise_organism.h:686`) as a secondary path the scalar overrides. A network-wide Hz rate here is
  therefore consistent with the shipped sibling's primary control, not a divergence from it; per-lane-
  kind rate multipliers (letting gain breathe faster than pitch) are deliberately deferred to Phase 10,
  when there is a patch to tune them against. The requested correlation time is
  `T = 1/rate ∈ [1.0, 500]` s. `BrownianDrift` alone **cannot express `T > kTauMax = 30 s`**
  (`brownian_drift.h:99`; `setSmoothness` clamps its argument to `[0, 1]`, `:152-156`), so a bare
  `tau = 1/rate` mapping would make **every rate at or below `1/30 = 0.0333 Hz` behave identically —
  including this spec's own 0.03 Hz default and the whole `[0.002, 0.0333]` Hz sub-range**, while
  `getWanderRate` kept reporting the requested number. FR-037 removes that dead zone; the mapping is
  therefore two-part and defined **jointly** with it:
  - `decimation = clamp(ceil(T / kTauMax), 1, kMaxLaneDecimation)` (FR-037);
  - `tau = T / decimation`, which by construction lands in `[kTauMin, kTauMax] = [0.2, 30]` s, then
    `smoothness = (tau − kTauMin)/(kTauMax − kTauMin)` passed to `setSmoothness`
    (`brownian_drift.h:152`).

  `void setWanderEnabled(bool) noexcept` (default `true`) freezes every lane at its current value
  without rewinding it — the control arm SC-003 (b) needs.
- **FR-035** — Frequency and Q writes are **slew-limited on the control grid**: the per-control-step
  change in `log2(frequency)` is capped at `freqSlewCeiling` and the per-step change in `log2(Q)` at
  `qSlewCeiling`. Both default to the values in FR-016's table — `kDefaultFreqStepOctaves = 0.02`
  (≈ 24 cents per 64 samples, ≈ 18 octaves/s at 48 kHz — far above any in-spec drift, so at the default
  it never shapes normal motion) and `kDefaultQStepOctaves = 0.05`. The limiter exists for
  discontinuous inputs — a `setNoteFrequency` jump, an anchor-mode switch, a gravity sweep — not for
  drift. Gain needs no slew limit: **every factor of it is applied per sample** — the FR-041 gate, the
  FR-017 base-level `LinearRamp` at `kGainRampMs`, and (**correction C-16**) the FR-017 per-sample
  interpolation of the wander part `appliedGainDb − baseDb` across one control chunk. An earlier form
  of this sentence let the wander part reach the bank on the control grid instead and argued that "its
  per-chunk step is bounded by the gain lane's own 150 ms output smoother inside `BrownianDrift`".
  Bounded is not continuous: `ResonatorBank::setGain` is a bare `gains_[index] = dbToGain(dB)` with no
  smoother of any kind (`resonator_bank.h:367-371`), so the drifting factor stepped once per 64
  samples and SC-002 (a) measured that gain path alone at `B/P = 15.72` against a bound of 1.5. The
  bank's per-slot gain is now pinned at unity.

  **One exemption, and only one (Clarifications Q6): the FR-042 dormant interval, of which the
  wake-edge snap is the audible boundary.** While a peak's gate sits at exactly `0.0f`, that peak's
  applied frequency and Q track their targets **unslewed** — for the whole dormant interval, not only
  on the wake edge — so that the single frequency-then-Q-then-gain write it receives when its gate
  leaves `0.0f` is **written in full on that control step regardless of how far it moved during the
  dormant interval**. The wider form is what FR-042's own normative sentence requires: a slew-limited
  dormant peak would still be fifty control steps from target at the wake edge after a mid-dormancy
  octave jump, and could not satisfy it. The justification is that the gate is provably exactly
  `0.0f` throughout the interval (FR-042), so no coefficient motion inside it is audible by
  construction; every other write in the component, including a peak's very next control step after
  waking, remains subject to this FR's ceiling. The two designs are observationally identical under
  drift, which never reaches the default ceiling, so SC-015 (f) is the arm that distinguishes them:
  after a mid-dormancy anchor jump the applied frequency reflects the new anchor within **one**
  control step, not fifty.

  **Both ceilings are settable, not compile-time constants:**
  `void setSlewCeilings(float freqOctavesPerStep, float qOctavesPerStep) noexcept`, each clamped to
  `[0.001, 24]` octaves per control step, read back by `getFreqSlewCeiling()` / `getQSlewCeiling()`
  (FR-051). This is a real control surface, not a test backdoor — Phase 10 will want a tighter ceiling
  for a slow patch and a looser one for snappy note tracking — and it is also the **declared
  mechanism** SC-002 (c)'s injection arm uses: a ceiling of 24 octaves/step is an in-spec setting under
  which a ±1-octave anchor jump lands in a single control step, producing the discontinuity that arm
  must show the estimator catching. No `#ifdef`-guarded hook and no edit to the header under test is
  needed or permitted.
- **FR-036** — Lanes advance **whether or not** the peak is audible — the Dormancy rule's "modulation
  lanes keep running" clause (roadmap lines 490–491), so a peak that sleeps for two minutes wakes
  somewhere new rather than where it fell asleep (SC-015 (b)). They advance via
  `BrownianDrift::processBlock(kControlChunkSamples)` (`brownian_drift.h:194`), never per sample, on
  the FR-037 schedule.
- **FR-037** — **Lane decimation, so minute-scale correlation times are reachable (roadmap line 31,
  "Time scale is minutes, not milliseconds").** Each lane advances
  `processBlock(kControlChunkSamples)` **once every `decimation` control chunks** rather than on every
  chunk, with `decimation` derived from the requested rate by FR-034 and
  `static constexpr std::size_t kMaxLaneDecimation = 17` (`17 × kTauMax = 510 s ≥ 500 s`, the slowest
  rate FR-016 admits). `decimation == 1` is the un-decimated case and is what every rate
  `≥ 1/30 Hz = 0.0333 Hz` selects.
  - **Why it works, and why it is sample-rate independent.** Between advances the lane's value is
    held, so in wall clock the walk experiences `kControlChunkSamples / fs` seconds of its own
    evolution per `decimation · kControlChunkSamples / fs` seconds elapsed. The realised correlation
    time is therefore `tau · decimation` seconds, and `fs` cancels — the same property SC-008 asserts.
  - **Why it does not zipper.** A decimated lane holds its output for at most
    `kMaxLaneDecimation · kControlChunkSamples = 1088` samples (22.7 ms at 48 kHz), and the value it
    then jumps to differs by at most one `kTauMin`-scale OU increment through a 150 ms output smoother
    (`kDriftOutputSmoothMs`, `brownian_drift.h:103`) that advanced only 64 samples — orders of
    magnitude below `freqSlewCeiling`. SC-002 renders at `wanderRate = 1.0 Hz`, where
    `decimation == 1`, so the un-decimated path is the one the zipper criterion stresses; SC-003 (f)
    is what proves the decimated path is not inert.
  - The decimation counter is **per network, not per lane** — all 48 lanes (FR-038 adds the pan lane)
    advance on the same chunk — so a rate change costs one integer recompute, and lane phase cannot
    drift apart. The counter is part of the FR-007 absolute grid, so block partitioning does not change
    it (SC-010).
- **FR-038** — **Per-peak pan (Clarifications Q4), the fourth wander lane.** Each peak owns a **pan**
  lane in addition to frequency, Q and gain: `void setPeakPan(std::size_t peak, float position)
  noexcept`, clamped `[-1, +1]` (`-1` hard left, `0` centre, `+1` hard right), and
  `void setPeakPanWander(std::size_t peak, float depth) noexcept`, clamped `[0, 1]` (a fraction of the
  full pan range), driving one more `BrownianDrift` instance per peak on the **same** FR-034
  network-wide Hz rate as the other three lanes. The realised pan position is
  `pan[i] = clamp(position[i] + depth[i] · lane[i], -1, +1)` with `lane ∈ [-1, +1]`
  (`brownian_drift.h:212-214`), so the lane is bounded by construction exactly as FR-030's frequency
  lane is.

  **Equal-power placement, applied to the peak's gated mono output (FR-044).** Given `pan[i] ∈ [-1,+1]`,
  let `theta[i] = (pan[i] + 1) · (π/4) ∈ [0, π/2]`; the peak's left/right gains are
  `gainL[i] = cos(theta[i])`, `gainR[i] = sin(theta[i])`, satisfying `gainL[i]² + gainR[i]² = 1` for
  every `pan[i]` — constant total power, so a peak crossing centre does not dip. This is the standard
  sin/cos equal-power law and the same idiom `HarmonicCloud` applies per-partial; it is computed fresh,
  not reused as code.

  **Default positions are seeded, not zero.** `void setSeed` (FR-005) draws each peak's default
  `position[i]` once, at `prepare`/`setSeed`, from a dedicated one-shot `Xorshift32` draw
  (`kSaltPanPosition`, FR-005) spread across `[-1, +1]` — so the field is deterministic under seed
  (SC-006 (a)) and spread across the stereo image out of the box, per FR-016's defaults, rather than
  collapsed to centre.

  **Total lane count is 48** (twelve peaks × four lanes), up from 36 in an earlier draft of this spec.
  The lane's cost is measured by FR-060's probe exactly like the other three
  (FR-060 (b) now covers all four lane kinds); **if the probe shows the pan lane is what breaks the
  SC-004 budget, the pre-approved fallback is STATIC seeded pan positions with the identical public API
  (drop the wander lane's per-sample advance; `setPeakPan`/`getPeakPan` are unaffected and
  `setPeakPanWander`/`getPeakPanWander` become no-ops that still accept and report a value) — never a
  threshold relaxation.**
- **FR-039** — Pan reaches only the network's own equal-power split (FR-044); `ResonatorBank` itself is
  never told about pan and has no stereo concept — the bank's mono `process(float)` output is what gets
  split into `gainL[i]`/`gainR[i]` after the gate multiply. Pan therefore costs no bank call and no
  control-rate write to `bank_[i]`.

### FR-040 series — Peak life cycle (roadmap lines 223–224)

- **FR-040** — `void setPeakWake(std::size_t peak, float amount) noexcept` takes a plain scalar in
  `[0, 1]` — not a scheduler reference (`noise_organism.h:841-843` precedent). Phase 10 owns the
  `SlowEventScheduler` and writes `getEnvelopeValue() × getActiveDepth()`
  (`slow_event_scheduler.h:391`, `:397`) into the peak named by `getActiveTarget()` (`:356`). Peak
  indices `[0, 11]` are valid scheduler target ids because `kMaxTargets = 16 ≥ kMaxPeaks = 12`
  (`slow_event_scheduler.h:152`).
- **FR-041** — The effective peak gate is `gateSteady(i) = (dormant(i) || i >= numPeaks) ? 0 : wake(i)`
  (`noise_organism.h:1669-1677`, same shape), reached through a per-peak `LinearRamp` configured at
  `kGainRampMs = 50.0f` (`noise_organism.h:178`) and stepped **per sample** — reachable per sample only because FR-011 gives each peak its own bank, and applied to that peak's output as a linear multiply by FR-044. A caller that jumps wake
  from 0 to 1 in one call gets a 50 ms ± 5 ms 0→100 % ramp; a caller that walks it over 40 s gets a
  40 s fade, because the ramp target simply tracks. The roadmap's "gain → 0 and back over tens of
  seconds" (line 223) is therefore the *caller's* trajectory; the 50 ms ramp is the guard that makes
  any trajectory click-free.
- **FR-042** — **Dormancy** (roadmap lines 489–493, which names Phase 3 peaks explicitly): when a
  peak's gate ramp reaches exactly `0.0f`, the network **stops calling `bank_[i].process` altogether**
  for that peak and disables its resonator via `ResonatorBank::setEnabled(0, false)`
  (`resonator_bank.h:401`). Skipping the call is what earns the CPU budget under FR-011's twelve-bank
  composition (the internal `enabled_` skip, `:487-488`, would still cost that bank's three global
  smoothers per sample); the `setEnabled` write keeps the bank's own state consistent and is what
  `isPeakEngineActive(i)` (FR-052) reports. Its four wander lanes keep advancing (FR-036, FR-038).
  `void setPeakDormant(std::size_t peak, bool dormant) noexcept` and `setPeakWake(i, 0.0f)` are
  **behaviourally identical** and differ only on the read surface, exactly as the cross-cutting rule
  requires.

  **The sleep edge clears the peak's filter state; it does not freeze it.** On the control step that
  takes the gate to exactly zero the network calls `bank_[i].reset()` (`resonator_bank.h:213`, whose
  step 1 is `filter.reset()` on every slot, `:214-217`) and **immediately re-applies** that peak's
  frequency, Q and gain in the FR-014 order, leaving slot 0 disabled. Without this the biquad state
  would be frozen at whatever energy it held — at `peakQ = 100` and a 40 Hz anchor that is
  `RT60 = Q·ln1000/(π·f) = 5.5 s` of stored ring — and would be released, at full amplitude, into a
  filter whose coefficients had drifted for the whole dormant interval, seconds after a 50 ms gate ramp
  had finished attenuating it. The peak therefore **wakes silent and re-excites from the input**, which
  is what makes the roadmap's "re-entry is a 50 ms per-sample linear fade" (lines 490–491) describe the
  audio and not merely the multiplier. The re-apply is mandatory for the same reason FR-004's is:
  `reset()` is a configuration wipe (`:224-231`). It is RT-safe — fixed-size loops, no allocation — and
  it runs once per sleep edge, not per sample.

  **While the gate sits at exactly `0.0f`, the network skips the peak's control-rate bank writes
  entirely (Clarifications Q6).** Between the sleep edge and the wake edge the network **does not**
  call `setFrequency`/`setQ`/`setGain` (FR-014) for that peak — the bank is disabled and inaudible, so
  writing to it buys nothing and only costs the `sin`/`cos`/divide FR-015 already prices. The peak's
  four wander lanes (FR-030, FR-031, FR-033, FR-038) and its anchor (FR-024) keep advancing and the
  network keeps **computing and internally storing** what the applied frequency, Q, gain and pan *would*
  be, so FR-052's read surface reports them exactly as it would for an awake peak — matching
  `NoiseOrganism`'s echo-while-dormant behaviour. This is the one CPU difference between the two Vorago
  siblings: `NoiseOrganism` keeps writing its resonator lanes while dormant; this network does not.
  Never writing anything is what SC-004 (c)'s "≥ 40 % below reference" all-dormant CPU arm legitimately
  counts toward.

  **On the wake edge — the control step where the gate target leaves `0.0f` — the network snaps the
  stored frequency, Q and gain to the bank in one write, in the FR-014 order, exempt from FR-035's
  slew-rate limiter.** The exemption is inaudible by construction: the gate is provably still exactly
  `0.0f` at that instant (the ramp has not yet lifted off), so the coefficient jump produces no audible
  step regardless of its size. FR-035 states this exemption explicitly. This snap write **is** the
  re-apply that re-enables the resonator described below; it is not a second, separate write.

  Re-entry re-enables the resonator on the control step in which the gate target leaves zero, before
  the ramp lifts off zero, so the first audible sample is already filtered. SC-015 (f) measures the
  wake after a 120 s dormancy at max Q, and is extended (Clarifications Q6) to assert that the bank
  itself — not only the network's echoed applied-state getters — received the snapped value on the wake
  edge.
- **FR-043** — `void setMix(float mix) noexcept`, `[0, 1]`, is a **linear crossfade owned by the
  network, applied identically and independently to each channel**:
  `outL = (1 − mix)·dryL + mix·wetL`, `outR = (1 − mix)·dryR + mix·wetR`, with `mix` ramped by one
  shared `OnePoleSmoother` at 20 ms (`kResonatorSmoothingTimeMs`, `resonator_bank.h:69`). **The dry
  path is the sanitised stereo input, carried unchanged on both channels (FR-009, Clarifications Q4)** —
  an earlier draft of this spec was mono here. `ResonatorBank::setExciterMix` is not used (FR-016
  states why). With every peak dormant and `mix = 1`, the output is silence on both channels, not the
  dry signal — documented and tested (SC-015 (c)), matching the bank's own fully-wet semantics
  (`resonator_bank.h:514`).

- **FR-044** — **The single canonical signal path, stated once; every other FR refers to it rather than
  restating a piece of it (Clarifications Q8 corrects the ordering an earlier draft of this FR stated,
  which conflicted with FR-019, FR-043 and SC-015 (c)).** For peak *i* at sample *n*, in order:

  1. **Per-peak gated mono sample (FR-011, FR-017, FR-041):**
     `peakOut[i][n] = bank_[i].process(0.5·(inL[n] + inR[n])) · gate[i][n] · levelGain[i][n] ·
     wanderGain[i][n]`, where `gate[i][n] ∈ [0, 1]` is the FR-041 per-sample `LinearRamp` value,
     `levelGain[i][n]` is the FR-017 per-sample base-level `LinearRamp` value (target
     `dbToGain(baseDb)`, `kGainRampMs`) and `wanderGain[i][n]` is the FR-017 per-sample interpolation
     of `dbToGain(appliedGainDb − baseDb)` across one control chunk (**correction C-16**) — all three
     **linear multiplies applied to the bank's output sample**, never dB offsets folded into
     `setGain`. The bank's own per-slot gain is pinned at unity, so the product
     `levelGain[i][n] · wanderGain[i][n]` settles at exactly `dbToGain(appliedGainDb)` and FR-017's
     composition is unchanged by the split. For the gate the reason is arithmetic, not
     stylistic: a gate of 0 in dB is `−inf`, and FR-033's clamp would floor it at `−60 dB`, which is
     `1.0e-3` of full scale — audible bleed, not silence. `gate == 0.0f` makes the peak's contribution
     **exactly** `0.0f` by multiplication, and FR-042 additionally stops calling `bank_[i].process` at
     all once the ramp has settled at zero. These are the **only** two mechanisms that produce an exact
     zero; no dB path does.
  2. **Per-peak equal-power pan, folded into the stereo sum (FR-038):**
     `wetSumL[n] = Σ_i peakOut[i][n]·gainL[i][n]`, `wetSumR[n] = Σ_i peakOut[i][n]·gainR[i][n]`, summed
     over the twelve gated, panned peaks. This is "the wet sum" FR-019 and Clarifications Q8 refer to —
     already stereo, because pan is a per-peak weight and can only be applied before the cross-peak
     sum, not after it. Both weights are **per-sample interpolations across the control chunk** of the
     equal-power pair the control step computed (**correction C-16**), for the reason SC-002's preamble
     records.
  3. **Normalise (FR-019):** `wetNormL[n] = wetSumL[n] · (1/√numPeaks)`, and likewise for R — the same
     scalar on both channels, since it is peak-invariant and therefore commutes with step 2's ordering.
  4. **Trim (FR-045, Clarifications Q2):** `wetTrimmedL[n] = wetNormL[n] · wetGainLinear`, and likewise
     for R — again the same scalar on both channels.
  5. **Crossfade against the stereo dry path (FR-043):** `outL[n] = (1−mix)·dryL[n] + mix·wetTrimmedL[n]`,
     and likewise for R. The wet sum is tested for finiteness before this step, and the rings of any
     peaks that diverged are cleared (FR-018, correction C-15).
  6. **Final clamp (FR-018):** `outL[n]`/`outR[n]` are each clamped to `±kOutputClamp` independently,
     sharing one engagement counter.

  Steps 3 and 4 apply the **same scalar to both channels**, so their position relative to step 2's
  per-peak pan is arithmetically immaterial to the result; step 2 must nonetheless happen first in any
  implementation because panning is per-peak and the cross-peak sum is not reversible. SC-015 (a) and
  SC-015 (c) assert the exact-zero property (step 1) on both channels; SC-002 (d) asserts the ramp's
  per-sample step bound on `gate[i][n]` itself; SC-015 (c) (extended, Clarifications Q4) asserts that
  `mix = 0` returns both channels of the dry input unchanged within `kSampleTolerance`, which holds
  because steps 2–4 only ever scale the wet term step 5 multiplies by `mix`.
- **FR-045** — **Network-owned static wet-level trim (Clarifications Q2).**
  `void setWetGain(float dB) noexcept`, clamped `[-24, +48]` dB, with a matching
  `float getWetGain() const noexcept` (FR-051). The trim is applied to the normalised wet sum, **before**
  the FR-043 crossfade (FR-044 step 4) — it is a static, network-wide scalar, not a follower: it
  introduces no new audio-rate state and cannot itself make the bed move (unlike option (c), automatic
  wet-energy normalisation, rejected because it reintroduces the level motion FR-019 and D-11 forbid).

  **The default is measured, not guessed, following the `kSourceDriveDb` precedent (Phase 2).** During
  implementation: render the FR-016 reference patch (`numPeaks = 12`, all defaults, `AnchorMode::Free`)
  driven by SC-001's broadband source (white noise at −12 dBFS) with `mix = 1`; measure the wet RMS and
  compare it against the drive RMS; choose `kDefaultWetGainDb` so the wet RMS lands within a few dB of
  the drive RMS at the default trim. Record the measured wet-vs-drive gap, the chosen constant and the
  method in the header next to the constant. The **provisional** figure carried into the build is
  **+30 dB** — informed by SC-001 (c)'s cited 25–35 dB passive attenuation of twelve constant-0 dB-peak
  bandpasses on broadband drive — and is replaced by the measured number, never guessed past.
  SC-020 is the enforcing criterion.
- **FR-046** — **`clearAudioState()` — a lighter sibling to `reset()` (Clarifications Q5).**
  `void clearAudioState() noexcept` clears **only** audio state: every resonator's biquad state (via
  each peak's `bank_[i].reset()` plus immediate FR-014-order re-apply, exactly as FR-042's sleep edge
  does), the FR-041 gate ramps and the FR-043 mix ramp. **It does not touch the wander lanes** — all
  four per peak keep freewheeling from wherever they were, unlike `reset()` (FR-004), which rewinds
  them via `BrownianDrift::reset()`. `clearAudioState()` is therefore the method Phase 10 will use when
  it wants a note to end cleanly (no stored ring) without resetting the cave's evolving state — recorded
  here for Phase 10; this phase adds the method but does not call it from anywhere, matching the
  Non-Goals' "nothing drives it yet." `NoiseOrganism` is expected to gain the same lighter method when
  Phase 10 needs freewheeling across notes on that sibling too — not added in this phase.
  **`clearAudioState` swept this session (`grep -rn "clearAudioState" dsp/ plugins/`): 0 hits** — the
  name is free, unlike a candidate name floated during the interview.
  SC-022 is the enforcing criterion.
### FR-050 series — Read surface

- **FR-050** — An out-of-range `peak` on **any** setter is a silent no-op (`resonator_bank.h:329`
  idiom); on any getter it returns the documented neutral (`0.0f` for floats, `0` for sizes, `false`
  for bools, `AnchorMode::Free` for the mode) and never reads out of bounds
  (`noise_organism.h:855-859` rule).
- **FR-051** — Configuration getters mirror every setter: `getNumPeaks`, `getAnchorMode`,
  `getPeakAnchorHz(peak)`, `getPeakRatio(peak)`, `getNoteFrequency`, `getGravity`, `getPeakLevel(peak)`,
  `getPeakQ(peak)`, `getFreqWander(peak)`, `getQWander(peak)`, `getGainWander(peak)`,
  `getPeakPan(peak)`, `getPeakPanWander(peak)` (FR-038, Clarifications Q4), `getWanderRate`,
  `getFreqSlewCeiling`, `getQSlewCeiling`, `isWanderEnabled`, `isPeakDormant(peak)`,
  `getPeakWakeAmount(peak)`, `getMix`, `getWetGain` (FR-045, Clarifications Q2). **Every per-peak getter
  takes a `peak` index** (Clarifications Q7 makes this explicit — `getFreqWander`/`getQWander`/
  `getGainWander` take the same `std::size_t peak` argument their setters do, FR-030/FR-031/FR-033;
  they were listed without the index in an earlier draft of this spec by omission, not by design).
- **FR-052** — Realised-state getters expose what the tests must measure without adding public API to
  any shipped component: `getPeakCurrentFrequency(i)` (Hz, post-drift, post-clamp),
  `getPeakCurrentQ(i)`, `getPeakCurrentGainDb(i)` (base + wander, **excluding** the gate),
  `getPeakCurrentPan(i)` (FR-038, post-wander, post-clamp), `getPeakGate(i)`,
  `getPeakEquivalentRt60(i)` (`Q·ln1000/(π·f)`, reported only — FR-032),
  `getNormalisationPeakCount()` (FR-019's `N`; there is deliberately no `getActivePeakCount`),
  `isPeakEngineActive(i)`, `getLaneDecimation()` (FR-037), `getClampEngagementCount()`,
  `getAllocatedBytes()`, `isPrepared()`. These are applied-state getters: after a setter they report
  what the audio path is actually using, which for gated quantities may lag by up to `kGainRampMs`
  (`noise_organism.h:461-463` documents the same caveat) and for slew-limited quantities by as many
  control steps as FR-035's ceiling requires — so every criterion that reads one after a setter states
  a settling clause.

  **While a peak is dormant, these getters keep reporting live computed values, not the bank's stale
  applied state (Clarifications Q6).** `getPeakCurrentFrequency(i)`, `getPeakCurrentQ(i)`,
  `getPeakCurrentGainDb(i)` and `getPeakCurrentPan(i)` read the network's own internally tracked
  values — which FR-042 keeps computing every control step even while it skips writing them to
  `bank_[i]` — so they move as an awake peak's do (SC-015 (b)), **except that the FR-035 slew ceiling
  does not apply while the gate sits at exactly zero**: a dormant peak's applied frequency and Q
  track their targets unslewed, per FR-035's dormant-interval exemption, and SC-015 (f) is what
  reads the difference. The bank itself holds whatever it was last written before the sleep edge.

  **`isPeakEngineActive(std::size_t peak)` returns `true` while the network is still calling that
  peak's `bank_[peak].process` and its slot 0 is enabled, `false` once FR-042's sleep edge has fired**
  — `false` for an out-of-range index (FR-050). It exists because FR-011 makes the banks private
  members and the FR-042 skip, which is the whole basis of the SC-004 (c) budget, would otherwise be
  unassertable: `ResonatorBank::isEnabled` (`resonator_bank.h:410`) is not reachable from a test.

  **`getPeakCurrentQ(i)` is the network's applied value, i.e. what it last wrote through `setQ`.** It
  is *not* a read of the filter's realised bandwidth, so on its own it cannot catch the FR-014 /
  FR-015 write-order trap (D-9) — that is SC-017's job, and no Q criterion may be considered covered by
  this getter alone.

### FR-060 series — Safety, budget, footprint

- **FR-060** — **A stage-cost probe is written and run before the component exists**
  (`noise_organism_perf_test.cpp:26-36` precedent). It measures, at 512-sample blocks and 48 kHz, in
  isolation: (a) **twelve single-resonator `ResonatorBank` instances** (FR-011) across the FR-016
  default anchors, each read individually — reported alongside the one-bank-twelve-resonators figure
  the earlier draft of this spec assumed, so the cost of FR-011's composition is a measured number and
  not a guess; (b) **48** `BrownianDrift` lanes advanced with `processBlock(64)` (the `std::pow` term
  quoted in the Existing-components table — the fourth lane, pan (FR-038, Clarifications Q4), is
  reported both pooled with the other 36 and **isolated on its own** so a budget miss can be attributed
  to it specifically), at `decimation = 1` and at `decimation = 17` (FR-037); (c) the twelve-peak
  control-step write path (`setFrequency` + `setQ` + `setGain`, with and without change detection, and
  with FR-015's mandatory Q write); (d) the per-sample gate, pan split, normalisation, trim and mix
  arithmetic (FR-044); (e) one FR-042 sleep edge (`reset()` + re-apply), reported as ns per edge so its
  amortised cost against a 20–90 s event schedule is visible. The table is **reported** and the projection compared against the SC-004 ceiling. The
  probe is a probe, not a gate: it `REQUIRE`s only that every figure is finite and positive. If the
  projection exceeds the ceiling, the response is, in order: **FR-013's Tier 1 (additive
  `ResonatorBank` method)**, **FR-013's Tier 2 (SIMD kernel)** when the engine ((a)) is the dominant
  term, or **FR-038's static-pan fallback** when the pan lane ((b), isolated) is the dominant term;
  if none of those close the gap, a **stop-and-surface to the user** with the measured table — never a
  threshold relaxation, a cap reduction or a budget raise (Clarifications Q3, Q4).
- **FR-061** — Zero allocation after `prepare` (roadmap line 481), including across the entire setter
  surface, `setNumPeaks` changes, mode switches and `reset()`.
- **FR-062** — The prepare-time footprint is bounded and reported by `getAllocatedBytes()`. **There is
  no heap term at all: the declared prepare-time footprint is exactly zero bytes, `prepare` performs
  exactly zero heap allocations, and `getAllocatedBytes()` returns `0`.** The twelve `ResonatorBank`
  instances, the 48 `BrownianDrift` lanes (FR-038 adds the pan lane), every ramp and every peak table
  are fixed-size members with no heap
  state (`resonator_bank.h:184-209` allocates nothing; `brownian_drift.h:121-128` likewise), and no
  dry-path scratch buffer exists: the render path captures both dry samples into locals before either
  output is written, which satisfies every aliasing case FR-003 admits, including the cross-aliased
  one. **`PrepareConfig::maxBlockSamples` is retained** — FR-002 mandates the field and Phase 10/12
  may want it, and removing it would be a gratuitous API change — clamped to `[64, 8192]` and
  reported by the read surface, but it **sizes nothing in this component** and no reported figure is
  derived from it. Both figures are independently recomputable by a test without reading the
  implementation, which is what SC-011 requires. If the implementation ever finds it needs a buffer,
  the constant is **declared here first** and SC-011's expected values are re-derived from the
  declaration — never transcribed from the code.
- **FR-063** — Sample-rate changes are handled by `prepare` only. Every time constant (lane tau via
  smoothness, `kGainRampMs`, the 20 ms mix smoother) is expressed in seconds and re-derived there;
  every frequency is in Hz and re-clamped against the new `0.45·fs` (FR-025).
- **FR-064** — The component compiles clean under `node tools/check-portability.js`, introduces no
  SIMD of its own (so `tools/lint-simd-aligned-loadstore.js` has nothing to check unless FR-013's
  fallback is taken, in which case the kernel is *called*, not written, and its loads are already
  linted), uses designated initialisers for any brace-initialised aggregate (the Clang narrowing
  rule), and adds no `std::isnan`/`std::isinf` (FR-008).

## Success Criteria

Measurement basis for every timing figure: **ns per 512-sample block at 48 kHz**, the basis
established at `harmonic_cloud_perf_test.cpp:69-101` and reused by `continuous_body_perf_test.cpp`,
`atmosphere_engine_perf_test.cpp:22-44` and `noise_organism_perf_test.cpp:201-272`. One 512-block
period is **10 666 667 ns**, so the roadmap's 0.75 % per-voice budget (line 228) is
**80 000 ns/block**. Percent figures are reported, never asserted.

All renders are at 48 kHz unless stated. **The component is stereo (Clarifications Q4):** unless a
criterion says otherwise, its drive is fed identically to both input channels (`inL[n] == inR[n]`),
which makes the mono engine's `0.5·(inL+inR)` sum equal the single-channel drive exactly and lets every
existing single-channel metric (RMS, spectral bands, autocorrelation) apply unchanged to either output
channel; criteria that specifically exercise stereo behaviour (pan placement, independent-channel dry
pass-through) state their per-channel drive explicitly. No criterion below uses a bit-exact float
golden (roadmap line 494).

- **SC-001 — Stability at max Q under sustained input (roadmap line 227).**
  The infinite-ring pattern, `ContinuousBody_DecaysToSilence`
  (`continuous_body_test.cpp:3339-3400`), applied to this component.
  Configuration: 12 peaks, every `peakQ` at `kMaxResonatorQ = 100`, `qWander = 0` (Q pinned at the
  ceiling), anchors at the FR-016 defaults, `mix = 1`; drive = white noise at −12 dBFS for **60 s**,
  then silence.
  Thresholds: (a) no non-finite sample anywhere in the render (bit-pattern test, FR-008);
  (b) `getClampEngagementCount() == 0` — the FR-018 clamp never engages, so boundedness is a property
  of the design and not of the clamp; (c) **the FR-019 normalisation actually normalises** — the
  property the criterion is trying to express, replacing the earlier "peak magnitude ≤ 2.0" bound,
  which could not fail: twelve constant-0 dB-peak bandpasses of width `f/100`
  (`resonator_bank.h:560-591`) on a −12 dBFS broadband drive sum to tens of dB *below* the drive, so a
  2.0 bound sat ≈ 40 dB clear of any real value and (b) already forbids the clamp. It also replaces
  the "agree within a measured bound" form that followed it: under FR-016's geometric anchors at equal
  Q the `numPeaks = 4` and `numPeaks = 12` arms carry **different acoustic content** (`Σf` = 273 Hz
  vs 4624 Hz), so a correct build differs by ≈ 7.5 dB — larger than the 4.77 dB defect the arm targets
  — and any bound "set above the observed maximum" would therefore absorb the very defect it was
  written to catch. **The arm is instead a derived relationship, with the acoustic content held
  identical so that only `N` differs:** render the same patch twice with **peaks 4–11 dormant in both
  arms**, once at `numPeaks = 4` and once at `numPeaks = 12`, and require the level difference between
  them to equal the derived `20·log10(sqrt(12/4)) = **4.77 dB ± 0.5 dB**`, with the `numPeaks = 12`
  arm the quieter of the two. A wrong exponent or a missing normalisation moves that number out of the
  window. This is also the **only** assertion anywhere in this spec of FR-019's "`N` is `numPeaks`,
  not the awake count": with peaks 4–11 dormant in both arms, a build whose `N` counted awake peaks
  computes `1/sqrt(4)` in **both** arms and the measured difference collapses to 0 dB;
  (d) after the drive stops the output falls
  below **−80 dBFS**
  (`1.0e-4`, Membrum's threshold, `test_kit_switch_infinite_ring.cpp:59`) within a bound
  **computed in the test from shipped constants**, not transcribed:
  `kMaxResonatorQ · kLn1000 / (π · kMinResonatorFrequency) + 5 s = 10.995 + 5 ≈ 16.0 s`;
  (e) the same render with `freqWander`/`qWander`/`gainWander` at their maxima (24 st / 2 oct /
  24 dB) and `wanderRate` at its 1.0 Hz ceiling satisfies (a)–(d) unchanged — the worst-case sweep
  roadmap line 483 demands.
  Measured by: `ResonanceDriftNetwork_MaxQRingOut`.
- **SC-002 — No zipper under drift (roadmap line 227).**
  Metric: **control-boundary discontinuity ratio.** Render a steady 220 Hz sine at −12 dBFS, identically
  on both input channels (the Success Criteria section's stereo drive convention) — a deterministic,
  near-sinusoidal output whose second difference is a clean estimator, and the reason a broadband drive
  is *not* used here (`artifact_detection.h`'s `ClickDetector` is unusable on noise, the finding
  recorded in the Phase 2 spec's helper row) — through 12 peaks with anchors spread across the sine, all
  four lanes (frequency, Q, gain and pan, FR-038) at maximum depth and `wanderRate` at its 1.0 Hz
  ceiling, the fastest legal retuning (so `decimation == 1`, FR-037: the un-decimated lane path is the
  one this criterion stresses). **Render duration is fixed at exactly 60 s** (2 880 000 samples at
  48 kHz, 45 000 control chunks) — pinned because the boundary population's size determines the
  statistic. **The metric is computed on the left output channel** (`x[n] = outL[n]`). An earlier
  draft of this paragraph claimed the pan lane "contributes no new boundary discontinuities of the
  kind this criterion targets", because `BrownianDrift`'s internal output smoother
  (`kDriftOutputSmoothMs`) makes the lane itself continuous. **That was wrong, and this criterion
  caught it:** a continuous lane sampled once per control step and held for 64 samples is a staircase,
  and the pan pair alone measured `B/P = 8.79` against a bound of 1.5. Correction C-16 interpolates
  the pan gains across the chunk instead; SC-021 remains the dedicated pan criterion.
  For every sample compute `d[n] = |x[n] − 2·x[n−1] + x[n−2]|`, then partition the samples into the
  **boundary** population (`n mod 64` in `{0, 1, 2}`, 3/64 of the render) and the **interior**
  population (the other 61/64). The residue set is `{0, 1, 2}` and not `{0, 1}`: the control step runs
  at `controlPhase_ == 0`, so the first sample it affects is `n ≡ 0`, and a second difference spans
  three samples — `d[n]`, `d[n+1]` and `d[n+2]` each touch it. Leaving residue 2 in the interior
  population puts discontinuity-carrying samples into the 0.1 % tail `P` is drawn from — 1.6 % of that
  population against a 0.1 % tail — which lifts `P` and collapses `B/P` toward 1 on a build that does
  have a zipper. **`kBoundaryRatio` is measured under this corrected partition**; a figure measured
  under the `{0, 1}` partition may not be carried over.

  **Both statistics are the same quantile of their own population.** `B` = the 99.9th percentile of `d`
  over the boundary population; `P` = the 99.9th percentile of `d` over the interior population. The
  earlier form of this criterion compared a *maximum* over the boundary population against a
  *percentile* over the interior one; because a max over `N` draws sits near the `1 − 1/N` quantile,
  that comparison put the boundary statistic at roughly the 99.9989th percentile for a 60 s render and
  could go red on a perfectly smooth implementation from extreme-value statistics alone. Like is now
  compared with like.

  Thresholds: (a) `B <= kBoundaryRatio · P`, where **`kBoundaryRatio` is measured, not asserted**: the
  build renders the (a) configuration across **≥ 8 seeds**, records the observed distribution of `B/P`
  in `compliance.md`, and sets `kBoundaryRatio` above the observed maximum with margin. The provisional
  figure carried into the build is **1.5**; if measurement puts the null distribution above it the
  bound is re-derived from the measurement (and the (c) arm re-verified against the new bound), never
  quietly widened past what (c) can still discriminate.
  (b) **control arm, `setWanderEnabled(false)` — directional and scalar.** Two assertions, both on
  numbers: `B_off / P_off <= 1.05` (with no retuning happening at all, the boundary population is
  statistically indistinguishable from the interior one), **and — correction C-14 — the control arm's
  interior curvature equals the drive sine's own closed form**:
  `| P_off / A_off − 4·sin²(π·f_drive/fs) | / (4·sin²(π·f_drive/fs)) <= 0.10`, where `A_off` is the
  same 99.9th percentile of `|x[n−1]|` over the same interior population. For `x[n] = A·sin(ωn)` the
  estimator is exact rather than approximate — `|x[n] − 2x[n−1] + x[n−2]| = 4·sin²(ω/2)·|x[n−1]|` — so
  this says the control arm's interior population is the drive's own curvature and **nothing else**:
  no residual retuning, no numerical junk, no partition drift. That is what "the estimator is reading
  retuning rather than the drive" means as a number. Measured: 1.0003 of the closed form across three
  seeds and two wet trims. A third, directional assertion survives from the original form:
  `P_on > P_off` — switching retuning on can only add curvature to the interior population.

  **Correction C-14 — what this assertion used to say, and why no build could satisfy it.** The
  original form was `|P_on − P_off| / P_off <= 0.10` on the raw statistic, justified as "the
  underlying signal is the same sine in both arms, so the interior curvature must not move". The
  premise is false. `P` is a level statistic as much as a curvature one (the identity above), and the
  two arms do not carry the same output level **by construction**: (a)'s mandated patch runs all four
  lanes at maximum depth, so the on arm's gain lane alone swings the peak level over [−24, +24] dB
  around base and its frequency lane sweeps resonances onto and off the drive. The input sine is the
  same; the output is not, and cannot be. Measured, the ratio is **210.8** at the provisional +30 dB
  trim (where the on arm is also clipping) and still **14.7** on a perfectly linear render — three and
  a half orders of magnitude, then 147×, from a bound of 0.10. What *is* invariant is the curvature
  per unit amplitude: 0.9907 (on) against 1.0004 (off) of the closed form, i.e. the two arms agree to
  0.97 %, an order of magnitude inside the spec's own tolerance — which is the quantity the corrected
  assertion uses, and the reason that tolerance is unchanged. The form before both corrections
  compared an unnamed statistic against a *distribution* and asserted that the boundary statistic
  **stays the same** when retuning is switched off — a claim an estimator that measures nothing would
  also satisfy.
  (c) **injection check, mandatory, through the public surface — and systematic, not one-shot.** Call
  `setSlewCeilings(24.0f, 24.0f)` — an in-spec setting under FR-035, whose ceilings are settable
  precisely so this arm needs no `#ifdef` hook and no edit to the header under test — then apply a
  **±1-octave `setPeakAnchorHz` jump on every sixteenth control chunk for the full pinned 60 s render,
  with the side of each jump drawn from a coin rather than alternated** (**correction C-17**,
  measured; see below). `B / P` must exceed **`kInjectionRatio`, measured at 3.0**, and must also
  exceed `kBoundaryRatio` — an injection this arm passes is one the (a) arm would have gone red on,
  which is the arm's whole purpose. The injection has to be systematic rather than one-shot because
  `B` is the 99.9th percentile of a 135 000-sample boundary population at the pinned duration: a
  single jump contributes ≈ 3 outliers, which cannot move that percentile at all (correction C-12).

  **Correction C-17 — the original form of this arm, measured.** It specified an *alternating* jump on
  *every* control chunk with a bound of 10. Neither survived measurement:
  - *Every chunk, alternating* is a 750 Hz square modulation of twelve resonances sitting at
    110–880 Hz — modulation at ≈ 2f for the peaks near 375 Hz, i.e. a **parametric pump**. Measured on
    the shipped build, that render's wet sum passes 3.4e38, goes **non-finite 1.83 s in and stays
    non-finite for the remaining 97 % of the render**, at every wet trim (the trim is a post-sum
    multiply). The statistic was therefore computed over NaNs, through a `std::sort` whose comparator
    NaN makes non-transitive: the numbers that arm produced were undefined behaviour, not evidence.
    FR-018's non-finite guard (**correction C-15**) keeps the output finite, but the render still
    saturates the clamp at every trim, so it can never be the linear render the statistic needs.
    Drawing the side from a coin removes the coherence the pump needs; jumping once every sixteen
    chunks still perturbs ≈ 1 430 boundary triples against a 0.1 % tail of 135 samples.
  - *B/P > 10* is unreachable by **any** implementation with this estimator. An anchor jump does not
    put a lone spike on the boundary sample and stop: it re-tunes a resonator, and the resonator
    **rings** — broadband, past the end of the chunk — so each jump lifts ≈ 3 boundary samples **and**
    the ≈ 61 interior samples behind them. That is the same 3 : 61 ratio as the partition, so both
    tails rise together and `B/P` converges on the per-sample contrast between the coefficient-switch
    step and the ring it excites, not on the number of jumps. Measured across three seeds on the
    linear fixture: 4.01 / 4.00 / 4.66 for this arm, against a null (the (a) arm) of
    1.010 / 0.992 / 1.023. Making the jump bigger or more frequent moves it **down** (×8 jumps: 2.77;
    every fourth chunk: 1.32), because the extra ring dominates. `kInjectionRatio` is therefore set
    the way `kBoundaryRatio` is — below the observed minimum with margin — at **3.0**, still 3× above
    the null and 2× above `kBoundaryRatio`.

  **The fixture renders at a wet trim that keeps it linear, and all three arms assert
  `getClampEngagementCount() == 0` (correction C-17).** At FR-045's provisional
  `kDefaultWetGainDb = +30 dB` a 220 Hz sine parked on twelve resonances with the gain lane at its
  24 dB maximum drives **16–21 % of the render into FR-018's ±4.0 clamp** (measured 599 219 / 520 614 /
  465 382 engagements across three seeds), and a clipped render's second difference is the *clipper's*,
  in both populations at once — which pinned `B/P` at 1.06 and made the (a) arm unable to fail. Every
  statistic SC-002 defines is a ratio and is therefore trim-invariant while the render is linear, so
  the fixture runs at **−12 dB** (worst measured peak 0.23 against a clamp of 4.0) and checks
  linearity rather than assuming it. **One second of render is discarded before the pinned 60 s**: the
  FR-045 trim rides the same 50 ms `LinearRamp` as FR-019's normalisation and can only be set after
  `prepare`, so an unsettled render's first 2 400 samples — 0.08 % of it, landing on top of the 0.1 %
  tail both statistics are drawn from — are louder than the patch asks for. The discarded pass also
  primes the estimator's two-sample history, so the measured populations are the full 3/64 and 61/64
  of the pinned render (135 000 and 2 745 000) rather than losing `n = 0` and `n = 1`.
  (d) **gate ramp — the estimator is named, because the bound is written about a control value, not
  about audio.** Primary assertion, on `getPeakGate(i)` (FR-052) sampled at **single-sample block
  granularity** (`processBlock` called with `numSamples == 1`, which FR-003 admits): after
  `setPeakWake(i, 0 → 1)` the gate rises 0 → 1 in **50 ms ± 5 ms**, monotonically, and **no step
  between consecutive samples exceeds `1.05 / (kGainRampMs · 0.001 · fs)`** (= 4.375e-4 at 48 kHz).
  This is the exact quantity FR-041's `LinearRamp` produces and FR-044 multiplies, and it is reachable
  per sample only because FR-011 gives each peak its own bank. Secondary, coarser, audio-domain
  assertion, with its extraction named: drive the network with a sine at that peak's centre frequency
  with all other peaks dormant, take the **per-cycle peak magnitude** over consecutive windows of
  `round(fs / f_peak)` samples, and require that envelope to reach **90 % of its settled value within
  50 ms ± 5 ms**. No per-sample bound is asserted on the audio arm: the observable there is an
  oscillating waveform, and any follower introduced to extract an envelope from it would impose its own
  smoothing and dominate the step statistic.
  Measured by: `ResonanceDriftNetwork_NoZipperUnderDrift`.
- **SC-003 — Wander-rate spectral tests (roadmap line 227).**
  Metric and threshold shape are inherited from the Phase 2 SC-002 rewrite, which was
  measurement-corrected after the first-zero-crossing-lag estimator proved undiscriminating
  (`specs/vorago-phase2-noise-organism/compliance.md:79-84`): a **fixed-lag autocorrelation**, not a
  zero-crossing lag. Render at least `10·T` with `T = 1/wanderRate` (350 s at the 0.03 Hz default),
  drive = **pink noise at −12 dBFS**, **`mix = 1`**, FR-016 defaults otherwise. Every 100 ms extract the
  five band-energy fractions (`AudioFeatures::band`, `audio_features.h:28-29`).

  **Band selection is fixed, not "strongest".** `AudioFeatures`'s bands are the fixed edges
  `[20–100, 100–500, 500–2k, 2k–8k, 8k–Nyquist]` (`audio_features.h:28-29`), and the FR-016 default
  anchors top out at 1278 Hz, so bands 3 and 4 carry essentially no energy at `mix = 1`: a *fraction*
  computed on a near-zero denominator is numerically unstable and would win a naive "strongest"
  selection, making the criterion measure numerical noise; selecting the max over five bands post hoc
  also biases toward passing. The rule instead: **eligible** bands are those whose mean fraction over
  the wander-on render is **≥ 0.01** of total energy; among the eligible bands the **motion metric** is
  the coefficient of variation (CV) of the band-fraction trajectory; the band with the highest CV **in
  the wander-on arm** is selected, and **that same band index is used in every arm**, including the
  control. The selected index is recorded in `compliance.md`.

  Thresholds: (a) **wander on** — the normalised autocorrelation of the selected band's
  mean-removed trajectory at a lag of `T/8` **seconds** is ≥ **0.20**, the Phase 2-measured bound;
  (b) **control arm, `setWanderEnabled(false)`** — the selected band's fraction CV is at least
  **1.8×** below the wander-on arm's, the Phase 2-measured multiplier; (c) **bounds** (the roadmap's
  "bounded around anchor ratios", line 220) — sampling `getPeakCurrentFrequency(i)` every control step
  over the whole render, every reading lies inside
  `[anchor · 2^(−semitones/12), anchor · 2^(+semitones/12)]` intersected with `[20, 0.45·fs]`, and
  every `getPeakCurrentQ(i)` inside `[baseQ · 2^(−amount), baseQ · 2^(+amount)]` intersected with
  `[0.1, 100]`, with **zero** excursions in at least 10^5 samplings; (d) each peak's realised frequency
  trajectory has a lag-`T/8` autocorrelation ≥ 0.20 and a lag-`8T` autocorrelation ≤ 0.10 — the walk is
  neither frozen nor white at the configured rate; (e) **independence, measured against a null
  distribution and against an in-process control.** The earlier form of this arm ("mean absolute
  pairwise Pearson correlation across the twelve frequency trajectories ≤ 0.10") **fails on a correct,
  fully decorrelated implementation** and is replaced. Two independent AR(1) series with retention
  `a = exp(−dt/τ)` have, by Bartlett's formula, `var(r) ≈ (1/N)·Σ_k ρ(k)² ≈ (τ/dt)/N`; at the default
  `τ = 30 s`, `dt = kControlChunkSamples/fs = 1.333 ms` and the 350 s render's `N = 262 500` that is
  `0.086`, so `sd(r) ≈ 0.29` and `E|r| ≈ 0.29·√(2/π) ≈ 0.23` — more than twice the old bound — purely
  from estimator noise. The old criterion measured render length, not the FR-005 salts. The arm is now:
  - the statistic is the mean absolute pairwise Pearson `r` over the `C(12,2) = 66` pairs of
    **mean-removed `log2(getPeakCurrentFrequency(i))` lane trajectories**, sampled once per control
    step (the lane value, not the band-energy trajectory);
  - the threshold `kLaneIndependenceR` is **derived from a measured null distribution**: render the
    configuration under **≥ 12 network seeds**, record mean `|r|` and its spread in `compliance.md`,
    and set the bound above the observed maximum with margin — never transcribed from Phase 2, whose
    0.05 was measured on 10 s of *broadband audio* (≈ 480 000 effectively independent points,
    `noise_organism_test.cpp:2966-3001`), a different statistic on a different population;
  - **anti-vacuity control, in the same process** (the Phase 2 pattern, `noise_organism_test.cpp`
    "anti-vacuity control" section): the identical estimator applied to twelve lanes given the **same**
    seed and salt must read ≈ **1.0** (`≥ 0.95`). Without it the criterion cannot distinguish "the
    salts work" from "the estimator is noise".
  (f) **`setWanderRate` is not inert.** Two renders at well-separated rates — **0.005 Hz**
  (`T = 200 s`, `decimation = 7` under FR-037) and **0.3 Hz** (`T = 3.33 s`, `decimation = 1`), each at
  least `10·T` long — must produce peak-frequency trajectories whose **lag-matched** autocorrelations
  differ by at least a **measured** factor: evaluate both trajectories' autocorrelation at the *same*
  absolute lag of **1.67 s** (`T/2` of the fast arm) and require
  `ρ_slow(1.67 s) − ρ_fast(1.67 s) ≥ kRateSeparation`, with `kRateSeparation` set from the measured
  distribution across ≥ 8 seeds and recorded. This is the arm that fails if `setWanderRate` is a no-op
  across `[0.002, 0.0333]` Hz — the dead zone FR-037 exists to remove — which every other criterion in
  this spec would pass through unchanged.
  Measured by: `ResonanceDriftNetwork_WanderRateSpectral`, tagged `[long]`.
- **SC-004 — CPU ≤ 0.75 % per voice (roadmap line 228).**
  Reference configuration: 12 peaks, `AnchorMode::Hybrid` with `gravity = 0.5`, all four lanes
  (frequency, Q, gain, pan — FR-038) at their FR-016 defaults, wander enabled, `mix = 1`, all peaks
  awake, drive identical on both channels (Success Criteria stereo convention).
  Thresholds: (a) the reference configuration measures **≤ 80 000 ns** per 512-sample block at 48 kHz;
  (b) **Amended 2026-09-11 (user decision, compliance gap SC-004):** a wander-disabled arm measures
  **below** the reference arm, and the saving is recorded as a **transcribed measurement**, not gated
  by a percentage. The original "at least 10 %" floor was written before Clarifications Q6/Q7 and the
  roadmap's Dormancy rule fixed that the 48 lanes keep advancing with wander off (FR-036); the only
  cost wander-off can remove is therefore FR-015's control-write term, which the FR-060 probe and the
  isolated SC-004 run both put at **4–6 %** of the reference (measured 4.83 % isolated, 3.38 % under
  suite load) — structurally short of 10 % and not a defect. The directional clause still rejects a
  change-detection path that saves nothing; the 40 % arm in (c) remains the real dormancy criterion.
  (c) an all-dormant arm measures **at least 40 %
  below** the reference arm, which is FR-042's "stop calling `bank_[i].process`" skip **and** its
  control-write skip (Clarifications Q6 — the network also stops calling `setFrequency`/`setQ`/
  `setGain` for a dormant peak, not just `process`) earning their keep.
  The arm renders with all twelve peaks dormant **and settled** (≥ 50 ms of rendering after
  `setPeakDormant(i, true)` on every peak, so every gate ramp has reached exactly zero and every sleep
  edge has fired); `isPeakEngineActive(i)` is `false` for all twelve before timing starts, asserted
  rather than assumed. The output is exactly `0.0f` on both channels throughout at `mix = 1` (FR-019,
  FR-044), which the arm also checks so a "cheap" measurement cannot come from an accidental early
  return;
  (d) each arm carries a checked-in baseline with `static_assert(kBaseline * 1.5 <= 80000.0)` and
  `static_assert(kBaseline >= 80000.0 / 50.0)` (`noise_organism_perf_test.cpp:1501-1562` idiom), so the
  absolute ceiling is enforced at compile time on every CI leg even though the timing case is
  `[.perf]`-hidden.
  Trial shape: best-of-25 × 500 blocks after 400 warm-up blocks, run in isolation
  (`node tools/run-cpu-tests.js dsp_systems_tests`).
  Measured by: `ResonanceDriftNetwork_CpuBudget` (`[.perf]`), informed by
  `ResonanceDriftNetwork_StageCostProbe` (`[.perf]`, FR-060).
- **SC-005 — Zero allocation after prepare (roadmap line 481).**
  Threshold: **0 allocations** over 20 000 blocks of mixed sizes (1, 63, 64, 65, 512, 2048, 4096) that
  also walk the entire setter surface in declaration order (including `setWanderRate` across values
  that change FR-037's decimation, and `setSlewCeilings` across its clamps), switch anchor mode in all
  six directions, move `setNumPeaks` up and down, toggle dormancy on every peak so FR-042's sleep-edge
  `reset()` + re-apply fires on all twelve, and call `reset()` — all inside an `AllocationScope`
  (`allocation_detector.h:111`).
  Measured by: `ResonanceDriftNetwork_NoAllocationAfterPrepare`.
- **SC-006 — Seed determinism (roadmap lines 171, 342 — the determinism practice every Vorago stochastic component inherits).**
  Thresholds: (a) two instances with the same seed, configuration and sample rate, fed the same input,
  render **`max|diff| == 0`** over 10 s; (b) **two instances differing only in seed decorrelate —
  measured over enough correlation times to be a statistic, against a measured null.** The comparison
  is made on the *trajectories*, not on the audio, because both instances filter the same input and
  their outputs are therefore correlated by construction regardless of seed (recorded so a later reader
  does not "fix" this criterion by correlating audio). The earlier form of this arm asked for mean
  `|r| ≤ 0.05` over the 10 s render of (a); that is unreachable by a correct implementation — the
  trajectories are OU walks whose decorrelation time is `τ = 30 s` at the FR-016 default
  (`brownian_drift.h:99`), so a 10 s window holds **one third of one correlation time** and each
  trajectory is a single smooth excursion, two of which typically read `|r| ≈ 0.5–0.9`. The 0.05 figure
  was imported from Phase 2's audio-domain SC-017 (`noise_organism_test.cpp:2966-3001`, ≈ 480 000
  effectively independent points of white noise), a different statistic on a different population. The
  arm is now:
  - **Render duration ≥ 20·τ_effective**, stated explicitly: either **600 s** at the FR-016 default
    rate, or — the cheaper option, permitted and to be recorded in `compliance.md` — a render of
    **≥ 20·τ** at a raised `wanderRate` chosen so `τ` shrinks (e.g. `wanderRate = 1.0 Hz` ⇒ `τ = 1 s`
    ⇒ a 20 s render). Whichever is used, the duration and the resulting `τ` are transcribed.
  - **Pairing is defined:** peak *i* of instance A against peak *i* of instance B, 12 pairs — **not**
    all 24 trajectories cross-paired.
  - **Threshold `kSeedDecorrelationR` is derived from a measured null distribution** across **≥ 12 seed
    pairs**: report mean `|r|` and its spread, then set the bound above the observed maximum with
    margin. It is not transcribed from Phase 2.
  - **Anti-vacuity control in the same process:** the same estimator on a **same-seed** pair must read
    ≈ **1.0** (`≥ 0.95`).
  (c) the audio of the two
  differently-seeded instances nevertheless differs measurably: the RMS of their difference is
  ≥ **−30 dB** relative to the RMS of either render; (d) `reset()` with no setter called since
  `prepare` reproduces the post-`prepare` stream exactly (`max|diff| == 0`).
  Measured by: `ResonanceDriftNetwork_SeedDeterminism`.
- **SC-007 — `reset()` is configuration-preserving (FR-004).**
  Thresholds: (a) after a full configuration (non-default anchors, Q, levels, mode, ratios) followed by
  `reset()`, the render is **non-silent** (RMS > −60 dBFS on a −12 dBFS drive) — the arm that catches a
  `ResonatorBank::reset()` forwarded without the FR-004 re-apply, which would leave every resonator
  disabled (`resonator_bank.h:226-231`) and the output at digital silence; (b) that render is
  sample-exactly equal to a fresh `prepare` plus the same setter sequence; (c) every configuration
  getter returns its pre-`reset` value.
  Measured by: `ResonanceDriftNetwork_ResetPreservesConfiguration`.
- **SC-008 — Sample-rate independence (cross-cutting: everything is sized and derived at `prepare`, roadmap lines 480–481).**
  Thresholds, across 44 100 / 48 000 / 96 000 Hz with identical configuration and seed: (a) realised
  anchor frequencies (`getPeakCurrentFrequency`, wander disabled) agree within **0.1 %** for every peak
  whose anchor is below `0.45 · 44 100`; (b) the FR-041 gate ramp measures 50 ms ± 5 ms at every rate,
  measured on `getPeakGate(i)` by SC-002 (d)'s named estimator;
  (c) **drift is defined in seconds, not samples — asserted on a rate-invariant quantity, not on three
  independent random draws.** The earlier form compared the lag-`T/8` autocorrelation *estimate* across
  44.1/48/96 kHz within ±15 %. That is not a well-posed comparison: `BrownianDrift::prepare` recomputes
  `controlDtSeconds_ = kControlRateInterval / sampleRate_` (`brownian_drift.h:121-128`) and
  `updateCoefficients` derives `a = exp(−controlDtSeconds_/τ)`, so the same seed produces the *same
  draw sequence consumed at a different cadence* — three different realisations of one process, whose
  estimator spread across realisations is comparable to the tolerance being asserted. Instead: over a
  render of **≥ 50·τ** at each rate (stated in the test, e.g. 20 s at `wanderRate = 1.0 Hz` ⇒
  `τ = 1 s`), estimate the **1/e decorrelation lag** of a peak's `log2` frequency trajectory — the
  smallest lag in **seconds** at which the normalised autocorrelation falls to `1/e` — and require the
  three rates' estimates to agree within a tolerance **measured across ≥ 8 seeds at one fixed rate**
  and set above the observed spread, so realisation noise is inside the bound rather than being
  mistaken for a rate dependency. The measured spread and the resulting tolerance go in
  `compliance.md`; the provisional figure carried into the build is ±15 %, replaced by measurement if
  the null distribution is wider. **A hardcoded sample rate must move the statistic** — verified by
  injection, the Phase 2 SC-008 (c) precedent
  (`specs/vorago-phase2-noise-organism/compliance.md`, "Thresholds changed during this phase");
  (d) at 44.1 kHz an anchor configured
  above `0.45 · fs` is clamped silently and reported clamped by the read surface, with no non-finite
  value and no clamp-counter engagement (FR-025).
  Measured by: `ResonanceDriftNetwork_SampleRateIndependence`.
- **SC-009 — Non-finite immunity (roadmap line 495).**
  Non-finite values are constructed from **bit patterns through a volatile sink**, never
  `std::numeric_limits::quiet_NaN()` (macOS CI is `-ffast-math`), in a TU registered in the
  `-fno-fast-math` block (`dsp/tests/CMakeLists.txt:780-800`).
  Thresholds: (a) a NaN or Inf **input sample** leaves every output sample finite, and the next block
  of finite input renders normally (FR-009); (b) NaN/Inf passed to **every** setter is rejected and the
  previously configured value is still reported by the matching getter (FR-008); (c) after every
  injection `getClampEngagementCount()` is unchanged.
  Measured by: `ResonanceDriftNetwork_NonFiniteGuards` — the only Phase 3 TU in the `-fno-fast-math`
  block; the other three stay out so their guards are proved in the FP mode the header ships in.
- **SC-010 — Block-size invariance (FR-007).**
  Threshold: the same total render delivered as 512-sample blocks and as an irregular partition
  (1, 63, 64, 65, 200, 512, 1024, …) differs by `max|diff| <= kSampleTolerance = 5.0e-4`
  (`render_fingerprint.h:58`) — the control grid is absolute, so the only admissible difference is
  floating-point summation order.
  Measured by: `ResonanceDriftNetwork_BlockSizeInvariance`.
- **SC-011 — Prepare footprint (FR-062).**
  Threshold: `getAllocatedBytes() == 0` **exactly**, and `prepare` performs exactly **0** allocations —
  both re-derived from FR-062's declaration that the prepare-time heap footprint is zero bytes, so
  nothing has to be transcribed from the implementation and nothing is computed from
  `PrepareConfig::maxBlockSamples`, which sizes nothing here. There is no dry-path scratch buffer, and
  the twelve banks and 48 lanes (FR-038 adds the pan lane) are fixed-size members with no heap state
  (`resonator_bank.h:184-209`, `brownian_drift.h:121-128`). Checked at
  `maxBlockSamples` = 64, 2048 and 8192 so the **relationship** — a footprint independent of the
  configured block size — and not one value, is what passes. If a buffer is ever needed, FR-062's
  declaration changes first and this criterion is re-derived from it.
  Measured by: `ResonanceDriftNetwork_PrepareFootprint`.
- **SC-012 — Lints and portability (roadmap lines 486, 496).**
  Thresholds: `node tools/lint-odr.js`, `node tools/lint-layers.js`,
  `node tools/lint-nonfinite-symbols.js`, `node tools/lint-float-bit-goldens.js` and
  `node tools/check-portability.js` all pass; `./tools/run-clang-tidy.ps1 -Target dsp` reports zero new
  warnings; the build is warning-free on MSVC, GCC and AppleClang.
  Measured by: the commands themselves, transcribed into `compliance.md`.
- **SC-013 — No shipped component regressed (roadmap line 498), restated as "no behavioural change"
  under Clarifications Q3.** This phase plans **no** edit to any shipped header **unless FR-013's
  probe-gated fallback is taken**. Thresholds, in the default (no-fallback) case: `dsp_core_tests`,
  `dsp_primitives_tests`, `dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`,
  `membrum_tests`, `innexus_tests` and `seraphis_tests` green with **no edits to existing cases**, and
  `git diff --stat` showing no change under `dsp/include/` outside the one new header.

  **If FR-013's Tier 1 (additive `ResonatorBank` method) or Tier 2 (SIMD kernel) is taken**, the bar is
  **no behavioural change to any shipped component**, not merely "no edits": `ResonatorBank` (or, for
  Tier 2, `sympathetic_resonance_test.cpp`'s kernel) is touched, so the enforcing gate is running the
  full consumer-suite set — `membrum_tests`, `innexus_tests`, `dsp_processors_tests`,
  `dsp_systems_tests` and `seraphis_tests` — **before and after** the change and diffing the results.
  **Any moved result in those suites is a regression to investigate and surface, never a golden to
  update.** This corrects an earlier draft of this spec's Assumption 1, which claimed `ResonatorBank`
  had no consumer outside its own test; the corrected consumer list — every Membrum body, Innexus,
  `ContinuousBody`, `NoiseOrganism`, `iresonator.h` and `modal_resonator_bank_simd.h` — is why the
  before/after suite run replaces a blast-radius argument.
  Measured by: the suite runs, before and after if either tier is taken, transcribed into
  `compliance.md`.
- **SC-014 — Anchor modes are correct (FR-021 – FR-023).**
  Configuration under test, stated so every expected value is computable by the test rather than
  transcribed: FR-016 defaults throughout — `noteFrequency = 55 Hz`, the twelve keyed ratios of
  FR-016's table (grid `27.5 … 660 Hz`), the twelve free anchors of FR-016's table
  (`40 … 1278 Hz`) — 48 kHz, wander disabled so anchors are directly readable.

  **Settling clause, applying to (a)–(f) alike.** Every threshold below reads FR-052's applied-state
  getters *after* a setter, and FR-035's limiter caps the applied frequency at `freqSlewCeiling`
  (default 0.02) octaves per 64-sample control step — a one-octave move therefore needs 50 control
  steps, ≈ 67 ms at 48 kHz. Each arm renders **≥ 100 control chunks (6400 samples) after the last
  setter and before the first read**, so the limiter has converged. A test that reads
  `getPeakCurrentFrequency` immediately after `setNoteFrequency` reads the *old* value and proves
  nothing.

  Thresholds: (a) **Free** — `getPeakCurrentFrequency(i)` equals the configured anchor within 1 cent
  for all 12 peaks; (b) **Keyed** — it equals `noteHz × ratio[i]` within 1 cent, and an **upward**
  one-octave `setNoteFrequency` change (55 → 110 Hz) moves every peak by 1200 ± 2 cents. **The
  direction is specified because the downward direction cannot pass**: 55 → 27.5 Hz puts peak 0's
  anchor at `27.5 × 0.5 = 13.75 Hz`, which FR-021/FR-025 clamp to
  `kMinResonatorFrequency = 20.0f` (`resonator_bank.h:42`), a move of
  `1200·log2(20/27.5) = −551` cents. A downward arm is included only as an explicit clamp check —
  peaks whose target falls below 20 Hz are named in the test and asserted to sit **at** 20 Hz, and the
  1200-cent assertion applies to the remaining peaks;
  (c) **Hybrid at `g = 0`** — every peak is **bit-equal** to the Free-mode result (the identity
  property, FR-023);
  (d) **Hybrid at `g = 1` — index-paired, rewritten under Clarifications Q1 to fail on the collision an
  earlier draft let through.** Two assertions: **first**, every peak *i*'s `getPeakCurrentFrequency(i)`
  is within 1 cent of **its own** `noteHz × ratio[i]` (FR-022's per-peak keyed anchor, not "the nearest
  keyed grid frequency"); **second**, the twelve resulting applied frequencies are **pairwise
  distinct** (no two peaks agree to within 1 cent of each other) — the assertion that catches exactly
  the defect the earlier nearest-neighbour law produced (peaks 0/1 both landing on 55 Hz, peaks 9/10/11
  all landing on 660 Hz for the FR-016 defaults). An **intermediate** `g` (e.g. `g = 0.5`) is also
  checked: every peak sits at the closed-form log-interpolated point
  `exp(log(f_free[i]) + g·(log(f_keyed[i]) − log(f_free[i])))` within 1 cent — a closed form with no
  tolerance games, since both `f_free[i]` and `f_keyed[i]` are read directly off the FR-016 tables;
  (e) **Hybrid at `g = −1`** — every peak is within 1 cent of the closed form
  `exp(2·log(f_free[i]) − log(f_keyed[i]))` (FR-023's per-peak log-mirror point). The earlier
  shared-grid `f_anti` concept, its bracketing-interval search and its repeated-extension rule for
  free anchors 9, 10 and 11 (all above the keyed grid top of 660 Hz for the FR-016 defaults) are
  **deleted**; this closed form needs no grid, no bracketing and no extension, and is defined
  identically for every peak regardless of where its free anchor sits relative to any other peak's;
  (f) **monotonicity** — sweeping `g` from −1 to +1 in 0.05 steps, with the settling clause applied at
  every step, moves each peak's `log2(f)` monotonically, with no reversal and no step larger than
  `freqSlewCeiling`.
  Measured by: `ResonanceDriftNetwork_AnchorModes`.
- **SC-015 — Peak life cycle and dormancy (FR-040 – FR-044).**
  Thresholds: (a) **after ≥ 50 ms of rendering at `wake == 0`** — the precondition, so the FR-041
  `LinearRamp` has actually run to exactly `0.0f` and FR-042's sleep edge has fired — the peak
  contributes **exactly zero on both channels**: sweeping that peak's anchor across a sine's frequency
  changes the output by `max|diff| == 0` on `outL` and `outR`, and **`isPeakEngineActive(i)` (FR-052)
  reads `false`**. The criterion is
  stated against that getter, not against `ResonatorBank::isEnabled` (`resonator_bank.h:410`), which is
  unreachable from a test because FR-011 makes the banks private members; (b) **lanes freewheel while
  dormant**:
  `getPeakCurrentFrequency(i)` sampled before and after a 120 s dormant interval differs by ≥ 20 cents
  for at least 9 of 12 peaks at the default wander settings (the Dormancy rule's "modulation lanes keep
  running", roadmap lines 490–491); (c) **extended to both channels (Clarifications Q4).** With every
  peak dormant and `mix = 1` the output is digital silence on both `outL` and `outR`, and with
  `mix = 0` **both** `outL` and `outR` equal their respective input channels unchanged within
  `kSampleTolerance` (FR-043, FR-044) — this holds for any per-peak pan configuration, since a
  fully-dormant network never reaches the pan step at all;
  (d) `setPeakDormant(i, true)` and `setPeakWake(i, 0)` produce `max|diff| == 0` renders on both
  channels and are
  distinguished only by `isPeakDormant`/`getPeakWakeAmount` — the cross-cutting rule's "differ only on
  the read surface"; (e) `setNumPeaks` reduced mid-render silences the dropped peaks over the FR-041
  ramp with no envelope discontinuity above SC-002 (d)'s per-sample bound, measured on
  `getPeakGate(i)`;
  (f) **wake after long dormancy is silent, not a burst** (FR-042's state clear). Configuration:
  `peakQ = 100` (where the peak's own `RT60 = Q·ln1000/(π·f) = 5.5 s` at a 40 Hz anchor), one peak
  driven to steady state, then made dormant for **120 s** with wander at the FR-016 defaults so its
  anchor moves meanwhile, then woken. Thresholds: the woken peak's gate satisfies SC-002 (d)'s
  per-sample bound, **and** the peak's contribution never exceeds its own pre-dormancy steady-state
  peak magnitude at any point in the first **5 s** after the wake. Without FR-042's `reset()` + re-apply
  this arm goes red: seconds of stored ring would be released into drifted coefficients, and the 50 ms
  gate would attenuate only its first 50 ms.
  **Extended (Clarifications Q6): the wake-edge snap write is asserted against the bank, not only the
  network's echo.** Immediately after the wake edge, with the peak briefly held at `wake = 0.0` one
  control step longer than usual (a test-only hold, achieved entirely through the public surface), the
  test confirms `getPeakCurrentFrequency`/`getPeakCurrentQ`/`getPeakCurrentGainDb` already reflect the
  post-dormancy drifted value *before* the gate lifts — i.e. the snap write already reached `bank_[i]`
  on the control step the gate target left zero, not merely that the network's own bookkeeping updated;
  a build that defers the snap to the following control step, or that never writes it at all, must fail
  this arm even though FR-052's echo alone (Q6's "network keeps computing and storing") would still
  look correct;
  (g) **the bed does not move on wake/sleep, and moves by the derived amount on `setNumPeaks`**
  (FR-019). Over a scripted wake/sleep sequence — peaks woken and slept one at a time on a 20–90 s
  pattern with `numPeaks` held at 12 — the total output RMS measured over 200 ms windows moves by no
  more than a bound **measured across ≥ 8 seeds during the build** and recorded, and no per-sample gate
  step exceeds SC-002 (d)'s bound. Separately, a mid-render `setNumPeaks(12 → 4)` moves the surviving
  peaks' level by `20·log10(sqrt(12/4)) = 4.77 dB ± 0.5 dB` — asserted as the **expected** consequence
  of FR-019's `1/sqrt(numPeaks)` law rather than left invisible, since SC-015 (e) only watches the
  *dropped* peaks.
  Measured by: `ResonanceDriftNetwork_PeakLifeCycle`. Tagged `[long]`: (b)'s 120 s dormant interval
  and (f)'s 120 s + 5 s render are ≈ 5.8 M samples each, well past CLAUDE.md's ~15 s threshold, and
  every assertion here is a property of the seeded walk and of exact zeros — toolchain-independent, so
  it belongs in the nightly lane rather than the per-push one.
- **SC-016 — 30-minute boundedness soak (roadmap line 483).**
  Configuration: every wander depth and `wanderRate` at maximum, `peakQ` at 100, an external wake
  pattern toggling peaks on a 20–90 s stochastic schedule (the `SlowEventScheduler` default range,
  `slow_event_scheduler.h:164-165`), drive = pink noise at −12 dBFS, 30 minutes at 48 kHz.
  Thresholds: (a) **to be measured, not transcribed** — every 10 s window's RMS is within
  `±kSoakWindowDb` of the median window, where `kSoakWindowDb` is set **during the build from a soak
  across ≥ 8 seeds**, above the observed extreme, with the measured distribution recorded in
  `compliance.md`. The provisional figure carried in is ±6 dB, and it is explicitly *not* trusted: this
  is the most extreme configuration in the spec (gainWander 24 dB and freqWander 24 semitones per peak,
  `wanderRate` at its 1.0 Hz ceiling, an external wake pattern toggling peaks, and FR-019's
  normalisation constant only because `numPeaks` is), and Phase 2 had to widen its analogous bound from
  ±3.0 to ±4.5 dB for a *less* extreme configuration after measuring
  (`specs/vorago-phase2-noise-organism/compliance.md`, "Thresholds changed during this phase" —
  "3.0 sat *below* the observed max (3.247) over 24 seeds — it passed on seed luck");
  (b) the least-squares slope of window RMS in dB against time is within **±0.5 dB per 30 minutes** —
  no creep in either direction; (c) **also to be measured** — no window below `kSoakFloorDbfs` (the
  resonance did not die) and none above −3 dBFS (it did not run away). The provisional floor is
  −60 dBFS and is likewise not trusted: a Q = 100 bank on a −12 dBFS pink drive, after FR-019's
  `1/sqrt(12)` and FR-016's −6 dB default `peakLevel`, may legitimately sit near it, so the floor is
  set below the observed minimum across the same ≥ 8 seeds and recorded. **Neither (a) nor (c) may be
  widened after a red run without re-deriving it from the measured distribution and recording the
  change** — the Bound provenance note below binds both; (d) zero non-finite samples and
  `getClampEngagementCount() == 0`.
  Measured by: `ResonanceDriftNetwork_LongRenderBoundedness`, tagged `[long]`.

- **SC-017 — Realised Q matches reported Q (FR-014, FR-015, FR-031, FR-032).**
  **The criterion the spec was missing, and the one D-9 needs.** A wrong write order, or FR-015's
  change detection applied to Q, is *silent*: the audio still sounds plausible and
  `getPeakCurrentQ(i)` still reports the intended number, because per FR-052 it is the network's own
  applied value and not a read of the filter. Every existing Q criterion — SC-003 (c), SC-014 — reads
  that echo; SC-001 (d)'s ring-out is an upper bound and passes with a *shorter* decay. So a build in
  which `setFrequency` reprogrammed Q to `rt60ToQ(f, kDefaultDecayTime) = 0.4548·f`
  (`resonator_bank.h:333`, `:60`) — Q ≈ 18 at 40 Hz instead of 12, clamped to 100 above ≈ 220 Hz —
  would pass this spec entirely. Phase 2 had exactly this check
  (`specs/vorago-phase2-noise-organism/compliance.md:68`, SC-021: "reported-Q vs realised-bandwidth
  agreement within 25 %"); Phase 3 restores it.
  Metric: drive one peak in isolation (all others dormant) with white noise at −12 dBFS, `mix = 1`,
  wander disabled, and estimate that peak's **−3 dB bandwidth `BW`** from the output magnitude
  spectrum. Require `f / BW` to agree with `getPeakCurrentQ(i)` within **25 %** — the Phase 2-measured
  tolerance, re-verified here across the grid below and re-derived from measurement if it proves unable
  to discriminate.
  Coverage: **≥ 3 Q settings** (`peakQ` = 2, 12, 100) × **≥ 3 anchor frequencies** (40, 265, 1278 Hz,
  the FR-016 defaults at the bottom, middle and top of the table), each with the SC-014 settling clause
  applied, and each with the frequency written **before** the Q read so the FR-014 order is actually
  exercised.
  **Injection arm, mandatory:** a build that swaps the FR-014 write order (Q first, then frequency), or
  that lets FR-015's change detection skip an "unchanged" Q after a frequency write, must turn this
  criterion **red** — recorded in `compliance.md` with the measured `f/BW` from the mutated build, and
  the unmutated build re-verified afterwards. Also asserted: `getPeakEquivalentRt60(i)` is consistent
  with the realised `f/BW` within the same tolerance, and the network never calls
  `ResonatorBank::setDecay` — checked by requiring `getPeakEquivalentRt60(i)` to change when `peakQ`
  changes at a fixed anchor, which it cannot do if Q is being derived from a stale decay table
  (FR-032).
  Measured by: `ResonanceDriftNetwork_RealisedQ`.
- **SC-018 — The slew limiter engages, and at the specified rate (FR-035).**
  Without this the limiter has no criterion that can fail: SC-014 (f)'s "no step larger than the FR-035
  slew limit" is true by construction of any implementation that has a limiter at all; SC-002 (c) runs
  with the ceiling deliberately opened to 24 octaves/step; and SC-002 (a)/(b) run at in-spec drift
  rates which FR-035 itself says the limiter never shapes. A missing, inverted or mis-signed limiter
  would pass every other criterion in this spec.
  Metric: `AnchorMode::Keyed`, FR-016 defaults, wander disabled, default `freqSlewCeiling = 0.02`;
  apply an upward one-octave `setNoteFrequency` step (55 → 110 Hz) and sample
  `getPeakCurrentFrequency(i)` **once per control step** across the transition.
  Thresholds: (a) the per-control-step change in `log2(f)` **never exceeds `freqSlewCeiling`** for any
  peak, at any step; (b) each peak reaches its new target within **50 ± 2 control steps**
  (`1.0 / 0.02 = 50`), which fails if the limiter is absent (1 step), slower than specified, or applied
  in the wrong domain; (c) the SC-002 `B/P` statistic computed across that transition stays under
  SC-002 (a)'s bound — the limiter's actual purpose is that a note change does not step the
  coefficients; (d) the same three assertions with `setSlewCeilings(0.005f, 0.005f)` give
  200 ± 5 control steps, proving the ceiling is a real control and not a hardcoded constant.
  Measured by: `ResonanceDriftNetwork_SlewLimit`.
- **SC-019 — Render-path boundaries and guard ladder (FR-003).**
  The Edge Cases section states these and the traceability table previously mapped them to no
  criterion, so they would not have appeared as compliance rows. **Stereo signature (Clarifications
  Q4):** every threshold below is stated against `processBlock(inL, inR, outL, outR, n)`'s four
  pointers, not the mono two-pointer signature an earlier draft of this spec carried.
  Thresholds: (a) **in-place equality** — a render with `inL == outL` and `inR == outR` equals the
  out-of-place render of the same input from the same state with `max|diff| == 0` on both channels (the
  implementation must capture both dry samples before the banks write); the cross-aliased case
  (`inL == outR`, `inR == outL`) gives the same guarantee; (b) **any** of the four pointers null (each
  case tried independently) writes nothing on either channel and advances nothing — asserted by
  rendering a reference block, calling with one pointer null, then rendering again and requiring
  `max|diff| == 0` on both channels against the reference continuation; (c) `numSamples == 0` is a
  no-op that consumes **no** control step — same before/after equality construction, repeated 1000
  times so a one-sample-per-call drift would show; (d) `processBlock` **before `prepare`** writes
  exactly `numSamples` zeros to both `outL` and `outR` and advances nothing; (e) `numSamples` far above
  `maxBlockSamples` (65 536 with `maxBlockSamples = 64`) renders correctly on both channels and inside
  an `AllocationScope` (`allocation_detector.h:111`) performs **0** allocations.
  Measured by: `ResonanceDriftNetwork_RenderPathBoundaries`.
- **SC-020 — Wet-gain trim gives a usable blend (FR-045, Clarifications Q2).**
  Thresholds: (a) **default-trim level check.** Render the FR-016 reference patch (`numPeaks = 12`, all
  defaults, `AnchorMode::Free`, `mix = 1`) driven by SC-001's broadband source (white noise at
  −12 dBFS); the wet RMS at the measured `kDefaultWetGainDb` lands within the window recorded in
  `compliance.md` from the FR-045 measurement (a few dB of the drive RMS, per FR-045's method) — this is
  the compliance row FR-045 promises, not a re-measurement; (b) **the wet path is a real, blend-sized
  signal — isolated directly, never by differencing renders.** With the same reference patch and drive
  at the default `wetGain`, render at `mix = 1` and at `mix = 0` and require
  `|RMS_wet_dB − RMS_dry_dB| <= 6 dB`, where `RMS_wet_dB` is the RMS of the `mix = 1` render and
  `RMS_dry_dB` that of the `mix = 0` render — the wet path measured on its own, which is what a
  network shipped at the earlier draft's implicit `wetGain = 0 dB` would fail. The earlier form
  isolated the wet contribution by differencing the `mix = 0.5` render against the `mix = 0` one,
  i.e. `0.5·wetTrimmed − 0.5·dry`; with the wet path muted that degenerates to `0.5·dry`, an RMS of
  exactly `dry − 6.02 dB`, landing inside the "within roughly 6 dB" pass window — so the criterion
  reported a healthy blend for the very failure it names. The `mix = 0.5` render is retained only as a
  **monotonicity** check: its RMS must lie between the `mix = 0` and `mix = 1` RMS values, within
  measurement tolerance, so the crossfade is verified monotone without any statistic being computed
  from a difference of renders; (c) **`setWetGain` is a real, clamped control**:
  `getWetGain()` echoes the last set value clamped to `[-24, +48]` dB, and a lower trim (e.g. 0 dB)
  measurably reduces the wet RMS relative to the default-trim render from (a) by the expected dB
  difference within 0.5 dB.
  Measured by: `ResonanceDriftNetwork_WetGainTrim`.
- **SC-021 — Per-peak pan is correct (FR-038, Clarifications Q4).**
  Configuration base: isolate one peak (all others dormant), drive with a sine at that peak's centre
  frequency, `mix = 1`, `AnchorMode::Free`.
  Thresholds: (a) **equal-power constancy.** Sweep the isolated peak's `setPeakPan` across
  `[-1, +1]` in 0.1 steps (wander disabled so the position is exactly the swept value), and at each step
  measure the combined power `outL_rms² + outR_rms²` over a settled window; the total stays within
  **0.5 dB** of its value at `pan = 0` across the entire sweep — the sin/cos law's constant-power
  property, asserted directly rather than assumed; (b) **determinism under seed.** Two instances built
  with the same seed report identical `getPeakPan(i)` default positions for all twelve peaks
  (Clarifications Q4's seeded one-shot draw, FR-005/FR-038) and, with `panWander > 0`, identical
  `getPeakCurrentPan(i)` trajectories over a render; (c) **non-vacuity — wander actually moves the
  field, threshold measured not guessed.** With `panWander` at its FR-016 default and `wanderRate` at
  its default, render ≥ 60 s of the isolated-peak configuration and measure the inter-channel level
  difference `20·log10(outL_rms / outR_rms)` over consecutive 1 s windows; the difference's range across
  the render exceeds `kPanNonVacuityDb`, a bound **set during the build from a measurement across ≥ 8
  seeds** (below the observed minimum range, with the distribution recorded in `compliance.md`,
  following this spec's own Bound provenance discipline) — proof the lane is live, not a static split
  that would pass (a) and (b) vacuously. The provisional figure carried into the build is **1 dB**;
  (d) **mix = 0 passthrough holds regardless of pan** — folded into SC-015 (c), not restated here, since
  a dormant or `mix = 0` render never reaches the pan step (FR-044).
  Measured by: `ResonanceDriftNetwork_PeakPan`.
- **SC-022 — `clearAudioState()` clears audio, not modulation (FR-046, Clarifications Q5).**
  Thresholds: (a) **audio state is cleared**: drive one peak to steady state at `peakQ = 100`
  (`RT60 ≈ 5.5 s` at a 40 Hz anchor per SC-015 (f)'s figure), call `clearAudioState()`, and require the
  peak's contribution to be **silent** (below `kSampleTolerance`) on the very next sample — the same
  immediacy SC-015 (f) requires of a sleep edge, but reached through the new method instead of
  dormancy; (b) **modulation is not cleared — lane trajectories continue.** Run two identically-seeded
  instances forward together; call `clearAudioState()` on one and nothing on the other at the same
  sample; every peak's `getPeakCurrentFrequency`/`getPeakCurrentQ`/`getPeakCurrentGainDb`/
  `getPeakCurrentPan` on the cleared instance agrees with its never-cleared twin within
  `kMetricTolerance` for at least 60 s afterward — the lanes never rewound, unlike `reset()`
  (SC-006 (d), SC-007); (c) **distinct from `reset()`**: the same before/after comparison using `reset()`
  instead of `clearAudioState()` shows the reset instance's lane trajectories **diverge** from the
  untouched twin (rewound to the mean, SC-006 (d)'s post-`prepare` stream) while the
  `clearAudioState()`-cleared instance's trajectories from (b) do not — proving the two methods are
  behaviourally distinct, not two names for the same effect.
  Measured by: `ResonanceDriftNetwork_ClearAudioState`.

**Bound provenance note.** SC-003 (a)/(b) and SC-016 (a) reuse thresholds that Vorago Phase 2
*measured* rather than assumed, and the Phase 2 compliance pass had to rewrite five criteria after
measurement showed the originals could not discriminate
(`specs/vorago-phase2-noise-organism/compliance.md:71-86`). The same discipline binds here: any
threshold in this section that proves unable to separate a correct implementation from an injected
defect must be **re-derived from a measured distribution across seeds** and the change recorded, never
merely widened.

**Thresholds this spec ships as explicitly to-be-measured**, each with the number carried into the
build marked provisional and the measurement mandatory before it becomes a compliance row:
`SC-001 (c)` (4-vs-12-peak level agreement), `SC-002 (a)` (`kBoundaryRatio`, provisional 1.5),
`SC-003 (e)` (`kLaneIndependenceR`), `SC-003 (f)` (`kRateSeparation`), `SC-006 (b)`
(`kSeedDecorrelationR`), `SC-008 (c)` (1/e-lag agreement, provisional ±15 %), `SC-015 (g)` (wake/sleep
RMS bound), `SC-016 (a)` (`kSoakWindowDb`, provisional ±6 dB), `SC-016 (c)` (`kSoakFloorDbfs`,
provisional −60 dBFS), `SC-020 (a)` (wet-vs-drive RMS window, tied to FR-045's `kDefaultWetGainDb`
measurement) and `SC-021 (c)` (`kPanNonVacuityDb`, provisional 1 dB). Each is set from a distribution
across **≥ 8 seeds** (≥ 12 seed pairs for SC-006 (b) and ≥ 12 seeds for SC-003 (e)) and recorded with
its spread. Three criteria additionally
carry a **mandatory injection arm** that must go red on the defect they guard: SC-002 (c),
SC-003 (e)'s same-seed control, and SC-017's write-order swap.

## Edge Cases

**Render-path boundaries (RT safety).** `processBlock` is stereo, `(inL, inR, outL, outR, n)`
(Clarifications Q4, FR-003) — an earlier draft of this spec carried a mono two-pointer signature.

- Any of `inL`/`inR`/`outL`/`outR` null (each case tried independently) — nothing written on either
  channel, no state advanced, no control step consumed (FR-003). Asserted by rendering a reference
  block, calling with one pointer null, then rendering again and requiring the second render to equal
  the first on both channels.
- `numSamples == 0` — no-op; the control phase does not advance, so a caller that issues zero-length
  blocks cannot drift the control grid (FR-003, FR-007).
- `numSamples` far above `PrepareConfig::maxBlockSamples` (e.g. 65 536 with `maxBlockSamples = 64`) —
  legal and correct, and not a special case at all: `maxBlockSamples` sizes nothing in this component
  (FR-062), there is no scratch buffer to slice, and the render path is driven entirely by the
  absolute control grid, so any `numSamples` renders directly with no reallocation (FR-061).
- `processBlock` before `prepare` — writes `numSamples` zeros to both `outL` and `outR` and advances
  nothing (FR-003).
- In-place call (`inL == outL`, `inR == outR`) — supported and equal to the out-of-place result
  (FR-003). Cross-aliased (`inL == outR`, `inR == outL`) gives the same guarantee. The implementation
  must therefore capture both dry samples *before* the bank writes.

**Parameter extremes.**

- `setNumPeaks(0)` clamps to 1; `setNumPeaks(99)` clamps to 12 (FR-010). Peaks dropped by a reduction
  are gated down, not cut (FR-041).
- All twelve peaks at `peakQ = kMaxResonatorQ = 100` with `qWander = 2` — the Q map is clamped to the
  bank's `[0.1, 100]` before the `setQ` call, so the requested `100 · 2^2 = 400` becomes 100 and the
  wander becomes one-sided. This is expected and reported by `getPeakCurrentQ`, not an error;
  SC-003 (c) asserts the clamp holds rather than asserting symmetry.
- All twelve peaks at `peakQ = kMinResonatorQ = 0.1` — the bank's bandpass is nearly all-pass in shape;
  the network must remain bounded and the ring-out bound of SC-001 (d) is trivially met.
- Anchor at 20 Hz with `freqWander = 24` semitones — the lower excursion clamps at
  `kMinResonatorFrequency`; the upper does not. Asymmetric, expected, reported.
- Anchor above `0.45 · fs` (e.g. 20 kHz at 44.1 kHz) — clamped silently at both anchor resolution and
  post-drift (FR-025, FR-030). No clamp-counter engagement: this is a configuration outcome, not a
  signal excursion.
- `setNoteFrequency` at its `[8, 0.45·fs]` extremes with `ratio = 64` — the keyed anchor is clamped by
  the same law; the peak simply parks at the ceiling.
- `gravity` exactly at ±1 and exactly 0 — the three closed forms of FR-023, each asserted separately in
  SC-014 (c)/(d)/(e). `g = 0` must be **bit-equal** to Free mode, which forbids an implementation that
  always runs the log/exp round trip.
- **Every `peakRatio` set to the same value in `Hybrid` mode (Clarifications Q1 obsoletes the earlier
  "degenerate grid" case).** Under the index-paired law (FR-023) there is no shared grid to degenerate:
  each peak still crossfades independently between its own `f_free[i]` and its own
  `f_keyed[i] = noteHz × ratio[i]`, so identical ratios across peaks simply mean their `f_keyed[i]`
  values coincide — the twelve `f_free[i]` anchors still differ (FR-016's defaults), so the twelve
  Hybrid results at any `g < 1` still differ too. No fallback, no special case and no no-op: the closed
  form is defined identically for every configuration.
- `mix = 0` with every peak awake — the bank still runs (its cost is unchanged) and the output is the
  dry input on both channels; `mix = 1` with every peak dormant — digital silence on both channels
  (SC-015 (c)).
- `setWanderRate` at both clamps: 0.002 Hz means `T = 500 s`, far above the `kTauMax = 30 s` a single
  `BrownianDrift` lane can express (`brownian_drift.h:99`). It is **not** allowed to saturate there:
  FR-037's decimation reaches it as `decimation = ceil(500/30) = 17` with `tau = 500/17 = 29.4 s`.
  Without decimation the whole sub-range `[0.002, 0.0333]` Hz — including FR-016's own 0.03 Hz default
  — would be a dead zone in which `setWanderRate` is a no-op while `getWanderRate` still reported the
  requested number, and no criterion in the earlier draft of this spec could have detected it. SC-003
  (f) is what detects it now. At the other clamp, 1.0 Hz gives `T = 1 s`, `decimation = 1`,
  `tau = 1 s` — inside the lane's native range with no decimation.
- `setSlewCeilings` at its clamps: 24 octaves/step lets a full-scale anchor jump land in one control
  step (the SC-002 (c) injection configuration, an in-spec setting rather than a test backdoor);
  0.001 octaves/step makes a one-octave note change take 1000 control steps (≈ 1.33 s at 48 kHz) —
  slow, legal, and audibly a portamento rather than a defect.

**Sample-rate changes.**

- `prepare(44100, cfg)` → `prepare(96000, cfg)` on a live object without `reset()` — legal;
  `prepare` fully re-initialises (FR-002). All time constants re-derive; all anchors re-clamp against
  the new Nyquist limit (FR-063). Lane seeds are unchanged, so the same seed still reproduces the same
  *walk*, though not the same audio (different rate ⇒ different control-step count per second).
- A sample rate below `kMinUsableSampleRate = 8000.0` Hz or non-finite — floored at
  `kMinUsableSampleRate`, **not at 1 Hz**. A 1 Hz floor is not merely useless, it is undefined
  behaviour: at 1 Hz the derived frequency-clamp pair `[kMinResonatorFrequency = 20, 0.45·fs = 0.45]`
  is **inverted**, and `std::clamp` with `hi < lo` is UB — MSVC fires
  `_STL_VERIFY("invalid bounds argument passed to std::clamp")` — including inside the shipped
  `ResonatorBank::clampFrequency` (`resonator_bank.h:542-545`), which SC-013 forbids amending. At the
  8 kHz floor the pair is `[20, 3600]` and correctly ordered, every derived coefficient is finite
  rather than a NaN that would poison the bank, and no clamp in this component or its dependencies
  can be called with inverted bounds. The ordering is carried as a `static_assert` on the constants
  and asserted at runtime by `ResonanceDriftNetwork_ControlSurfaceClamps`.

**Seed determinism.**

- `setSeed(0)` — legal. `deriveStreamSeed` guarantees a non-zero per-lane stream even from a zero base
  (`core/random.h:102-110`), and `Xorshift32::seed()` substitutes its own default for a zero
  (`:73-74`); without the derivation two lanes hashing to zero would collapse onto one stream.
- `setSeed` called **after** `prepare` and after audio has been rendered — re-seeds every lane and
  rewinds their streams; the network does not silently ignore a late seed. This is the only way a
  Phase 10 voice-steal can give a recycled voice a fresh trajectory without a full `prepare`.
- Two networks constructed identically but never seeded — identical output. That is intended
  (`BrownianDrift::kDefaultDriftSeed = 0xB17E`, `brownian_drift.h:109`); Phase 10 assigns per-voice
  seeds, and SC-006 (b) is what proves distinct seeds actually decorrelate.

**Life-cycle boundaries.**

- Wake toggled faster than the 50 ms ramp (e.g. every 10 ms) — the ramp target tracks and the output
  is continuous; no re-trigger, no restart, no click (FR-041).
- A peak made dormant **while its gate is mid-ramp** — the target becomes 0 and the ramp continues from
  where it is; the resonator is disabled only when the ramp reaches exactly 0 (FR-042).
- Every peak dormant for hours — the network stops calling all twelve banks' `process` **and** stops
  writing their `setFrequency`/`setQ`/`setGain` control-rate values (FR-042, Clarifications Q6; the
  banks' own per-sample skip, `resonator_bank.h:487-488`, would still cost their global smoothers if
  `process` were still called), each peak's filter state was cleared on its sleep edge, the lanes still
  advance (FR-036) and the network keeps computing (but not writing) their applied values, and CPU falls
  to the SC-004 (c) figure. The network must not
  "optimise" by pausing lanes: that would make wake-up positions deterministic in wall-clock terms and
  contradict roadmap lines 490–491. On eventual wake the accumulated drift is snapped to the bank in one
  exempt write (FR-035, FR-042).

## Decisions taken where the roadmap is silent

The roadmap defers **no** decision to this spec (its Open Questions list, lines 502–515, names phases
6, 8, 9, 10, 11 and 12 — not Phase 3). The decisions below fill genuine silences; each is recorded with
its evidence so the clarification pass can overturn it with knowledge rather than guesswork.

- **D-1 — Scalar `ResonatorBank`, not a SIMD bank (FR-012). CONFIRMED, now subordinate to
  Clarifications Q3's probe-gated rule.** The roadmap prefers SIMD (line 219), but
  the only shipped SIMD bank cannot express per-peak Q (`modal_resonator_bank.h:356-372`), which
  line 220 requires. Chosen: scalar, with a **two-tier** measured fallback (FR-013, Q3) — a purely
  additive `ResonatorBank` method first, `processSympatheticBankSIMD` only as the last resort. The
  budget is the arbiter, not preference.
- **D-2 — Twelve peaks, fixed cap (FR-010).** The roadmap says twelve (line 219) and
  `ResonatorBank::kMaxResonators = 16` accommodates it with slack. A runtime count in `[1, 12]` is
  exposed because Phase 10 will want cheaper voices; the cap is not configurable at compile time.
- **D-3 — SUPERSEDED by Clarifications Q4: stereo I/O with a mono engine and per-peak pan, not mono in
  / mono out.** This decision's original argument — "every element of the prescribed chain is mono,
  citing `NoiseOrganism`" — does not survive at this position in the roadmap's chain: the roadmap's
  Phase 10 signal path is cloud, then noise organism, then *this* network, then the rest, then
  `ContinuousBody`, and **both** `HarmonicCloud::processStereoBlock` (`harmonic_cloud.h:878`) and
  `ContinuousBody::processStereoBlock` (`continuous_body.h:1369`) are stereo. A mono network would force
  an undocumented mono-sum onto Phase 10 immediately upstream or downstream of two stereo neighbours.
  Chosen instead (FR-003, FR-038, FR-044): the engine renders mono (one bank per peak, on the mono sum
  of the two input channels — one engine's cost, so FR-060's budget defence is unaffected), each peak's
  gated mono output is placed by its own wandering equal-power pan position, and the dry path is
  carried stereo unchanged through the crossfade. Broader stereo processing (correlated reverberant
  width, mid-side, cross-channel effects) is still Phase 9's or a Phase 10 amendment's, per the
  Non-Goals section.
- **D-4 — Insert with a dry/wet mix, not a parallel send (FR-043).** The architecture diagram
  (roadmap lines 48–58) shows cloud and noise flowing *through* the network into the feedback ecology,
  so an insert is the shape. A fully-wet insert would delete all non-resonant content, so the network
  owns a linear crossfade rather than inheriting `ResonatorBank`'s asymmetric `exciterMix` law
  (`resonator_bank.h:514`).
- **D-5 — Q is set directly, never as a decay time (FR-032).** `rt60ToQ` saturates at Q = 100 for
  `f · RT60 > 219.87` (`resonator_bank.h:92-99`), which at Vorago's low anchors is a musically ordinary
  three-second decay — a decay-time surface would silently freeze Q wander exactly where the instrument
  lives. RT60 is reported, not written.
- **D-6 — This component does not replace `NoiseOrganism`'s per-slot resonators.** `NoiseOrganism`
  already wanders 2–4 resonators *per noise slot* (`noise_organism.h:686`, `:703`); those colour one
  noise source. This network's twelve peaks sit on the summed voice bus and colour everything,
  including the harmonic cloud. Both are in the roadmap's architecture diagram (lines 48–56) as
  separate boxes. Recorded because "we already have wandering resonators" is the obvious — and
  wrong — objection.
- **D-7 — The wander-rate default is shared with `NoiseOrganism` (FR-034, 0.03 Hz,
  `noise_organism.h:163`).** Two components drifting on unrelated default clocks would make a Phase 10
  patch sound arbitrary; a caller can still separate them with one setter call.
- **D-8 — No `SlowEventScheduler` ownership, no `ModulationSource` implementation.** Both follow
  established Vorago precedent (Phase 1's routing finding and Phase 2's `noise_organism.h:841-843`
  comment) and are restated in Non-Goals.
- **D-9 — Frequency-then-Q-then-gain write order is normative (FR-014), and the Q write is exempt from
  change detection (FR-015).** It is an implementation detail with an FR because getting it wrong is
  silent: the audio still sounds plausible, Q is simply never what was asked for.
  `resonator_bank.h:333` is the reason, and `decays_[i]` standing permanently at
  `kDefaultDecayTime = 1.0 s` (`:60`, `:201`, `:227`) is what makes the wrong value
  `rt60ToQ(f, 1.0) = 0.4548·f` rather than something obviously broken. Because it is silent by nature,
  it needs an *audio-domain* criterion, not a read of the network's own applied value: **SC-017**, with
  a mandatory injection arm.
- **D-10 — Twelve single-resonator banks, not one twelve-resonator bank (FR-011).** Forced, not
  preferred: `ResonatorBank::process(float)` returns only the summed wet output and exposes no
  per-resonator accessor (`resonator_bank.h:470-516`), so the finest per-peak gate a shared bank admits
  is one 64-sample control chunk — ≈ 36× coarser than the per-sample ramp the roadmap's Dormancy rule
  requires (lines 490–491) and than the Phase 2 code states in as many words
  (`noise_organism.h:1819-1821`). The alternatives were: amend `ResonatorBank` outright (SC-013
  forbids it in the default case regardless of blast radius — and Clarifications Q3 later corrected
  this spec's blast-radius premise to false besides), re-implement the
  bandpass on a bare `Biquad` (impossible at the required Q: `BiquadCoefficients::calculate` clamps Q
  to `kMaxQ = 30`, `biquad.h:53`, `:170`, which is why `ResonatorBank` carries its own coefficient
  routine at all), or weaken the ramp to the control grid (weakening a roadmap constraint to fit an
  implementation — rejected). The cost is measured by FR-060 (a), not assumed, and FR-013's two-tier
  fallback (Clarifications Q3: a purely additive `ResonatorBank` method, then the SIMD kernel) — both of
  which preserve per-resonator outputs and so make the same gate expressible — is the pre-approved
  response if it breaks the budget.
- **D-11 — FR-019's normalisation count is `numPeaks`, not the awake-peak count.** The alternative
  reading makes every scheduled wake event rescale the whole bed (0.19 dB for one peak of twelve,
  5.4 dB from one to twelve) in the one component whose premise is that nothing steps, and produces
  `1/sqrt(0) = +inf` in the all-dormant state that SC-015 (c) and SC-004 (c) both require to be exactly
  silent. `numPeaks ≥ 1` always, so the normalisation is always finite. The consequence that a
  `setNumPeaks` change *does* move the level of the survivors is stated in FR-019 and asserted with its
  derived figure in SC-015 (g) rather than left to be discovered.
- **D-12 — Lane decimation rather than narrowing the rate range (FR-037).** The other way to close
  FR-034's dead zone was to shrink `wanderRate`'s range to `[1/kTauMax, 1.0] = [0.034, 1.0]` Hz and
  drop the "minutes" language. Rejected: roadmap line 31 makes minute-scale time the instrument's
  identity ("Time scale is minutes, not milliseconds"), so narrowing the range would be resolving a
  review finding by deleting the requirement. Decimation reaches 500 s correlation times through the
  shipped lane with no amendment to it (SC-013) and costs one integer counter.
- **D-13 — FR-035's slew ceilings are settable, not constants.** They were pinned in the first draft,
  which left SC-002 (c)'s injection arm — the spec's own anti-vacuity proof for the no-zipper
  criterion — with no mechanism, reachable only by an undeclared test hook or by editing the header
  under test. Making them a real, clamped, documented control surface removes the backdoor, gives
  Phase 10 something it will want anyway (tight for slow patches, loose for note tracking), and turns
  the injection into an ordinary in-spec configuration. SC-018 (d) asserts the setter is not
  cosmetic.
- **D-14 — Hybrid mode is index-paired, not nearest-neighbour (FR-023, Clarifications Q1).** The
  nearest-neighbour law this spec shipped for review collapsed distinct peaks onto shared grid points at
  `g = 1` for the FR-016 defaults (peaks 0/1 both to 55 Hz, peaks 9/10/11 all to 660 Hz) and needed a
  separate repeated-extension rule to define `f_anti` for free anchors above the grid. Index-pairing
  peak *i* to its own `f_free[i]`/`f_keyed[i]` pair removes both problems at once: bijective by
  construction, and both `g > 0` and `g < 0` become the same closed form with no grid, no search and no
  extension.
- **D-15 — `reset()` keeps `NoiseOrganism`'s meaning; `clearAudioState()` is a separate, lighter method
  (FR-004, FR-046, Clarifications Q5).** Splitting the *meaning* of `reset()` between the two Vorago
  siblings would make Phase 10 unable to call `reset()` on both at the same moment and expect the same
  kind of thing to happen. Adding a second method is one extra name, not a compatibility hazard, and it
  gives Phase 10 the freewheeling-across-notes behaviour the roadmap's "nothing repeats exactly"
  philosophy (line 30) asks for without changing what `reset()` itself means.
- **D-16 — A dormant peak skips its control-rate bank writes; the wake edge snaps, exempt from FR-035
  (FR-035, FR-042, Clarifications Q6).** Keeping the bank always current while dormant is the correctness-
  first reading, but a dormant peak's bank is disabled and inaudible, so the write buys nothing; the
  cheaper reading (skip, then snap once on wake) is inaudible by construction because the gate is
  provably `0.0f` at the snap instant. This is the one designed CPU difference between the two Vorago
  siblings — `NoiseOrganism` keeps writing while dormant, this network does not — recorded so a future
  reader does not "fix" the asymmetry.
- **D-17 — One network-wide Hz wander rate, per-peak depths (FR-034, Clarifications Q7).** A per-peak
  rate in seconds would match `NoiseOrganism`'s *secondary* per-slot path but would make FR-037's
  decimation counter per-peak state (lane phase could then drift apart, and the counter can no longer
  ride the FR-007 absolute grid for free). A network-wide Hz rate matches `NoiseOrganism`'s *primary*
  `setWanderRate(hz)` control instead, which is the correct comparison — the two components share one
  vocabulary, not two. Per-lane-kind multipliers (Q7's option (c)) are deferred to Phase 10, when there
  is a patch to tune them against.
- **D-18 — Normalise, then trim, then pan, then crossfade, then clamp — one canonical ordering, stated
  once (FR-044, Clarifications Q8).** An earlier draft of FR-044 stated an ordering sentence
  ("mix, normalisation and clamp... in that order, after this multiply") that contradicted FR-019 and
  FR-043 and would have scaled the dry path by `1/sqrt(numPeaks)`, breaking SC-015 (c) by 10.8 dB at the
  FR-016 defaults. Three of the four original statements already agreed on wet-only normalisation; the
  outlier sentence is corrected rather than honoured, and Q4's per-peak pan step is folded into the same
  single canonical statement so no later reader has to reconcile pieces stated in different FRs again.

## Open Questions

**None deferred by the roadmap, and the clarification pass named in the paragraph below has now run
(see Clarifications).** The roadmap's Open Questions (lines 502–515) assign no decision to
Phase 3, and no roadmap statement about this phase is ambiguous enough to require a user ruling before
planning. The roadmap-silent decisions D-1, D-3, D-4 and D-6 were the ones flagged as most worth a
challenge; D-3 was superseded (Clarifications Q4), D-1 and D-10 were made subordinate to a probe-gated
rule (Q3), and D-4/D-6 were confirmed as written (Q8's ordering correction attaches to D-4; D-6 stands
unchanged) — see the `DECISIONS-CONFIRMED` line in Clarifications for the complete disposition. SC-004's
ceiling question remains genuinely open until FR-060's probe is run: if the reference configuration
straddles 80 000 ns, the response is FR-013's two-tier fallback, then FR-038's static-pan fallback, then
a user decision — never a quiet relaxation.

## Traceability

| Roadmap statement (line) | Requirements | Criteria |
|---|---|---|
| "New component (L3, `systems/resonance_drift_network.h`)" (217) | FR-001, FR-002 | SC-012 |
| "12 resonant peaks (compose `ResonatorBank`/`ModalResonatorBankSimd` — SIMD path preferred…)" (219) | FR-010, FR-011, FR-012, FR-013, FR-044 | SC-004, SC-013 |
| "per-peak `BrownianDrift` on frequency (bounded around anchor ratios), Q, and gain" (220) | FR-030, FR-031, FR-032, FR-033, FR-034, FR-035, FR-036, FR-037 (pan, FR-038/FR-039, is a Phase 3 clarification addition beyond this roadmap line — see Clarifications Q4) | SC-003, SC-006, SC-017, SC-021 |
| "Anchor modes: free … keyed … hybrid (gravity-style pull…)" (221–222) | FR-020, FR-021, FR-022, FR-023, FR-024, FR-025 | SC-014 |
| "Peak life cycle: peaks can sleep/wake …, event-hookable" (223–224) | FR-040, FR-041, FR-042, FR-044 | SC-015 |
| "stability at max Q under sustained input (infinite-ring harness pattern)" (227) | FR-018, FR-019, FR-031 | SC-001, SC-016, SC-017 |
| "no zipper under drift (per-block coefficient smoothing)" (227) — **read as "coefficients recomputed on a per-block control grid, not per sample"; interpolation deliberately not implemented, evidence and enforcing measurement recorded in FR-007** | FR-007 (interpretation), FR-014, FR-015, FR-035, FR-041, FR-044 | SC-002, SC-018 |
| "wander-rate spectral tests" (227) | FR-034, FR-036, FR-037 | SC-003 (incl. (f), the rate-is-not-inert arm) |
| "CPU ≤ 0.75% per voice" (228) | FR-011, FR-013, FR-015, FR-038, FR-042, FR-060 | SC-004 |
| RT safety, pools sized at prepare (481) | FR-002, FR-003, FR-006, FR-061, FR-062 | SC-005, SC-011, SC-019 |
| Boundedness soak (482–483) | FR-018, FR-019 | SC-001 (e), SC-016 |
| Layer discipline + ODR sweep (486) | FR-001, New-components table | SC-012 |
| CPU budgets are FRs (487) | FR-060 | SC-004 |
| Dormancy rule (489–493) | FR-036, FR-041, FR-042, FR-044 | SC-015 (incl. (f), wake after long dormancy) |
| No bit-exact float goldens (494) | — | SC-010 (`render_fingerprint.h` tolerances). SC-013 is a suite-green + `git diff --stat` check and uses no fingerprint; it is **not** cited here |
| Portability, non-finite handling (495–496) | FR-008, FR-009, FR-064 | SC-009, SC-012 |
| Shared-component changes keep Seraphis green (498–500) | FR-012, FR-013 (no amendment planned in the default case; Clarifications Q3's probe-gated additive method/SIMD fallback is the pre-approved contingency) | SC-013 |

**Edge Cases are compliance-tracked, not prose.** Every item in the Edge Cases section maps to a named
test case and gets its own compliance row: the render-path boundaries and guard ladder to **SC-019**
(`ResonanceDriftNetwork_RenderPathBoundaries`); the parameter extremes to `ResonanceDriftNetwork_AnchorModes`
(anchor clamps, degenerate grid) and `ResonanceDriftNetwork_WanderRateSpectral` (Q clamp asymmetry,
SC-003 (c)); the sample-rate-change items to `ResonanceDriftNetwork_SampleRateIndependence`; the
seed-determinism items to `ResonanceDriftNetwork_SeedDeterminism`; the life-cycle boundaries to
`ResonanceDriftNetwork_PeakLifeCycle`; and the two `setWanderRate` / `setSlewCeilings` clamp items to
SC-003 (f) and SC-018 (d) respectively.

**Clarification-session additions with no roadmap line of their own.** FR-038/FR-039 (per-peak pan),
FR-045 (wet-gain trim) and FR-046 (`clearAudioState()`), with their enforcing criteria SC-020, SC-021
and SC-022, arose from the 2026-09-10 clarification session (Q4, Q2 and Q5 respectively) rather than
from a roadmap statement, so they are not rows in the table above; see Clarifications for the decision
that minted each one.

## Assumptions

1. **CORRECTED (Clarifications Q3), not an assumption any more: `ResonatorBank` has real consumers, and
   the earlier claim that it did not was false.** An earlier draft of this spec (and, before it, the
   Vorago Phase 2 spec) claimed a word-bounded sweep for `ResonatorBank` across `dsp/` and `plugins/`
   found no consumer outside `resonator_bank_test.cpp` and the compile-only `dsp/lint_all_headers.cpp`.
   That claim is **false**. `ResonatorBank` is consumed by every Membrum body
   (`plugins/membrum/src/dsp/bodies/*.h`, `body_bank.h`, `drum_voice.h`, `tone_shaper.h`,
   `voice_pool.cpp`), by Innexus (`plugins/innexus/src/dsp/physical_model_mixer.h`,
   `processor/innexus_voice.h`), and by `ContinuousBody`, `NoiseOrganism`, `iresonator.h` and
   `modal_resonator_bank_simd.h`. This phase still plans **no** amendment to it in the default case
   (SC-013), but FR-013's probe-gated fallback no longer leans on a small-blast-radius argument — it
   leans instead on running the corrected consumer-suite list (`membrum_tests`, `innexus_tests`,
   `dsp_processors_tests`, `dsp_systems_tests`, `seraphis_tests`) before and after any change is taken.
2. **The network's input is the summed cloud + noise-organism signal at roughly −12 dBFS, on each
   channel.** The
   architecture diagram (roadmap lines 48–58) fixes the position; the level is an assumption used only
   to choose test drive levels, and every criterion is stated relative to the drive rather than in
   absolute dBFS where that is possible.
3. **Phase 10, not this phase, decides how many peaks a voice runs and how they are seeded.** The
   defaults in FR-016 are a musically plausible starting point for tests, not a tuned patch.

## Review notes

This section records how the review pass of 2026-09-09 was resolved. Every issue was applied; **none
was rejected**, and no threshold was relaxed to make one go away. Two entries are recorded here because
their resolution changed the shape of the component rather than only the wording of a criterion, and a
later reader is entitled to know that the change was forced by a verified fact and not by preference.

- **The engine changed shape (FR-011).** The spec previously composed one twelve-resonator
  `ResonatorBank`. That is incompatible with the roadmap's Dormancy rule, because `process(float)`
  returns only the summed output and the class has no per-resonator accessor
  (`resonator_bank.h:470-516`) — verified this session by reading the render loop and sweeping every
  write to `filterOutput` (`:498-510`). The available resolutions were: weaken FR-041's per-sample
  ramp to the 64-sample control grid (rejected — it would resolve a review finding by deleting a
  roadmap constraint, roadmap lines 490–493), amend `ResonatorBank` (rejected — SC-013), or own twelve
  single-resonator banks (taken, D-10). The cost is now an FR-060 measurement rather than an
  assumption, and FR-013's fallback covers the case where the measurement is bad.
- **The wander-rate range was kept, and the lane clock was decimated instead (FR-037).** The review
  correctly showed that `[0.002, 0.0333]` Hz was a dead zone, and offered narrowing the range as one
  option. Narrowing was rejected: roadmap line 31 makes minute-scale motion the instrument's identity,
  so removing it would be resolving the finding by deleting the requirement. D-12 records the choice.

Two review suggestions were adopted in a **stronger** form than proposed, recorded so the difference is
not read as a partial application:

- The review offered SC-002 (c)'s injection either as a compile-time-guarded hook, as a recorded
  mutation experiment, or through the public surface. The spec takes the third and makes FR-035's
  ceilings a real, clamped, documented control surface (D-13), so the arm needs no backdoor at all —
  and adds SC-018 (d) to prove the setter is not cosmetic.
- The review suggested defining `activePeakCount`. The spec goes further and **removes the name**:
  FR-019's count is `numPeaks` by definition and the getter is `getNormalisationPeakCount()`
  (FR-052), because the ambiguity lived in the word "active" (D-11).

## Clarifications

### Session 2026-09-10

Answers below were given in an interview against the clarification scan of 2026-09-09 (the questions
that scan raised are preserved only as history in this log; the spec body above already reflects every
decision — see the bracketed FR/SC ids for the enforcing text). No later stage may treat an item here as
open; all eight are decided.

- **Q1 (Hybrid mode's anchor-selection law):** Index-paired, not nearest-neighbour — peak *i* in Hybrid
  mode pulls only toward its own keyed anchor `noteHz × ratio[i]`, never toward the nearest grid point;
  `g` is a closed-form log-domain crossfade between peak *i*'s own Free and Keyed anchors, bijective and
  collision-free by construction, and the shared-grid `f_anti`/grid-extension machinery is deleted
  entirely. [FR-023, FR-024, SC-014 (c), SC-014 (d), SC-014 (e)]
- **Q2 (wet-path level and what `mix` is for):** Network-owned static wet-level trim — add
  `setWetGain(float dB)`/`getWetGain()`, applied to the normalised wet sum before the crossfade, with a
  default measured (not guessed) by rendering the FR-016 reference patch on SC-001's broadband drive so
  wet RMS lands within a few dB of drive RMS, and `mix = 0.5` is audibly a blend (wet within roughly
  6 dB of dry) rather than a mute. [FR-045, SC-020]
- **Q3 (is a purely additive `ResonatorBank` amendment allowed):** Probe-gated, one additive method,
  tried only if FR-060's stage probe shows the twelve-bank composition exceeds 48 000 ns/block (60 % of
  the SC-004 ceiling); the SIMD kernel (FR-013) stays as the last-resort fallback behind it. The
  spec's prior claim that `ResonatorBank` has no consumer outside its own test is corrected to false —
  it is consumed by every Membrum body, by Innexus, and by `ContinuousBody`, `NoiseOrganism`,
  `iresonator.h` and `modal_resonator_bank_simd.h` — so the enforcing gate for either fallback tier is
  running `membrum_tests`, `innexus_tests`, `dsp_processors_tests`, `dsp_systems_tests` and
  `seraphis_tests` before and after, never a small-blast-radius argument.
  [FR-013, SC-013, Assumptions item 1]
- **Q4 (mono vs. stereo, given a stereo upstream source):** Stereo I/O with a mono engine and wandering
  per-peak equal-power pan — `processBlock(inL, inR, outL, outR, n)`; the mono sum of the two input
  channels feeds one bank per peak (one engine's cost); each peak's gated output is placed by its own
  equal-power (sin/cos) pan position, a fourth wander lane (`setPeakPan`/`setPeakPanWander`) on the same
  network-wide Hz rate as the other three; the dry path is carried stereo unchanged. The roadmap-silent
  decision D-3 ("mono in, mono out") is superseded: both `HarmonicCloud` and `ContinuousBody` are stereo
  at this position in the Phase 10 chain, so D-3's mono argument does not hold here.
  [FR-003, FR-038, FR-039, FR-044, D-3, SC-015 (c), SC-021]
- **Q5 (what `reset()` does to the wander lanes):** `reset()` keeps exactly `NoiseOrganism::reset()`'s
  meaning — clears audio state, rewinds every lane, mandatorily re-applies configuration — so Phase 10
  can call `reset()` on both siblings at the same moments; a separate, lighter `clearAudioState()` is
  added that clears only audio state and leaves the lanes freewheeling, for Phase 10's future
  across-notes case. [FR-004, FR-046, SC-006 (d), SC-022, D-15]
- **Q6 (does a dormant peak still get its control-rate writes):** No — the network skips
  `setFrequency`/`setQ`/`setGain` entirely while a peak's gate sits at exactly `0.0f`, keeps computing
  and internally tracking the values so the read surface echoes them as it would for an awake peak, and
  snaps the stored value to the bank in one write on the wake edge, exempt from FR-035's slew limiter
  (inaudible by construction because the gate is provably zero at that instant).
  [FR-035, FR-042, FR-052, SC-004 (c), SC-015 (f), D-16]
- **Q7 (scope and units of the wander controls):** Per-peak depths, one network-wide rate in Hz — the
  spec as written is kept, with FR-016's table relabelled "per-peak default" and FR-051's getters given
  their peak index; this is confirmed to be the *same* Vorago vocabulary as `NoiseOrganism`'s primary
  `setWanderRate(hz)` control, not a unit mismatch, since that component's per-slot seconds argument is
  its secondary path. Per-lane-kind rate multipliers are deferred to Phase 10.
  [FR-016, FR-034, FR-051, D-17]
- **Q8 (where the `1/sqrt(N)` normalisation sits relative to `mix`):** Normalise the wet sum only, then
  trim (Q2), then place each peak's pan (Q4), then crossfade against the stereo dry path, then clamp —
  stated once as the single canonical signal path; FR-044's earlier ordering sentence, the outlier
  against FR-019/FR-043/SC-015 (c), is corrected rather than honoured. [FR-044, SC-015 (c), D-18]

**Decisions confirmed unchanged by this session:** D-1 (scalar `ResonatorBank` over the SIMD modal bank)
stands, now subordinate to Q3's probe-gated rule; D-3 is superseded by Q4; D-4 (insert with a
network-owned linear crossfade) stands, with Q8's ordering; D-6 (this network's bus-wide peaks are
distinct from `NoiseOrganism`'s per-slot colouring) stands unchanged. SC-004's ceiling policy is
confirmed: a budget miss is answered by Q3's additive method, then FR-013's SIMD fallback, then (for the
pan lane specifically) FR-038's static-pan fallback, then a user decision surfaced with the per-stage
breakdown — never a quiet threshold relaxation or cap reduction, and never applied unilaterally by an
implementing agent.

### Post-plan rulings, 2026-09-10

Two items plan S17/S14 put to the user after the plan review; both ruled before the build stage.

- **OQ-1 (Tier 1's per-resonator state clear):** Option (i) — Q3's "one additive method" is read as one
  additive *capability*: `processIndividual` plus a five-line `resetResonatorState(std::size_t)` companion
  on `ResonatorBank`, touching no existing behaviour, under the same before/after consumer-suite gate.
  Options (ii) (drop FR-042's clear) and (iii) (keep Tier 0 banks as state containers) rejected.
  [FR-013, FR-042, SC-013, SC-015 (f)]
- **C-1 (`PrepareConfig::maxBlockSamples` after the zero-allocation finding):** Kept, retained-but-inert
  — same `PrepareConfig` shape as `NoiseOrganism` so Phase 10 configures both siblings identically,
  documented as sizing nothing in this component. FR-062/SC-011 stand as re-derived (zero bytes, zero
  allocations). [FR-062, SC-011]

### Build-stage corrections, 2026-09-10 (all forced by measurement, none by preference)

Four corrections raised while making group P's spectral criteria run. Each is recorded at its FR/SC
site with the numbers; this list exists so the compliance pass can find them from one place. Every
figure below is a 60 s render of SC-002's own fixture at 48 kHz, three seeds
(`0x5C02A001` / `0x11111111` / `0xDEADBEEF`).

- **C-14 — SC-002 (b)'s second assertion was unsatisfiable, and is now normalised.**
  `|P_on − P_off| / P_off <= 0.10` measured **210.8** at the provisional +30 dB trim and **14.7** on a
  perfectly linear render, because `P` scales with output level and the two arms cannot carry the same
  level (all four lanes at maximum depth). Replaced by the closed-form identity on the control arm —
  `P_off / A_off` versus `4·sin²(π·f/fs)`, measured **1.0003** — plus the surviving directional clause
  `P_on > P_off`. [SC-002 (b)]
- **C-15 — the network could emit NaN, through the public control surface alone.** FR-018's clamp
  bounds `±inf` but not NaN. Under SC-002 (c)'s own injection the resonators are parametrically pumped
  (750 Hz modulation of peaks at 110–880 Hz): the wet sum went **non-finite 1.83 s into the render and
  stayed non-finite for 97 % of it**, at every wet trim, and the criterion's statistic was being
  computed over NaNs through a non-transitive `std::sort` comparator. FR-018 now tests the wet sum per
  sample and clears the ring of any peak whose engine output is non-finite. [FR-018, FR-044 step 5]
- **C-16 — the component had a real zipper, in gain and in pan.** On a linear render the shipped build
  measured `B/P` = **15.72** with only the gain lane open and **8.79** with only the pan lane open,
  against SC-002 (a)'s bound of 1.5 (frequency 1.00, Q 1.02 — the two the slew limiter already
  guards). Both are per-control-step **multiplies** applied straight to the sample: the bank's
  per-slot gain (`ResonatorBank::setGain` has no smoother) and the equal-power pan pair. Both are now
  interpolated across the control chunk; re-measured, `B/P` = **1.010 / 0.992 / 1.023**. FR-017,
  FR-035, FR-044 and SC-002's preamble each carried a claim this measurement refuted, and each is
  corrected in place. [FR-014, FR-015, FR-017, FR-035, FR-044, SC-002]
- **C-17 — SC-002's fixture and its injection arm.** The fixture ran at FR-045's provisional
  `kDefaultWetGainDb = +30 dB` and clipped **16–21 %** of every render, which pinned `B/P` at 1.06 and
  made the (a) arm unable to fail; it now renders at −12 dB, discards one second of settling, and
  asserts `getClampEngagementCount() == 0` on all three arms. The injection schedule (every chunk,
  alternating) was the parametric pump C-15 found, and its bound of 10 is unreachable by any
  implementation because a retune rings into the interior population at the same 3 : 61 ratio as the
  partition; the schedule is now one coin-flipped jump every sixteenth chunk and the bound is
  **measured at 3.0** against a null of ~1.0. [SC-002 (a), (c)]

**Still owned by T019, unchanged by the above:** `kDefaultWetGainDb` itself. Whatever value it lands
on, SC-002's fixture keeps its own trim — a criterion about a boundary discontinuity cannot be
measured on a render that is clipping.
