# Feature Specification: Vorago Phase 7 — Harmonic Bloom

**Spec slug:** `vorago-phase7-harmonic-bloom`
**Roadmap source:** `specs/Vorago-roadmap.md` → Part A → Phase 7 (lines 335–354); reuse-inventory row
`L7 Harmonic Bloom` (line 115); the ODR note (lines 127–129); the cross-cutting constraints
(lines 518–542), of which the Dormancy rule is lines 527–535 and the shared-component rule is
lines 540–542; the dependency graph (lines 498–511). Roadmap **Open Questions** (lines 544–559)
defer **nothing** to this spec — see *Open Questions* below. Every roadmap line number in this
document was read and re-verified against `specs/Vorago-roadmap.md` this session.
**Layer:** one new Layer 3 component, `dsp/include/krate/dsp/systems/bloom_engine.h`
(roadmap line 340).
**Test target:** `dsp_systems_tests` — an **enumerated, not globbed** source list opening at
`add_executable(dsp_systems_tests` (`dsp/tests/CMakeLists.txt:324`) and closing at `:464`; an
unregistered TU silently drops out of the build and its cases never run (the list says so itself at
`:451-452`). The `-fno-fast-math` block opens at `dsp/tests/CMakeLists.txt:556`
(`if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`), its `set_source_files_properties` at `:557`, its
`PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` at `:900`.
**Depends on:** nothing from Vorago Phases 2–6. The roadmap's dependency graph (lines 498–511) puts
Phase 7 on its own branch off Phase 1, and even that edge is nominal: this component does **not**
own a `SlowEventScheduler` (FR-040, and the house rule at `noise_organism.h:842`), so nothing here
consumes a Phase-1 deliverable at compile time. It consumes the **shipped Seraphis** `HarmonicCloud`
contract only as a *documented array shape*, not as an include.
**Plugin work:** none. The Vorago plugin starts at Phase 11; phases 1–10 are KrateDSP-only.

---

## Overview

Harmonic Bloom is Vorago's **growth** layer: "every few minutes the drone grows new harmonics; later
they die back" (roadmap line 338). It is a Layer 3 system that reads the voice's current partial
spectrum, picks the strongest `K` partials, spawns **child partials** in octave / fifth /
detuned-neighbour relationships to them, and runs each child through a **45 s fade-in →
minutes-scale hold → 3 min fade-out** lifecycle managed by a fixed lifecycle table with no
allocation (roadmap lines 342–348).

The component renders **no audio**. It is an **array-in / array-out control-rate transformer** over
the `(ratios, amplitudes, count)` triple that a Vorago voice hands to
`HarmonicCloud::setSpectralTarget` — exactly the shape `EntropyProcessor::processChunk(float*
ratios, float* amplitudes, std::size_t count, std::size_t numSamples)`
(`processors/entropy_processor.h:269`) already has, and the shape the Seraphis voice's
morph → cloud handoff already produces (`systems/seraphis_voice.h:1053-1054`). This is not a
stylistic choice: **`HarmonicCloud` has no per-partial ratio or amplitude setter**. Its entire
public write surface for partial content is `setSpectralTarget(const float* ratios, const float*
amplitudes, std::size_t count)` (`systems/harmonic_cloud.h:769`) — the only other per-partial
entry points are `setPartialPosition` (`:1069`), `setPartialMask` (`:1084`), `soloPartial`
(`:1092`) and `clearPartialMask` (`:1101`), none of which can introduce a frequency. Writing
children into "reserved cloud partial slots" (roadmap line 346) therefore *means* writing them into
reserved indices of the array that goes to `setSpectralTarget`, and nothing else. The roadmap's
"CPU ≈ free (bookkeeping only — partials render in the existing SIMD bank)" (lines 353–354) is a
direct consequence: the children are rendered by `HarmonicCloud`'s existing SIMD MCF kernel with no
new oscillator anywhere.

Two further verified facts shape the whole design:

1. **A child slot only sounds if its index is below the cloud's active count.**
   `HarmonicCloud::recalculateAmplitudes` zeroes `baseAmplitude_[i]` and `continue`s for every
   `i >= activeCount_` **before** the spectral-target branch is reached (`harmonic_cloud.h:1469-1473`
   vs `:1492-1494`), and `activeCount_ = clamp(round(64^richness), 1, 64)` (`:1462-1463`). A child
   written past the active count is silently inaudible. FR-050 turns this into an explicit capacity
   contract instead of a latent Phase-10 bug.
2. **Children drift, breathe and pan for free.** The cloud's per-partial Brownian detune lane,
   mutation lane, pan scatter, anti-alias fade and per-partial attack/decay offsets are indexed per
   slot across all 64 partials, so a child occupying slot *i* inherits slot *i*'s life without this
   component doing anything. FR-030 therefore owns the **amplitude** lifecycle only and deliberately
   leaves everything else to the cloud.

---

## Scope

In scope:

- One new Layer 3 component, `BloomEngine`, at `dsp/include/krate/dsp/systems/bloom_engine.h`
  (roadmap line 340), header-only, **no audio path**.
- **Parent-spectrum analysis**: pick the strongest `K` partials out of the supplied amplitude array
  (roadmap line 346), evaluated **at spawn time only**.
- **Child spawning** in the three roadmap relationships — octave, fifth, detuned neighbour
  (roadmap line 346) — with a minimum-spacing rule so a child never lands on top of an existing
  partial.
- **Per-child lifecycle**: 45 s fade-in, minutes-scale hold, 3 min fade-out (roadmap line 347),
  C1-continuous in amplitude, driven by a **fixed-capacity lifecycle table, no allocation**
  (roadmap line 347).
- **Reserved-slot accounting** that provably never exceeds cloud capacity (roadmap line 352),
  including the `activeCount_` interaction above.
- **Triggering**: an internal seeded "continuous slow probability" clock **and** an external
  trigger hook, so the Phase-10 `SlowEventScheduler` can drive it (roadmap lines 349–350), plus a
  `depth` scalar that Phase 10's Life/Age macros write.
- Unit tests covering the roadmap's four Phase-7 success criteria (lines 351–354) plus the
  cross-cutting gates (lines 518–542), each as a numbered criterion: seed determinism, block-partition
  invariance, sample-rate change and re-prepare, zero allocation after prepare, layer/ODR lints and
  portability, no bit-exact goldens, and a worst-case boundedness soak.
- A **CPU probe** that prices the component against a concrete ns/block ceiling (FR-070), because
  "≈ free" is not a measurable threshold.

## Non-Goals (owned by later phases, or deliberately excluded)

- **Any FFT or audio-domain analysis.** Roadmap lines 342–344 are explicit: the input is the voice's
  current partial state "direct from `HarmonicCloud` — no FFT analysis needed in-voice; the cloud
  already knows its spectrum", and `HarmonicSnapshot` / `SpectralCoringEstimator` "remain available
  if an audio-domain variant is wanted later". Both were read this session and are **not** consumed
  (see the Existing-components table for the read-and-rejected rows, with reasons).
- **Owning a `SlowEventScheduler`.** House rule, verified: `noise_organism.h:842` states "Vorago
  Phase 10 owns the `SlowEventScheduler`"; `resonance_drift_network.h:993` repeats the arrangement.
  Every Vorago Phase 2–6 component exposes a **plain scalar setter** as its event hook
  (`setSourceWake` at `noise_organism.h:844`, `setPeakWake` at `resonance_drift_network.h:771`).
  FR-040 keeps that shape. `slow_event_scheduler.h` is **not** included.
- **Rendering audio, or being a `ModulationSource`.** No `ModSource` enumerator is added; this is a
  spectrum transformer, not a modulation source. (Following `NoiseOrganism`,
  `ResonanceDriftNetwork`, `FeedbackEcology` and `SubharmonicEngine`, none of which derive from
  `ModulationSource`.)
- **Amending `HarmonicCloud`.** The spectral-target surface already accepts everything the bloom
  needs, *including non-monotone ratios*: the header states that "Non-monotone ratios, ratios outside
  `[SpectralState::kMinStateRatio, kMaxStateRatio]` and amplitudes above 1 are all **ACCEPTED** —
  that is the point of the surface" (`harmonic_cloud.h:765-769` doc block). FR-080 requires the
  header byte-unchanged and Seraphis's suites green (roadmap lines 540–542).
- **Deciding where the bloom sits in the Vorago voice chain**, how many children a voice affords,
  or how Life/Age macros scale `depth`. All Phase 10 (roadmap lines 414–441). This phase ships a
  component correct under any placement that respects FR-050's capacity contract.
- **A second bloom in the reverb.** `AetherReverb` already has a "harmonic bloom" resonator bank
  (`effects/aether_reverb.h:1442-1454`, `kMaxBloomResonators = 32`, `kMaxBloomVoices = 8`,
  `kBloomInjectChannels8`). It is a *reverb* feature and Seraphis's identity; roadmap lines 404–406
  explicitly keep shimmer/bloom-up taps out of Vorago's space engine. Unrelated to this component
  beyond the shared English word — recorded in the ODR table.
- **Per-child drift, pan, mutation or envelope offsets.** Inherited from the cloud slot (Overview
  fact 2). Duplicating them here would double-modulate the same partial.
- **`MultiStageEnvelope` as the child envelope.** Read and rejected: `kMaxStageTimeMs = 10000.0f`
  (`processors/multi_stage_envelope.h:65`) caps a stage at **10 s**, against a 45 s fade-in and a
  180 s fade-out. `GrowthEnvelope` is likewise capped: `kMaxDuration = 60.0f` seconds
  (`processors/growth_envelope.h:98`). Neither reaches the roadmap's time scale — which is precisely
  why roadmap line 348 calls the lifecycle manager the new part. D-3.

---

## Existing components (verified this session)

Every row was opened and read in this session; signatures are quoted from the file.

| Component | Header (verified) | What Phase 7 reuses / relies on |
|---|---|---|
| `HarmonicCloud` (L3) | `systems/harmonic_cloud.h:127` | **The consumer of this component's output** — read, relied upon, **not included and not modified** (FR-080; the engine is decoupled from it by the array contract, D-1). Load-bearing facts, each verified: (a) `static constexpr std::size_t kMaxPartials = 64;` (`:138`) — the hard slot ceiling FR-050 pins to. (b) `static constexpr std::size_t kControlChunkSamples = 64;` (`:144`) — the shared control grid FR-007 adopts. (c) `void setSpectralTarget(const float* ratios, const float* amplitudes, std::size_t count) noexcept` (`:769`) is the **only** write path for partial content; `clearSpectralTarget()` (`:862`), `hasSpectralTarget()` (`:868`). (d) Rejection is **wholesale** — "nothing is written, not even the slots that passed — on a null pointer, `count == 0`, `count > kMaxPartials`, any NaN/Inf, any `ratios[i] <= 0.0f` … or any `amplitudes[i] < 0.0f`" (doc block above `:769`); FR-009's output sanitisation exists so this component can never be the cause of such a rejection. (e) Slots `[count, kMaxPartials)` are padded by the cloud itself as `r = i + 1`, `a = 0` (`:825-826`) — FR-051 reuses exactly that padding for an unoccupied owned slot. (f) **Non-monotone ratios are accepted** (doc block, quoted under Non-Goals) — children may be appended out of order. (g) `activeCount_ = clamp(round(64^richness), 1, 64)` (`:1462-1463`) and every `i >= activeCount_` is zeroed **before** the target branch (`:1469-1473`) — Overview fact 1, FR-050. (h) Whole-array bit-identical skip at the head of `setSpectralTarget` (`:776-786`) — so a bloom that writes nothing costs the cloud nothing. (i) Read surface for tests: `getActivePartialCount()` (`:950`), `getPartialFrequencyHz` (`:955`), `getPartialCurrentAmplitude` (`:959`), `getPartialTargetAmplitude` (`:963`), `getPartialUnmutatedTargetAmplitude` (`:969`), `isQuiescent()` (`:1040`), `stateFinite()` (`:1054`). (j) Change thresholds the cloud applies to a supplied target: `kTargetRatioEpsilonCents = 0.05f` (`:255`), `kTargetRatioRelEpsilon` (`:256-257`, ≈ 2.887e-5), `kTargetAmpEpsilon = 1e-5f` (`:258`). **These thresholds do NOT freeze a slow fade, and this spec previously claimed they did.** Verified: the amplitude mask is `if (!hasTarget_ || std::abs(a - committedAmp_[i]) > kTargetAmpEpsilon)` (`:841`) — the comparand is `committedAmp_[i]`, **not** the previously *supplied* `targetAmp_[i]`, and `committedAmp_[i] = targetAmp_[i]` is assigned **only inside the dirty-gated recompute branch** (`:1481`). The baseline is therefore frozen between recomputes, so sub-epsilon per-chunk motion **accumulates and trips the mask** after enough chunks. The header says so itself, naming the opposite design as the broken one: "COMPARE AGAINST THE COMMITTED VALUE … NEVER against `targetRatio_`/`targetAmp_` … Deviation D14: with the stored target as baseline … sub-epsilon per-chunk motion accumulates FOREVER and never trips the threshold" (`:827-838`). The whole-array bit-identical skip (`:803-810`) cannot freeze it either — it requires `memcmp`-identical arrays, and any non-zero per-chunk increment breaks that. **Net consequence for FR-031:** a slow fade's *target* advances in ~1e-5 quantised steps, which the kernel's own amplitude smoother (`kAmpSmoothTimeSec = 0.002f`, `:165`, `:290`) then smooths — it is **quantised, never stalled**, and −100 dB quantisation of a fade is inaudible. (k) `setFundamentalHz` clamps to `[kMinFundamentalHz = 20, kMaxFundamentalHz = 4000]` (`:184-185`, `:387`) — FR-021's child ratio ceiling is derived against this, not against Nyquist. (l) **Spectral tilt is slot-indexed, not ratio-indexed (Clarification Q1).** `void setSpectralTiltDb(float dbPerOct)` clamps to `[kMinTiltDbPerOct = -12.0f, kMaxTiltDbPerOct = 12.0f]` (`:194-195`, `:439-447`); `tiltGain(std::size_t index)` returns `1.0f` at `tiltDb_ == 0` or `index == 0`, else `std::exp2(tiltDb_ * detail::kHarmonicCloudLog2N[index] * kLog2TenOver20)` with the local `constexpr float kLog2TenOver20 = 3.32192809488736235f / 20.0f;` (`:1429-1435`), and `detail::kHarmonicCloudLog2N[index] = std::log2(static_cast<float>(index + 1))` (`:57-63`) — a function of **slot index**, not of the partial's ratio, so a child written into a high reserved slot is tilted as though it were a high harmonic number regardless of its actual pitch. `baseAmplitude_[i] = targetAmp_[i] * tiltGain(i)` (`:1493`) applies this to every spectral-target amplitude, including a bloom child's. FR-023 restates this law (no `harmonic_cloud.h` include, D-2/D-9 style) to compensate at latch. |
| `EntropyProcessor` (L2) | `processors/entropy_processor.h:57` | **The contract shape this component copies**, read and **not included** (D-1). `void processChunk(float* ratios, float* amplitudes, std::size_t count, std::size_t numSamples) noexcept` (`:269`): in-place perturbation, `nullptr`/`count == 0` is a **no-op with no advance** (`:272-274`), `numSamples > 0` advances internal state, `numSamples == 0` applies without advancing (`:261-263`), and "internal lane state after N advanced samples is a function of N alone, never of how N was partitioned into chunks" (`:257-259`) — the block-partition-invariance wording FR-006 and SC-008 inherit verbatim. `static constexpr std::size_t kPartials = SpectralState::kStatePartials; // 64` (`:62`). `kMinRatioSpacingCents = 24.0f` / `kMinRatioSpacingLog2 = kMinRatioSpacingCents / 1200.0f` (`:80-81`) — the repo's existing "two partials must not collide" figure, which FR-022 adopts rather than inventing a second one. Not modified. |
| `SpectralState` (L2) | `processors/spectral_state.h:44` | Ratio-domain bounds, read for the FR-021 child-ratio clamp. `static constexpr std::size_t kStatePartials = 64; ///< == HarmonicCloud::kMaxPartials` (`:48`), `kMinStateRatio = 0.5f` (`:51`), `kMaxStateRatio = 128.0f` (`:52`). **Not included** — FR-021 restates the bounds as class-scoped constants of its own with a documented cross-check, because including a Seraphis morph header for two floats would drag `SpectralStateId` (`:317`) and the whole authored-state validity model (`:83-94`) into a component that has no states. D-2. |
| `SpectralMorphEngine` (L3) | `systems/spectral_morph_engine.h:90` | **Read, near-name hazard, not consumed.** It already owns a member called *bloom*: `void setBloom(float bloom) noexcept` (`:339`), `getBloom()` (`:447`), `static constexpr float kMaxBloomFraction = 0.6f` (`:100`). That "bloom" is a **per-partial completion stagger** for morph travel ("the PER-PARTIAL completion is staggered by the bloom law, so low partials …", `:18`; the law itself at `:622-636`) — a different concept sharing one English word. All three symbols are **class-scoped**, so there is no ODR collision; recorded so nobody later assumes the two are related. Its output surface `getOutputRatios()` / `getOutputAmplitudes()` / `getOutputCount()` (`:430-432`) is the array-producer precedent FR-005 matches. Not modified. |
| `SeraphisVoice` (L3) | `systems/seraphis_voice.h:1050-1054` | **The wiring precedent**, read only. `morph_.updateChunk(n); cloud_.setSpectralTarget(morph_.getOutputRatios(), morph_.getOutputAmplitudes(), morph_.getOutputCount());` executed once per 64-sample control chunk, "unconditionally every chunk (FR-012)", with the comment that the cloud's whole-array skip makes an unchanged target cheap so "the voice does not duplicate that check". This is the exact insertion point a Vorago voice will hand to `BloomEngine::processChunk` in Phase 10. Not modified. |
| `NoiseOrganism` (L3) | `systems/noise_organism.h:150,178,180,190-195,833,844,880,999` | **Vorago house style, item by item**, read and not included. `kControlChunkSamples` pinned at 64 (`:150`); `static constexpr float kGainRampMs = 50.0f;` (`:178`) — the Dormancy rule's 50 ms re-entry fade (roadmap lines 527–530); `static constexpr float kOutputClamp = 4.0f;` (`:180`); the designated-initialiser-only nested `struct PrepareConfig` (`:190-195`); the event-hook shape `void setSourceDormant(std::size_t slot, bool dormant)` (`:833`) and `void setSourceWake(std::size_t slot, float amount)` (`:844`, "A plain scalar input, not a scheduler reference: Vorago Phase 10 owns the `SlowEventScheduler`", `:841-843`); `getSourceWakeAmount` (`:880`); `[[nodiscard]] std::size_t getAllocatedBytes() const noexcept` (`:999`). Not modified. |
| `ResonanceDriftNetwork` (L3) | `systems/resonance_drift_network.h:135-136,281,297-306,771,782-789,852,904,912` | **Vorago house style + the sleep/wake precedent**, read and not included. `static constexpr std::size_t kControlChunkSamples = 64;` with `static_assert(kControlChunkSamples == 64, "shared 64-sample control grid");` (`:135-136`); `static constexpr double kMinUsableSampleRate = 8000.0;` (`:281`); nested `struct PrepareConfig` with the "callers MUST use designated initialisers … so no narrowing conversion hides in a positional brace init (Clang errors where MSVC does not)" rationale (`:297-306`); `void setPeakWake(std::size_t peak, float amount)` (`:771`) and `void setPeakDormant(std::size_t peak, bool dormant)` (`:784`) with the note that the two are "behaviourally identical" (`:788-790`) — the Dormancy rule made concrete; `isPeakDormant` (`:852`); `getClampEngagementCount()` (`:904`); `getAllocatedBytes()` returning 0 for a component with no heap term, kept "so the Phase-10 host can total its children uniformly" (`:906-912`). Not modified. |
| `SubharmonicEngine` (L3) | `systems/subharmonic_engine.h:155,168-169,175,177,327,380,487,545,554` | The most recent Vorago component; read as the current house template. `static constexpr std::size_t kControlChunkSamples = 64;` + live `static_assert` (`:168-169`); `kMinUsableSampleRate = 8000.0` (`:175`); `kGainRampMs = 50.0f` cross-referenced to `noise_organism.h:178` (`:177`); nested `PrepareConfig` (`:327`); `void prepare(double sampleRate, const PrepareConfig& config) noexcept` (`:380`); `void reset() noexcept` (`:487`); the paired plain/tapped render entry points (`:545`, `:554`). Not modified. |
| `HarmonicSnapshot` / `MemorySlot` (L2) | `processors/harmonic_snapshot.h:30`, `:51` | **Named by reuse row line 115, read this session, and REJECTED.** It is a POD capture struct — `float f0Reference`, `int numPartials`, `std::array<float, kMaxPartials> relativeFreqs/normalizedAmps/phases/inharmonicDeviation`, `std::array<float, kResidualBands> residualBands`, plus `residualEnergy`, `globalAmplitude`, `spectralCentroid`, `brightness` (`:31-44`) — built for the Innexus harmonic-memory feature (`specs/119-harmonic-memory`, banner `:7`) and sized by the **namespace-scope** `kMaxPartials = 96` of `processors/harmonic_types.h:21`, which is *not* `HarmonicCloud::kMaxPartials = 64`. Consuming it would mean carrying a 96-slot residual-bearing capture format, an L2-normalisation step (`:87-102`) and a `HarmonicFrame` reconstruction path (`recallSnapshotToFrame`, `:125`) to hold 64 floats this component already receives as an argument. Roadmap line 344 explicitly makes it optional ("remain available if an audio-domain variant is wanted later"). D-4. |
| `SpectralCoringEstimator` (L2) | `processors/spectral_coring_estimator.h:40` | **Named by reuse row line 115, read, REJECTED.** `void prepare(size_t fftSize, float sampleRate)` (`:54`), `[[nodiscard]] ResidualFrame estimateResidual(const SpectralBuffer& spectrum, const HarmonicFrame& frame) noexcept` (`:75`). It takes an **`SpectralBuffer`**, i.e. it presupposes an STFT this phase does not run, and it estimates a *residual*, not peaks. Roadmap line 342 forecloses the need ("no FFT analysis needed in-voice"). D-4. |
| `FFTAutocorrelation` (L1) | `primitives/fft_autocorrelation.h:40` | **Named by reuse row line 115, read, REJECTED.** `void prepare(std::size_t windowSize) noexcept` (`:94`), `void compute(const float* signal, std::size_t windowSize, …)` (`:153`). A time-domain **pitch/periodicity** tool over an audio signal; the fundamental is a known parameter here. `prepare()` allocates pffft state (`:94`, `destroy()` at `:212`) — an audio-thread non-starter for a component whose whole point is that it is free. D-4. |
| `processSympatheticBankSIMD` (L3 kernel) | `systems/sympathetic_resonance_simd.h:39-50` | **Named by reuse row line 115, read, REJECTED.** `void processSympatheticBankSIMD(float* y1s, float* y2s, const float* coeffs, const float* rSquareds, const float* gains, int count, float scaledInput, float* sums, float releaseCoeff, float* envelopes) noexcept` — a **driven resonator bank** kernel: it needs an audio input sample and produces audio. The bloom's children are *oscillators in the cloud*, not resonators excited by a signal, and roadmap line 354 states they "render in the existing SIMD bank". D-4. |
| `Xorshift32` / `deriveStreamSeed` (L0) | `core/random.h:41` / `:102` | Per-lane seed derivation for FR-070. `[[nodiscard]] constexpr float nextFloat()` (`:59`, bipolar), `nextUnipolar()` (`:67`); `[[nodiscard]] constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept` (`:102`). Load-bearing: `Xorshift32::seed()` substitutes its default for 0, so two lanes hashing to 0 would collapse onto one stream — FR-070's salt table uses `deriveStreamSeed` for exactly that reason (the `harmonic_cloud.h:686-696` rationale). |
| `detail::isNaN` / `isInf` / `flushDenormal` (L0) | `core/db_utils.h:99`, `:260`, `:245` | The `-ffast-math`-proof finiteness tests (bit pattern behind an opaque barrier). FR-008 forbids `std::isnan`/`std::isinf`/`std::isfinite`; `tools/lint-nonfinite-symbols.js` enforces it. |
| `centsToPitchRatioFast` / `semitonesToRatio` (L0) | `core/pitch_utils.h` (via `harmonic_cloud.h:120-122`, `detail::centsToDriftRatio`) | Cents → ratio for FR-021's detuned-neighbour child. **Accuracy caveat, verified:** `centsToPitchRatioFast` is a degree-4 polynomial documented accurate on `|cents| <= 50` (`harmonic_cloud.h:88-116`, worst case 6.15e-08 relative at 29.25 cents), "degrading outside it". FR-021's detune span is bounded to ±50 cents so the fast form is in-domain; anything wider uses `std::exp2`. |
| `LinearRamp` / `OnePoleSmoother` (L1) | `primitives/smoother.h:305` / `:134` | Read for the control-smoothing idiom the house uses (`configure(float rampTimeMs, float sampleRate)` `:329`, `setTarget` `:342`, `process()` `:370`, `snapTo` `:421`). **Not used for the child envelope** — FR-030's 45 s / 180 s segments are per-child piecewise clocks, not a ramp per slot; the ramp idiom is used only for FR-042's `depth` control smoothing. |
| Test helpers | `tests/test_helpers/` | `render_fingerprint.h:58 kSampleTolerance = 5.0e-4f`, `:61 kMetricTolerance = 2.5e-4`, `:63 struct RenderFingerprint`, `:122 compareFingerprints` (SC-008 — **no bit-exact goldens**, roadmap line 536); `allocation_detector.h:48 AllocationDetector`, `:111 AllocationScope` (SC-007); `artifact_detection.h:38 ClickDetectorConfig`, `:72 ClickDetection`, `:130 detect(…)` (SC-001); `audio_features.h:27 centroidHz`, `:88` the centroid computation (SC-004); `statistical_utils.h:41 computeMean`, `:76 computeStdDev`, `:90 computeMedian` (SC-004, SC-005); `spectral_analysis.h` (`:40 frequencyToBin`, `:194 toDb`) and `signal_metrics.h` for the spectral arms. |
| Perf-test idiom | `dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp:55-90` | The measurement basis SC-011 inherits: **nanoseconds per 512-sample block at 48 kHz** ("A percent-of-core figure is not reproducible across dev machines or CI runners", `:67-71`), best-of-25 × 500 blocks after 400 warm-up blocks (`:77-80`), tagged `[.perf]` so the per-push CI filter `~[performance]~[perf]~[benchmark]~[!benchmark]~[long]` excludes it, `static_assert`s on checked-in baselines, and **RUN IT ALONE** (`:83-86`). One 512-sample block period at 48 kHz is **10 666 667 ns**. The **stop-and-surface rule** at `:57-64` — "NO IMPLEMENTING AGENT MAY lower [a capacity], raise [a budget], relax a threshold, or shrink a workload to make a figure fit. Reduce cost, never move the line." — is inherited verbatim by FR-072. |

---

## New components

ODR sweep run **this session**, verbatim, from the repo root:

```
$ grep -rn "class BloomEngine"     dsp/ plugins/ tools/  -> 0 hits
$ grep -rn "struct BloomEngine"    dsp/ plugins/ tools/  -> 0 hits
$ grep -rn "class BloomChild"      dsp/ plugins/ tools/  -> 0 hits
$ grep -rn "struct BloomChild"     dsp/ plugins/ tools/  -> 0 hits
$ grep -rn "class HarmonicBloom"   dsp/ plugins/ tools/  -> 0 hits
$ grep -rn "class BloomLifecycle"  dsp/ plugins/ tools/  -> 0 hits
$ grep -rn "class ChildPartial"    dsp/ plugins/ tools/  -> 0 hits
$ grep -rn "struct ChildPartial"   dsp/ plugins/ tools/  -> 0 hits
$ grep -rn "class BloomEvent"      dsp/ plugins/ tools/  -> 0 hits
$ grep -rn "struct BloomEvent"     dsp/ plugins/ tools/  -> 0 hits
$ grep -rn "Bloom" dsp/ plugins/ --include=*.h --include=*.cpp   (re-run; files, deduped)
   dsp/include/krate/dsp/effects/aether_reverb.h
   dsp/include/krate/dsp/systems/seraphis_engine.h
   dsp/include/krate/dsp/systems/seraphis_macro_matrix.h
   dsp/include/krate/dsp/systems/seraphis_voice.h
   dsp/include/krate/dsp/systems/spectral_morph_engine.h
   dsp/tests/unit/effects/aether_reverb_{test,spectral_test,perf_test,nonfinite_test}.cpp
   dsp/tests/unit/systems/seraphis_{engine,macro,nonfinite,param_broadcast,perf,voice}_test.cpp
   dsp/tests/unit/systems/spectral_morph_{engine_test,perf_test,render_test}.cpp
   dsp/tests/unit/systems/spectral_state_authoring_test.cpp
   plugins/seraphis/**  (plugin_ids.h, processor.{h,cpp}, parameters/{aether,macro,morph}_params.h,
                         engine/seraphis_engine_config.h, ui/macro_ring_knob.h, tests/integration/**)
   plugins/membrum/**   (src/dsp/{drum_voice,noise_layer}.h and their two tests — unrelated
                         "bloom" in the noise-layer doc text)
$ ls dsp/include/krate/dsp/systems/bloom_engine.h -> No such file or directory
```

**The sweep transcript above was re-run this session and corrected.** An earlier draft of this
document claimed the word `Bloom` appeared only in `aether_reverb.h` and `spectral_morph_engine.h`.
That was wrong: a **third** family exists in `SeraphisEngine` (see the table row below), and the
tests/plugin hits are consequences of all three. The *conclusion* is unchanged — every
`class`/`struct` probe above is still 0 hits — but the transcript is recorded accurately here
because the whole point of recording near-name families is that a later reader can see them.

| Class / symbol | Layer | Header path | ODR sweep result |
|---|---|---|---|
| `BloomEngine` | 3 | `dsp/include/krate/dsp/systems/bloom_engine.h` (new) | **0 hits.** **Three** near-name families exist and none collides, because **all are class-scoped**: (1) `AetherReverb`'s reverb-side harmonic-bloom resonator constants (`effects/aether_reverb.h:1442-1454`, inside `class AetherReverb` opened at `:1377`); (2) `SpectralMorphEngine`'s morph-stagger "bloom" (`systems/spectral_morph_engine.h:100,339,447`); (3) **`SeraphisEngine`'s AetherReverb bloom-trigger plumbing** — see the dedicated row below. None is a type at namespace scope. **Consequence for this phase (FR-003): every constant of `BloomEngine` is class-scoped**, so no `kBloom*` name is added at namespace scope where it could later meet any of the three in one TU. |
| *(near-name family 3, not new — recorded)* `SeraphisEngine::BloomEvents` etc. | 3 | `systems/seraphis_engine.h` | **Read this session; the family the earlier transcript missed, and the one conceptually closest to `BloomEngine`** — both are Layer 3, both are spawn/note-triggered, both face `HarmonicCloud`. Verified symbols, **all class-scoped members of `class SeraphisEngine`**: `static constexpr std::size_t kBloomPartialCap = 32;` (`:261`, itself documented at `:256-260` as a deliberate duplicate of `AetherReverb::kMaxBloomResonators`, `aether_reverb.h:1442`), `struct BloomEvents` (`:293`), `consumeBloomEvents()` (`:968`), `getLastBloomPartials(i)` (`:1038`), `getLastBloomCount(i)` (`:1044`), and the members `bloomOnMask_` / `bloomOffMask_` / `bloomOnPending_` / `lastBloomCount_` / `lastBloomPartials_` (`:370-373`, `:1231-1237`). What it does: it snapshots a voice's held partials and hands `AetherReverb::bloomNoteOn` a note-on/note-off mask one control chunk late (`:962-971`). That is a **reverb resonator excitation**, not a partial-growth lifecycle. **No ODR collision** (nested/class-scoped, no namespace-scope type or constant), and **no reuse**: this phase's `BloomEngine` neither includes `seraphis_engine.h` nor touches it (FR-080). Also present, out of scope but recorded: `SeraphisMacro::Bloom` (`seraphis_macro_matrix.h:50`, an enumerator) and `SeraphisVoice::setBloom` (`seraphis_voice.h:680`), both forwarding to the `SpectralMorphEngine` stagger of family 2. |
| `BloomEngine::PrepareConfig` (nested struct) | 3 | same header | Nested, following `NoiseOrganism::PrepareConfig` (`noise_organism.h:190`), `ResonanceDriftNetwork::PrepareConfig` (`:299`) and `SubharmonicEngine::PrepareConfig` (`subharmonic_engine.h:327`). Several unrelated nested `PrepareConfig` structs already coexist, so nesting is the established, collision-free form. Designated initialisers mandatory (Clang narrowing). |
| `BloomEngine::Relation` (nested enum class) | 3 | same header | Nested. `{ Octave = 0, Fifth = 1, DetunedNeighbour = 2 }` (roadmap line 346). **APPEND ONLY** — it may become a persisted plugin parameter at Phase 12. Nested rather than namespace-scope for the same reason `SubharmonicEngine::Tone` is (`subharmonic_engine.h`, and the `EnvelopeFilter::FilterType` precedent at `processors/envelope_filter.h:89`). |
| `BloomEngine::Child` (nested **private** struct) | 3 | same header | Nested and private, following `ResonanceDriftNetwork::Peak` and `NoiseOrganism::DustGrain` (`noise_organism.h:196-200`). Holds one child's slot index, ratio, spawn amplitude, `Relation`, lifecycle phase and elapsed-seconds clock. `ChildPartial` / `BloomChild` swept clean at namespace scope (0 hits each) and **still rejected** as top-level names: a namespace-scope type for a private implementation detail is a liability for no gain. |

**No new namespace-scope type, no new namespace-scope constant, no new enumerator on an existing
enum, no new free function, no new `ModSource` value.** `tools/lint-odr.js` and
`tools/lint-layers.js` must pass on the result (SC-013).

---

## Functional Requirements

### FR-001 series — Component contract and lifecycle

- **FR-001.** The phase ships exactly one new component, `BloomEngine`, header-only at
  `dsp/include/krate/dsp/systems/bloom_engine.h`, in `namespace Krate::DSP`, declared **Layer 3**
  in its banner (roadmap line 340). Its includes point downward only (Layers 0–2 + stdlib);
  `tools/lint-layers.js` must pass.
- **FR-002.** It renders **no audio** and exposes **no audio entry point**. Its only per-block entry
  point is the FR-005 array transformer.
- **FR-003.** Every constant is **class-scoped** (`static constexpr` members). No namespace-scope
  constant is added — see the ODR table for why.
- **FR-004.** Lifecycle: `void prepare(double sampleRate, const PrepareConfig& config) noexcept`
  and `void reset() noexcept`, matching `SubharmonicEngine::prepare/reset`
  (`subharmonic_engine.h:380`, `:487`). `prepare` allocates nothing; `getAllocatedBytes()` returns
  `0` (the `resonance_drift_network.h:906-912` precedent — the getter exists so Phase 10 can total
  its children uniformly). Re-preparing a live object is legal and fully re-initialises; the
  configured seed survives. A non-finite sample rate is substituted by 48 000 and then floored at
  `kMinUsableSampleRate = 8000.0` (`resonance_drift_network.h:281`).
- **FR-005.** The per-chunk entry point is
  `std::size_t processChunk(float* ratios, float* amplitudes, std::size_t parentCount, std::size_t numSamples) noexcept`.
  It perturbs the two arrays **in place** and returns the partial count the caller must pass to
  `HarmonicCloud::setSpectralTarget`. Argument contract, copied from
  `EntropyProcessor::processChunk` (`entropy_processor.h:269-274`): `ratios == nullptr ||
  amplitudes == nullptr` makes the call a **no-op with no advance** and returns `parentCount`;
  `numSamples == 0` applies the current state **without advancing**; `parentCount` is clamped to
  `capacity()`.
- **FR-006.** **Block-partition invariance.** Given an accepted call, internal state after `N`
  advanced samples is a function of `N` alone and never of how `N` was partitioned into chunks
  (`entropy_processor.h:257-259` wording). Verified by SC-008.
- **FR-007.** Internal advance runs on the shared **64-sample control grid**:
  `static constexpr std::size_t kControlChunkSamples = 64;` with a live
  `static_assert(kControlChunkSamples == 64, "shared 64-sample control grid");`
  (`resonance_drift_network.h:135-136`). `processChunk` accepts **any** `numSamples`; the grid is
  internal bookkeeping, not a call-size requirement.
- **FR-008.** No `std::isnan` / `std::isinf` / `std::isfinite` anywhere in the header; finiteness is
  tested through `detail::isNaN` / `detail::isInf` (`core/db_utils.h:99`, `:260`).
  `tools/lint-nonfinite-symbols.js` must pass.
- **FR-009.** **Argument and output hygiene.** (a) A non-finite float argument to any setter is
  **rejected**: the previous value stands and the getter reports it. (b) An out-of-range index is a
  silent no-op. (c) An out-of-range float is clamped and the getter reports the clamp. (d) On the
  array path, any slot the engine **owns** is written finite, `ratio > 0`, `amplitude >= 0` — so
  this component can never be the cause of `setSpectralTarget`'s wholesale rejection
  (`harmonic_cloud.h` doc block above `:769`). (e) A non-finite value found in an **incoming** slot
  the engine reads for analysis disqualifies that slot from parent selection; the engine never
  copies it into an owned slot and never writes it back.

### FR-010 series — Parent-spectrum analysis (roadmap line 346)

- **FR-010.** On a spawn event (FR-040), the engine selects the **strongest K** partials of the
  supplied amplitude array over the parent region `[0, min(parentCount, reserveBase()))`, where `K`
  is configurable in `[1, kMaxParents]` with `kMaxParents = 8` and **defaults to
  `kDefaultParentCount = 4`**. (Four parents against the default `kChildrenPerEvent = 2` means a
  typical event draws its two children from two different parents, which is what makes a bloom read
  as *spread* rather than as one partial doubling.) The **`min(parentCount, …)` bound is
  load-bearing**: slots at or above `parentCount` hold whatever the caller's array happens to carry
  and are not live partial content — scanning them could select a parent that does not exist.
- **FR-011.** Selection is by **supplied amplitude**, descending; ties break by **lower index**
  (deterministic, and the lower index is the lower partial, which is the darker choice this
  instrument wants). Slots with amplitude `<= kSilentParentAmplitude` (`1e-5f`, matching
  `HarmonicCloud::kTargetAmpEpsilon`, `harmonic_cloud.h:258`) are **not** eligible parents — a
  silent partial has no harmonics to grow.
- **FR-012.** Selection reads only; it never modifies a parent slot. Roadmap line 346 says children
  are *spawned*, not that parents are altered.
- **FR-013.** Selection runs **only on a spawn event**, never per chunk. This is what makes the
  roadmap's "bookkeeping only" cost claim (line 353) true. It is **directly observable**, not merely
  implied by a CPU budget: the engine exposes `getParentScanCount()` (cumulative `std::uint64_t`,
  FR-061), incremented once per executed strongest-K scan, and SC-005 asserts it equals
  `getSpawnEventCount()` after a long run. (A CPU budget cannot police this: a per-chunk scan over
  48 floats costs ~100 ns per 512-sample block against SC-011's 10 667 ns ceiling, so SC-011 passes
  by two orders of magnitude whether FR-013 holds or not.)
- **FR-014.** If fewer than one eligible parent exists at a spawn event, the event is **consumed
  and produces no child**; the engine does not retry within that event and does not error.
- **FR-015.** A spawn event creates **at most `kChildrenPerEvent`** children (configurable,
  `[1, 4]`, default 2). Each child is assigned one parent from the FR-010 selection and one
  `Relation`. (Numbered into the FR-010 series and filed here deliberately: it is a property of the
  *event*, alongside FR-013's scan and FR-014's empty-selection rule. An earlier draft placed it
  under the FR-020 heading, where its number contradicted the block it sat in.)
- **FR-016.** **Parent and relation assignment across one event's children (Clarification Q4).**
  Parents are drawn **without replacement** from the FR-010 K-selection for as long as unused
  parents remain within the event; once every selected parent has supplied a child
  (`kChildrenPerEvent > K`), the remaining children in that same event draw **with replacement**
  from the full K-selection again. Each child's `Relation` is drawn independently **by weight**
  (`setRelationWeight`, FR-060), whose three per-relation weights **default to `1.0` each**
  (uniform) — the value FR-060 previously left unstated. Both draws for one child come from the
  FR-070 seeded stream. FR-022's spacing test is checked against the parent region, any occupied
  owned slot, **and any child already accepted earlier in the same event** — a sibling is a
  candidate for collision even before either is written into the array. Asserted by SC-006's added
  arm.

### FR-020 series — Child spawning (roadmap line 346)

- **FR-020.** `Relation` covers exactly the roadmap's three relationships and no others:
  - `Octave` — child ratio `= parentRatio * 2`;
  - `Fifth` — child ratio `= parentRatio * 1.5`;
  - `DetunedNeighbour` — child ratio `= parentRatio * 2^(c/1200)` with `c` drawn uniformly from
    `[-kMaxDetuneCents, +kMaxDetuneCents] \ (-kMinDetuneCents, +kMinDetuneCents)`,
    `kMaxDetuneCents = 50.0f`, `kMinDetuneCents = kMinRatioSpacingCents = 24.0f`.
    The ±50 cent cap keeps the conversion inside `centsToPitchRatioFast`'s documented accurate
    domain (`harmonic_cloud.h:88-116`); the 24 cent floor is the repo's existing
    partial-collision figure (`entropy_processor.h:80`).
- **FR-021.** **Child ratio bounds are a rejection test, never a clamp.** A computed child ratio
  outside `[kMinChildRatio, kMaxChildRatio] = [0.5f, 128.0f]` causes the **candidate to be
  rejected** — the child is not spawned and `getRejectedSpawnCount()` increments (FR-061). The
  bounds are **never applied by clamping**: clamping moves a partial to a pitch nobody asked for,
  which is a defect; refusing to grow one is not. The two values are those of
  `SpectralState::kMinStateRatio` / `kMaxStateRatio` (`spectral_state.h:51-52`), restated as
  class-scoped constants with a comment naming the source (D-2), and used **only** as the
  comparison bounds of this test.
- **FR-022.** **Minimum spacing.** A candidate child is **rejected** if its ratio lies within
  `kMinRatioSpacingCents = 24.0f` of any partial already present in the array (parent region or an
  occupied owned slot) **or of any child already accepted earlier in the same spawn event**
  (Clarification Q4, FR-016), evaluated in the log-ratio domain
  (`|log2(r_cand) - log2(r_other)| < kMinRatioSpacingCents / 1200`). The figure and its log form are
  taken from `entropy_processor.h:80-81`, not invented. See FR-026 for what happens to a rejected
  candidate.
- **FR-023.** **Spawn amplitude is latched, and compensated for the cloud's slot-indexed tilt
  (Clarification Q1).** The child's target amplitude is
  `parentAmplitude_at_spawn * kChildGain * depth() / tiltGain(childSlotIndex)`, captured at spawn
  and held for the child's life, where `tiltGain` is a **restated, class-scoped copy** of
  `HarmonicCloud::tiltGain` (no `harmonic_cloud.h` include — D-2/D-9 style):
  `tiltGain(index) = std::exp2(consumerTiltDb() * std::log2(static_cast<float>(index + 1)) *
  kLog2TenOver20)`, with `kLog2TenOver20 = 3.32192809488736235f / 20.0f` restated verbatim from
  `harmonic_cloud.h:1433`. `consumerTiltDb()` is a new control surface value,
  `setConsumerTiltDb(float)` / `getConsumerTiltDb()` (FR-060/FR-061), clamped to
  `[kMinConsumerTiltDbPerOct, kMaxConsumerTiltDbPerOct] = [-12.0f, 12.0f]` (restated from
  `HarmonicCloud::kMinTiltDbPerOct` / `kMaxTiltDbPerOct`, `harmonic_cloud.h:194-195`), default
  `0.0f` — at which `tiltGain` is exactly `1.0f` for every index and the formula is bit-identical to
  the pre-Q1 latch. **The division cancels the cloud's own `tiltGain(slot)` multiply
  (`harmonic_cloud.h:1493`) so the child sounds at `parentAmplitude_at_spawn * kChildGain *
  depth()` after the cloud's tilt is applied**, regardless of which slot it lands in — a caller sets
  `consumerTiltDb` to whatever the cloud's own `tiltDb_` currently is (Phase 10's responsibility;
  this component does not read the cloud). `kChildGain` is configurable in `[0, 1]`, default
  `0.35f`. Latching (rather than tracking the parent live) is what prevents a child from pumping
  when the parent's own drift, mutation or entropy moves it — and is what makes SC-002's measured
  fade shape a property of the lifecycle, not of the parent. Asserted by SC-017.
  **The latched target is deliberately NOT clamped to 1.0.** With `kChildGain` configurable up to
  `1.0` and a parent slot that may legitimately exceed 1, the product can exceed 1 — and that is
  fine: `HarmonicCloud::setSpectralTarget` states that "amplitudes above 1 are all **ACCEPTED** —
  that is the point of the surface" (doc block above `harmonic_cloud.h:769`), and FR-009 (d)
  requires only `amplitude >= 0`. Introducing a `<= 1` clamp here would silently attenuate a child
  relative to its parent for no audible reason. Consequently no criterion may assert an absolute
  `[0, 1]` bound on an emitted child amplitude; SC-015 asserts the **relative** bound
  `[0, latched target]` instead.
- **FR-024.** The child's **ratio is also latched** at spawn. Per-partial frequency motion is the
  cloud's job (Overview fact 2) and re-deriving the ratio per chunk would fight it.
- **FR-025.** A spawn event that finds no free owned slot is **consumed without effect** (no
  stealing, no oldest-child eviction). Killing a sounding child to make room is a click by
  construction, and roadmap line 347 gives children a fixed lifecycle, not a stealing policy.
- **FR-026.** **Rejection retry and detuned fallback (Clarification Q2).** On a candidate rejected
  by FR-021 (ratio bounds) or FR-022 (spacing, including the FR-016 sibling clause), the engine
  retries the **same child** by re-drawing parent (FR-016) and/or relation (FR-060) from the seeded
  stream, up to `kMaxSpawnAttempts = 4` total attempts (the first draw plus up to 3 retries). **Every
  rejected attempt — first draw or retry — increments `getRejectedSpawnCount()`** exactly as a
  single-attempt rejection always has, so one eventually-spawned child may still register up to
  `kMaxSpawnAttempts - 1` rejections against it.
  **Final-attempt fallback.** If the 4th (last) attempt's relation is `Octave` or `Fifth` and it
  too would be rejected, the engine does not draw a 5th time; instead it converts that attempt into
  a **detuned variant of the same relation** — ratio `= parentRatio * 2 * 2^(c/1200)` (Octave) or
  `parentRatio * 1.5 * 2^(c/1200)` (Fifth), with `c` drawn uniformly from `[-kMaxDetuneCents,
  +kMaxDetuneCents] \ (-kMinDetuneCents, +kMinDetuneCents)` (the FR-020 `DetunedNeighbour` draw,
  reused rather than duplicated) — and re-tests the result once against FR-021/FR-022. If it now
  passes, the child is spawned and `getFallbackChildCount()` (FR-061, cumulative `std::uint64_t`)
  increments in addition to `getSpawnedChildCount()`; per-child introspection
  (`getIsChildFallback(i)`, FR-061) reports `true` for it, while `getChildRelation(i)` still reports
  the **original** `Octave`/`Fifth` relation (it is a detuned octave/fifth, not a third relation). If
  it still fails, the candidate is rejected (`getRejectedSpawnCount()` increments) and that child
  slot in the event produces nothing — an FR-014-style empty result scoped to one child rather than
  the whole event. A `DetunedNeighbour` relation's 4th attempt is simply another cents redraw; it has
  no separate fallback form, since it is already detuned.
  **RT-bounded and deterministic:** `kMaxSpawnAttempts` is a compile-time constant, so no attempt
  loop can run unbounded, and the full draw sequence for one child is a deterministic function of the
  seeded stream state (FR-070). Asserted by SC-006.

### FR-030 series — Per-child lifecycle (roadmap line 347)

- **FR-030.** Each child runs a four-phase clock: **Idle → FadeIn → Hold → FadeOut → Idle**, in
  seconds, per child, advanced on the FR-007 control grid.
  Defaults from roadmap line 347: `kDefaultFadeInSeconds = 45.0f`,
  `kDefaultHoldSeconds = 120.0f`, `kDefaultFadeOutSeconds = 180.0f`.
  Configurable ranges: fade-in `[1, 300]` s, hold `[0, 900]` s, fade-out `[1, 600]` s.
- **FR-031.** The child's amplitude envelope is **C1-continuous**: value **and** first derivative are
  zero at both ends (roadmap line 351, "C1 amplitude envelopes (no clicks)"). The shape is the
  smoothstep `3u² − 2u³` on `u ∈ [0,1]` for FadeIn and its mirror for FadeOut, which satisfies
  `f(0)=0, f(1)=1, f'(0)=f'(1)=0` exactly. Measured by SC-002 (c).
  **No step-size floor, and no retiming of the user's configured fade times.** A previous draft of
  this FR mandated a prepare-time assert plus a clamp on `fadeInSeconds`/`fadeOutSeconds`, on the
  belief that a per-chunk amplitude increment below `HarmonicCloud::kTargetAmpEpsilon = 1e-5f`
  (`harmonic_cloud.h:258`) would make the cloud's dirty mask skip the update and freeze the fade.
  **That was a misreading of the cloud and is retracted.** Verified this session: the mask compares
  the supplied amplitude against `committedAmp_[i]` (`harmonic_cloud.h:841`), and `committedAmp_[i]`
  is written **only** for a slot a recompute actually consumed (`:1481`, inside the dirty-gated
  branch). The baseline is frozen between recomputes, so sub-epsilon per-chunk motion **accumulates
  until it trips the threshold** — the header names the *opposite* design (stored target as
  baseline) as the one that "accumulates FOREVER and never trips the threshold" (`:827-838`), and
  documents the committed-value comparison as the deliberate fix (deviation D14). The whole-array
  skip (`:803-810`) needs `memcmp`-identical arrays and cannot swallow a non-zero increment either.
  The real, and benign, consequence is that a long fade's **target** advances in ~1e-5 quantised
  steps (−100 dB), which the cloud's own amplitude smoother (`kAmpSmoothTimeSec = 0.002f`,
  `harmonic_cloud.h:165`, `:290`) then smooths on its way to the kernel. Two things follow, and both
  are requirements: (a) `fadeInSeconds` / `fadeOutSeconds` are honoured **as configured** across the
  whole FR-030 range, never silently retimed — a clamp that overrides a legal FR-030 value is a
  defect; (b) the reported child amplitude must be **strictly monotone non-decreasing** across the
  fade-in and monotone non-increasing across the fade-out, reaching the latched target and `0.0f`
  respectively within the configured durations — that, not a step floor, is the property that would
  actually fail if a fade ever stalled. Measured by SC-002 (a), (b), (d).
- **FR-032.** A child in **Idle** occupies no slot. A child leaving FadeOut releases its slot at the
  exact sample its amplitude reaches 0, and the slot is written with the cloud's own padding form
  (`ratio = index + 1`, `amplitude = 0`, `harmonic_cloud.h:825-826`) on that same chunk — FR-051.
- **FR-033.** Lifecycle times are **latched per child at spawn**. Changing `setFadeInSeconds` while
  a child is in flight does not retime that child (retiming a running envelope is a derivative
  discontinuity, i.e. the click FR-031 exists to prevent); the new value applies to children spawned
  afterwards.
- **FR-034.** The lifecycle table is a **fixed-capacity array** of `kMaxChildren = 16`
  `Child` records sized at compile time. No allocation, no container growth, no free list that can
  outgrow the table (roadmap line 347: "managed by a small lifecycle table (no allocation)").
- **FR-035.** **Dormancy** (roadmap lines 527–535). `setDormant(bool)` and `setWake(float)` follow
  the house shape (`noise_organism.h:833,844`; `resonance_drift_network.h:771,784`) and are
  behaviourally identical at zero.
  **Fractional wake is defined, not left to the implementer.** `setWake(float)` takes a value
  clamped to `[0, 1]` (the `noise_organism.h:848` / `resonance_drift_network.h:773-776` shape) and
  **multiplies the FR-041 internal clock's per-step spawn probability, and nothing else** — it does
  **not** scale child gain, child ratio, an in-flight child's envelope, or the FR-040 external
  `triggerBloom()` hook. It composes multiplicatively with `depth` (FR-042), so the per-step
  probability is `rateHz * depth * wake * kControlChunkSamples / sampleRate`. A wake at or below
  `kWakeSilenceEpsilon = 1e-6f` snaps to exactly `0.0f` at the source, following
  `resonance_drift_network.h:266` and its `gateSteady()` snap at `:1249` — which is what makes
  `setWake(0.0f)` and `setDormant(true)` behaviourally identical *by construction* rather than by
  luck, exactly as `resonance_drift_network.h:788-790` describes. The house precedents fold wake
  into a gate **gain** because they have a chain to gate; this component has neither gain nor chain
  (FR-002, D-7), so the only continuous quantity a wake can meaningfully scale is the event rate.
  Asserted by SC-014 (d).
  Dormant means: **no new spawns**, and the FR-040 trigger clock
  **keeps running** (the rule's "its source/generator and modulation lanes keep running"). Children
  already in flight **run their lifecycle to completion** — this component has no audio chain to
  skip, and cutting a child's envelope on a dormancy edge is precisely the discontinuity the
  Dormancy rule's 50 ms re-entry fade exists to avoid elsewhere. Re-entry needs no fade because
  nothing is gated: a wake simply re-enables spawning. FR-035 records this as an explicit,
  justified reading of the rule for a component with no chain, not a silent deviation.
  **An event armed on a control step where spawning cannot occur is discarded, never held
  (Clarification Q7).** Whether armed by the internal clock's Bernoulli draw (FR-041) or by
  `triggerBloom()` (FR-040 (a)), an event that lands on a **dormant**, `wake() == 0`, or
  `depth() == 0` control step is consumed and thrown away on that same step — it does not fire
  retroactively once spawning resumes. This is what makes "wake simply re-enables spawning" literal
  rather than "wake replays what was missed": there is no catch-up burst at a wake edge.
- **FR-036.** **Hold jitter (Clarification Q5).** Each child's Hold segment duration is
  `holdSeconds() * (1 + u)`, where `u` is drawn uniformly from `[-holdJitterFraction(),
  +holdJitterFraction()]` via the FR-070 seeded stream and **latched at spawn** alongside the other
  lifecycle times (FR-033) — `u >= -1` by construction, so the jittered hold is never negative.
  `holdJitterFraction()` (`setHoldJitterFraction(float)` / getter, FR-060/FR-061) is configurable in
  `[0, 1]`, default `kDefaultHoldJitterFraction = 0.5f` (±50 %); `0` disables jitter exactly (every
  child's latched hold equals `holdSeconds()` bit-for-bit). **`fadeInSeconds` / `fadeOutSeconds`
  remain latched unjittered** — jitter touches only the Hold segment's latched duration — so SC-002's
  fade-timing clauses continue to measure the configured value exactly, with no fixture change
  needed. Asserted by SC-002's added arm.

### FR-040 series — Triggering (roadmap lines 349–350)

- **FR-040.** The engine exposes **both** trigger sources the roadmap offers:
  (a) an **external hook**, `void triggerBloom() noexcept`, which arms exactly one spawn event to be
  consumed on the next control step — the entry point Phase 10's `SlowEventScheduler` calls; and
  (b) an **internal seeded "continuous slow probability" clock**, `setSpawnRateHz(float)` in
  `[0, 0.05]` Hz (one event per 20 s at the top, never at the bottom), default
  `kDefaultSpawnRateHz = 1.0f / 240.0f` (one event per 4 minutes — roadmap line 338's "every few
  minutes"). Setting the rate to **0 disables the internal clock entirely**, leaving (a) as the only
  source. The two are additive and independent.
- **FR-041.** The internal clock is a **seeded Bernoulli draw per control step** with per-step
  probability `rateHz * kControlChunkSamples / sampleRate`, so its expected inter-event time is
  `1/rateHz` **independently of sample rate and block partition** (SC-008, SC-010).
  **The draw is made every control step unconditionally (Clarification Q7)** — including while
  dormant, at `wake() == 0`, at `depth() == 0`, or at `rateHz == 0` — so the RNG stream position is a
  **pure function of elapsed control steps alone**, never of the engine's dormancy/wake/depth
  history. (A rate of exactly `0` still draws; it simply can never succeed, since the probability it
  is compared against is `0`.) Asserted by SC-008's added arm.
- **FR-042.** `setDepth(float)` in `[0, 1]`, default `1.0f`, is the Life/Age macro input of roadmap
  line 350. It scales (a) the child spawn amplitude **at the moment of latching** (FR-023) and
  (b) the internal clock's per-step probability, linearly. `depth` is smoothed with a `LinearRamp`
  at `kGainRampMs = 50.0f` (`noise_organism.h:178`) so a macro sweep cannot step a child's latched
  target amplitude; **the smoothed value is what gates both (a) and (b)** — there is no second, raw
  path.
  **Pass-through, stated precisely.** `depth == 0` makes the engine a **bit-identical pass-through
  only while it has never spawned a child since the last `prepare()`/`reset()`**: then no spawn is
  armed, **no owned slot is written at all** (the same "has ever spawned" gate FR-051 puts on the
  return value), and `processChunk` returns `parentCount` unchanged. Two qualifications, both
  load-bearing and both previously missing:
  - Because `depth` is ramped over 50 ms, `setDepth(0.0f)` does **not** make the smoothed depth zero
    for ~50 ms (≈ 37 control chunks at 48 kHz), during which the FR-041 clock can still arm an
    event. A caller that needs the pass-through from the first chunk configures `depth` **before**
    `prepare()`, or snaps the ramp (`LinearRamp::snapTo`, `smoother.h:421`). SC-014 (a) tests that
    precondition explicitly.
  - **With children already in flight, depth 0 is not a pass-through and must not be.** Live
    children keep their **latched** amplitude (FR-023, D-8) and run their lifecycle to completion,
    so owned slots continue to be written and the return value stays `capacity()` per FR-051. Only
    *future* children see the new depth. Cutting a 45-second swell because a macro reached its
    bottom is the click FR-031 exists to prevent.
  SC-014 (a) therefore tests the cold-start pass-through, and the Edge Cases entry for a
  `1 → 0` sweep tests the in-flight case; they are different claims and no longer contradict.
- **FR-043.** `triggerBloom()` is **edge-like, not level-like**: calling it *n* times between two
  control steps arms **one** event, not *n*. This makes the external hook safe for a caller that
  polls a scheduler's `isEventActive()` (`slow_event_scheduler.h:361`) rather than its onset. Per
  FR-035's added clause (Clarification Q7), an event armed this way is discarded, not held, if the
  control step it would resolve on is dormant, `wake() == 0`, or `depth() == 0`.

### FR-050 series — Slot accounting (roadmap line 352)

- **FR-050.** **Capacity contract.** `PrepareConfig` carries `std::size_t capacity = 64;` (clamped
  `[1, kMaxSlots]`, `kMaxSlots = 64` with
  `static_assert(kMaxSlots == 64, "== HarmonicCloud::kMaxPartials (harmonic_cloud.h:138)")`) and
  `std::size_t numChildSlots = 8;` (clamped `[0, min(kMaxChildren, capacity)]`). The engine **owns**
  the top `numChildSlots` indices of `capacity`: the owned region is
  `[reserveBase(), capacity())` with `reserveBase() = capacity() - numChildSlots()`.
  **`capacity` is the caller's promise about how many slots the cloud will actually sound**, i.e.
  `HarmonicCloud::getActivePartialCount()` (`harmonic_cloud.h:950`), NOT `kMaxPartials` — Overview
  fact 1. The contract is documented on `PrepareConfig` in those terms.
  **This is asserted against a real cloud, not only stated.** The roadmap's Phase-7 criterion is
  "reserved-slot accounting never exceeds cloud capacity (**asserted**)" (line 352), and the
  audibility boundary is the cloud's `activeCount_`, not `kMaxPartials`:
  `recalculateAmplitudes` zeroes `baseAmplitude_[i]` and `continue`s for every `i >= activeCount_`
  **before** the spectral-target branch (`harmonic_cloud.h:1469-1473`), with
  `activeCount_ = clamp(round(64^richness), 1, 64)` (`:1462-1463`). A documentation comment on
  `PrepareConfig` cannot fail a build, so **SC-016** drives a real `HarmonicCloud` at
  `richness < 1.0` (so `activeCount_ < 64`), configures `PrepareConfig::capacity` from
  `getActivePartialCount()`, and asserts every live child's slot index is below
  `getActivePartialCount()` **and** that the cloud actually sounds it. Note explicitly that
  `getOverlapEngagementCount()` (FR-052) is **not** a tripwire for this: it tracks
  `parentCount > reserveBase()` only and is completely blind to `capacity > getActivePartialCount()`
  (an earlier draft of A-2 claimed otherwise — corrected there).
- **FR-051.** **Write region, return value, and the gap between them.**
  Let *engaged* mean "`numChildSlots > 0` **and** at least one child has been spawned since the last
  `prepare()`/`reset()`".
  **This latch is sticky, not level-tracking (Clarification Q8).** It is set once and only cleared
  by `prepare()`/`reset()` — it does **not** clear when `getLiveChildCount()` later returns to `0`.
  Once engaged, the engine returns `capacity()` and pads `[parentCount, reserveBase())` on **every**
  subsequent chunk for the rest of the object's life, even across long stretches with zero live
  children. Stated explicitly so an implementation does not "optimise" it back to a pass-through
  when nothing is currently alive. Asserted by SC-003 and SC-014.
  - **Not engaged:** `processChunk` writes **nothing** and returns `parentCount`. (This single gate
    is what makes FR-042's cold-start pass-through, FR-054's `numChildSlots == 0` pass-through and
    FR-035's dormant-from-cold case all bit-identical no-ops without three separate rules — see
    SC-014.)
  - **Engaged:** `processChunk` returns `capacity()`, writes every index in
    `[reserveBase(), capacity())` — an occupied owned slot with its child's latched ratio and
    current amplitude, an **unoccupied** owned slot with the cloud's own padding form
    (`ratio = index + 1`, `amplitude = 0`, `harmonic_cloud.h:825-826`) — and writes **no index at or
    above `capacity()`**.
  - **The gap `[parentCount, reserveBase())` is padded too, and this is a hard requirement.**
    When `parentCount < reserveBase()` (legal, and `parentCount == 0` is an explicit Edge Case),
    those slots are below the returned `capacity()` and therefore **read by the cloud as live
    partial content**, but no one has written them: `HarmonicCloud::setSpectralTarget` validates
    every index in `[0, count)` and rejects the **whole array** on one NaN/Inf or one
    `ratio <= 0.0f` (`harmonic_cloud.h:814-818`), then consumes each as
    `r = (i < count) ? ratios[i] : (i + 1)` (`:825-826`). Stale or uninitialised caller data in the
    gap therefore either sounds as garbage partials or triggers exactly the wholesale rejection
    FR-009 (d) exists to make impossible. So: **when engaged, the engine pads every index in
    `[parentCount, reserveBase())` with the same `ratio = index + 1`, `amplitude = 0` form on that
    chunk**, before returning `capacity()`. This is the only case in which the engine writes below
    `reserveBase()`, it is by construction inaudible (amplitude 0), and it is the reason FR-009 (d)
    is true rather than aspirational. Asserted by SC-003 (hard) and SC-016 (through a real cloud).
- **FR-052.** **Parent overlap.** If `parentCount > reserveBase()`, the parent content in
  `[reserveBase(), parentCount)` is overwritten by the owned region. This is a caller error, not a
  crash: the engine proceeds, and increments `getOverlapEngagementCount()` (a `std::uint32_t`,
  following `getClampEngagementCount()` at `resonance_drift_network.h:904`) so a test or a Phase-10
  integration can assert it is zero.
- **FR-053.** **Invariant, asserted in tests (SC-003):** at every control step, the number of
  children in a non-Idle phase is `<= numChildSlots()`; no two live children share a slot index; and
  every live child's slot index is in `[reserveBase(), capacity())` **as those stood when the child
  was spawned**. **Clarification Q3 qualifies the last clause:** a live child spawned before a
  `setCapacity()` shrink (FR-055) keeps its original slot index even if that index is now
  `>= capacity()`; such a child is simply excluded from every array write for the rest of its life
  (FR-055) rather than being retimed or evicted. The no-shared-slot guarantee is **unchanged** and
  holds regardless of whether the slot lies inside or outside the current owned region: a slot held
  by a legacy child, wherever it sits, is never reused while that child is still alive.
- **FR-054.** `numChildSlots == 0` is legal and makes the component an exact pass-through (same
  observable behaviour as `depth == 0`, FR-042) — the configuration a Phase-10 voice uses to switch
  bloom off without removing it from the chain.
- **FR-055.** **Live capacity changes (Clarification Q3).** `setCapacity(std::size_t)` (FR-060),
  paired with the existing `capacity()` getter (FR-061), lets a caller change capacity after
  `prepare()` without a full re-initialise. The value is clamped `[1, kMaxSlots]`, identically to
  `PrepareConfig::capacity` (FR-050); `PrepareConfig::capacity` itself remains only the *initial*
  value consumed by `prepare()` and is never re-read afterward.
  - **Growth** (`newCapacity > oldCapacity`) takes effect **immediately**: `reserveBase()` and the
    FR-056 round-robin cursor recompute against the new capacity on the very next `processChunk`,
    and the newly available owned slots are eligible for a spawn immediately.
  - **Shrinkage** (`newCapacity < oldCapacity`) is **deferred**: no new child is ever placed at a
    slot `>= newCapacity` (FR-056 skips such slots); a live child already occupying a slot
    `>= newCapacity` is **not** killed, retimed or evicted (FR-033 still forbids that) — it keeps
    advancing its FR-030 lifecycle clock in the table and is simply excluded from every array write
    (FR-051's `capacity()` ceiling) for the rest of its life. When it completes, its table entry is
    freed exactly as FR-032 describes, **except that the final padding write is skipped** because the
    slot lies outside `[0, capacity())`. Such a child is "temporarily inaudible": the cloud never
    receives further updates for that slot past the shrink, but nothing already written is retimed
    or un-written — no click, by the same reasoning FR-025 and FR-033 already rely on.
  - A slot vacated by growth is eligible again only once any legacy child that still holds it (per
    FR-053) completes; it is never reused while that child is alive, matching FR-025's no-stealing
    rule.
  Asserted by SC-003's added arm.
- **FR-056.** **Owned-slot selection policy (Clarification Q6).** A spawning child is placed at the
  slot found by advancing a **round-robin cursor** over `[reserveBase(), capacity())`, starting from
  the cursor's current position, wrapping at `capacity()` back to `reserveBase()`, and skipping
  occupied slots; the cursor always advances past the slot it selects, so successive spawns rotate
  through the owned region rather than collapsing onto the lowest free index (Overview fact 2 is why
  this matters: the slot a child lands in determines its inherited drift/pan/mutation lane). The
  cursor is **one extra `std::size_t` of engine state**, is **reset to `reserveBase()`** by
  `prepare()`/`reset()`, and is re-clamped into `[reserveBase(), capacity())` whenever `setCapacity()`
  (FR-055) moves either bound. **Slot choice consumes no draw from the FR-070 seeded stream** — it is
  deterministic bookkeeping, not a stochastic decision. Asserted by SC-018; the no-RNG-consumption
  clause is additionally covered by SC-008.

### FR-060 series — Control and read surface

- **FR-060.** Control surface (all `noexcept`, all obeying FR-009):
  `setSeed(std::uint32_t)`, `setDepth(float)`, `setSpawnRateHz(float)`,
  `setParentCount(std::size_t)` (K, FR-010), `setChildrenPerEvent(std::size_t)` (FR-015),
  `setChildGain(float)` (FR-023), `setFadeInSeconds(float)`, `setHoldSeconds(float)`,
  `setFadeOutSeconds(float)` (FR-030), `setHoldJitterFraction(float)` (FR-036, Clarification Q5,
  `[0, 1]`, default `0.5f`), `setRelationWeight(Relation, float)` (relative draw weights
  over the three relationships, each `[0, 1]`, **defaulting to `1.0` for each relation**
  (Clarification Q4/FR-016) — all-zero falls back to uniform),
  `setDormant(bool)` / `setWake(float)` (FR-035), `triggerBloom()` (FR-040),
  `setCapacity(std::size_t)` (FR-055, Clarification Q3, `[1, kMaxSlots]`),
  `setConsumerTiltDb(float)` (FR-023, Clarification Q1, `[-12.0f, 12.0f]`, default `0.0f`).
- **FR-061.** Read surface (a public contract, not `#ifdef` scaffolding — the
  `entropy_processor.h:299-302` idiom): `getSeed()`, `getDepth()`, `getSpawnRateHz()`,
  `getParentCount()`, `getChildrenPerEvent()`, `getChildGain()`, `getFadeInSeconds()`,
  `getHoldSeconds()`, `getFadeOutSeconds()`, `getHoldJitterFraction()` (FR-036),
  `getRelationWeight(Relation)`, `isDormant()`,
  `getWakeAmount()`, `getConsumerTiltDb()` (FR-023), `capacity()`, `numChildSlots()`, `reserveBase()`,
  `getLiveChildCount()`, `getSpawnEventCount()` (cumulative, `std::uint64_t`),
  `getSpawnedChildCount()` (cumulative children actually created, `std::uint64_t` — **not** the same
  as the event count, since one event makes up to `kChildrenPerEvent` children and may make none),
  `getFallbackChildCount()` (FR-026, Clarification Q2, cumulative `std::uint64_t` — children spawned
  only via the final-attempt detuned-fallback path, counted separately from
  `getSpawnedChildCount()`, of which it is a subset),
  `getCompletedChildCount()` (cumulative children that reached the end of FadeOut and released their
  slot, `std::uint64_t` — SC-015's "death" observable, which an earlier draft asserted against
  without ever defining it),
  `getParentScanCount()` (cumulative executed strongest-K scans, `std::uint64_t` — FR-013's
  observable),
  `getRejectedSpawnCount()` (FR-014 + FR-021 + FR-022 + FR-025 + every failed FR-026 attempt
  combined, `std::uint64_t`),
  `getOverlapEngagementCount()` (FR-052), `getAllocatedBytes()` (FR-004),
  and per-child introspection by table index: `getChildSlotIndex(i)`, `getChildRatio(i)`,
  `getChildAmplitude(i)`, `getChildRelation(i)`, `getChildPhase(i)`,
  `getChildElapsedSeconds(i)`, `getIsChildFallback(i)` (FR-026, Clarification Q2, `bool`, `false` for
  a non-fallback child and for an out-of-range index). Out-of-range indices return a documented
  neutral value rather than reading past the array.
- **FR-062.** `[[nodiscard]] bool stateFinite() const noexcept` over the lifecycle table, using
  `detail::isNaN`/`detail::isInf` (the `harmonic_cloud.h:1054` idiom), for SC-009.

### FR-070 series — Determinism, footprint, budget

- **FR-070.** **Seed determinism.** All stochastic draws (the FR-041 Bernoulli clock, the FR-020
  detune draw, the FR-015/FR-020 parent-and-relation assignment) come from `Xorshift32` streams
  seeded through `deriveStreamSeed(seed, salt)` (`core/random.h:102`) off an **append-only**
  class-scoped salt table with overlap `static_assert`s (the `resonance_drift_network.h:929-947`
  idiom). Two instances with the same seed, the same configuration and the same input sequence
  produce identical output.
- **FR-071.** **Zero allocation after `prepare`**, and `prepare` itself allocates nothing
  (FR-004). Asserted by SC-007 with `AllocationScope` (`tests/test_helpers/allocation_detector.h:111`).
- **FR-072.** **CPU budget.** The roadmap's "CPU ≈ free (bookkeeping only)" (line 353) is made
  measurable as **≤ 0.1 % of one core per voice at 48 kHz = ≤ 10 667 ns per 512-sample block**, in
  the ns/block basis of `resonance_drift_network_perf_test.cpp:67-80`, best-of-25 × 500 blocks after
  400 warm-up, tagged `[.perf]`, with the percent figure reported and never asserted. The
  **stop-and-surface rule** (`:57-64`) applies verbatim: no implementing agent may lower
  `kMaxChildren`, raise the budget, relax a threshold or shrink the workload to make a figure fit;
  reduce cost or surface the decision to the user.
- **FR-073.** A **stage probe** (`[.perf]`, `WARN`-reported, not asserted) prices the three cost
  terms separately — the per-chunk child bookkeeping, the per-chunk owned-slot writes, and the
  spawn-event parent scan — so a future regression can be attributed rather than guessed.
- **FR-074.** **Footprint is declared, not hidden.** `sizeof(BloomEngine)` is reported by a test and
  `getAllocatedBytes()` returns 0; the whole state is `kMaxChildren = 16` `Child` records plus the
  scalar configuration.

### FR-080 series — Shared components stay untouched

- **FR-080.** This phase modifies **no existing header**. `harmonic_cloud.h`,
  `entropy_processor.h`, `spectral_morph_engine.h`, `seraphis_voice.h`, `spectral_state.h`,
  `harmonic_snapshot.h`, `spectral_coring_estimator.h`, `fft_autocorrelation.h` and
  `sympathetic_resonance_simd.h` are all read-only for this phase (roadmap lines 540–542: shared
  components are Seraphis's consumers too). `git diff --stat` on the phase must show
  `dsp/include/krate/dsp/systems/bloom_engine.h` as the only changed file under
  `dsp/include/`, plus the four new TUs and their two `dsp/tests/CMakeLists.txt` registrations.
- **FR-081.** Seraphis's shipped suites stay green — in particular the Phase-2/3 cloud, morph and
  entropy cases and the Phase-7 voice cases — because none of them can change if FR-080 holds
  (SC-012).

---

## Success Criteria

Each criterion names its measurement and a test-name sketch. TU allocation follows the Phase 5/6
shape: `bloom_engine_test.cpp` (behaviour), `bloom_engine_spectral_test.cpp` (the `[long]` renders),
`bloom_engine_perf_test.cpp` (`[.perf]`), `bloom_engine_nonfinite_test.cpp` (the **only** TU in the
`-fno-fast-math` block, `dsp/tests/CMakeLists.txt:556-900`).

- **SC-001 — Children appear and disappear without clicks** (roadmap line 351).
  Drive a real `HarmonicCloud` (`kMaxPartials = 64`, richness 1.0 so `activeCount_ = 64`) through
  `BloomEngine::processChunk` → `setSpectralTarget` → `processStereoBlock`, with fade-in 2 s, hold
  1 s, fade-out 3 s (time-compressed so the render is affordable; the shape is scale-invariant and
  the 45 s/180 s defaults are covered by SC-002).
  **The detector configuration is pinned, and the gate is differential** — the house pattern at
  `dsp/tests/unit/systems/atmosphere_engine_spectral_test.cpp:398-412` (`countClicks()` with every
  field designated-initialised) and `:414-423` (`smallestZeroSigma()`, so that changing a threshold
  is a measured one-step edit rather than a guess). `ClickDetectorConfig`
  (`tests/test_helpers/artifact_detection.h:37-43`) is constructed with **all six fields explicit**:
  `.sampleRate =` the render rate (the struct default is `44100.0f`, `:38`, and `isValid()` only
  range-checks it, `:46-63` — a wrong rate is used silently), `.frameSize = 512`, `.hopSize = 256`,
  `.detectionThreshold = 5.0f`, `.energyThresholdDb = -60.0f`, `.mergeGap = 5`.
  **Threshold, differential:** for each of 10 seeds, render the **identical** seed and cloud
  configuration twice — once with the bloom engaged, once as a reference with `numChildSlots = 0` —
  and require `detections(bloom) <= detections(reference)`, with detections additionally **windowed
  to the 200 ms following each spawn instant and each child's death instant** (the
  `feedback_ecology_test.cpp:3689-3696` windowed idiom), where the requirement is `0`. An absolute
  "zero detections over the full render" is **not** used: a 64-partial cloud at richness 1.0 beats
  against itself, and 5-sigma frame outliers from partial beating have nothing to do with a bloom.
  The reference run is what separates the two. On failure, report `smallestZeroSigma()` for both
  runs so the verdict is attributable.
  *`BloomEngine_ChildSpawnAndDeathAreClickFree`.*
- **SC-002 — Lifecycle timing and C1 shape.**
  **Fixture:** `childGain = 1.0f` and `depth = 1.0f` are set as **explicit non-default overrides**,
  to isolate the envelope *shape* from the gain scalar (the defaults are `kChildGain = 0.35f`,
  FR-023, and `depth = 1.0f`, FR-042; an earlier draft of this criterion called `childGain = 1.0`
  the default, contradicting FR-023). The parent amplitude is swept over
  `{just above kSilentParentAmplitude, 0.01, 0.35, 1.0}` so the shape claims are not measured only
  at the most favourable amplitude. `getChildAmplitude()` is sampled every control chunk at 48 kHz.
  (a) Fade-in reaches 50 % of the latched target amplitude at `0.5 * fadeInSeconds ± 1 %` and 99 % at
  `>= 0.9 * fadeInSeconds`, at the **default 45 s**.
  (b) Fade-out mirrors it at the default **180 s**, ending at exactly `0.0f`.
  (c) **Derivative continuity at the endpoints — the metric that can actually fail.**
  For each fade segment, let `d[k]` be the first difference of the amplitude series. Assert:
  (i) `mean(|d|)` over the **first 2 %** of the segment and over the **last 2 %** of the segment is
  each `<= 10 %` of `max(|d|)` over that segment; and (ii) the **second** difference has no
  sign-flipping spike at the FadeIn→Hold and Hold→FadeOut junctions (`|d²|` there is within `3×` the
  median `|d²|` of the segment interior).
  The smoothstep `3u² − 2u³` has `f'(u) ∝ 6u(1−u)`, giving ~6 % for (i); a **linear ramp** — the
  realistic C0-only defect FR-031 exists to forbid — gives **100 %** and fails. The previous
  formulation ("no first difference exceeds 3× the median non-zero first difference") is **deleted**:
  it cannot fail for a linear ramp at all (every non-zero difference is identical, so max/median is
  exactly 1.0, against a 3× gate), while the correct smoothstep scores only 1.33 — i.e. it
  discriminated nothing and gated the wrong thing.
  (d) **Monotone and non-stalling** (FR-031's replacement for the retracted step floor): the
  amplitude series is monotone non-decreasing over the fade-in and monotone non-increasing over the
  fade-out, strictly reaches the latched target by `fadeInSeconds` and exactly `0.0f` by
  `fadeOutSeconds`, and no plateau of identical consecutive samples exceeds **1 s**. No
  minimum-first-difference threshold is asserted: for the mandated smoothstep the first non-zero
  first difference at a 45 s fade is `≈ 3·du² ≈ 2.6e-9` (with `du = 64/48000/45`), four orders below
  the `1e-5` an earlier draft demanded — that criterion failed on a *correct* implementation and
  pushed toward breaking the very C1 shape it was meant to protect.
  (e) **Hold jitter is real and does not touch the fades (FR-036, Clarification Q5).** At the default
  `holdJitterFraction = 0.5f`, over 200 seeded two-or-more-child spawn events, at least 90 % of
  events have children whose latched hold durations differ from each other (a rare equal draw is
  tolerated, not required to fail); every latched hold lies within `holdSeconds() * (1 ± 0.5)`. At
  `holdJitterFraction = 0`, every child's latched hold equals `holdSeconds()` exactly, and clauses
  (a)–(d) above are unaffected by the jitter setting either way, since fade-in/fade-out are latched
  unjittered.
  *`BloomEngine_LifecycleTimingAndC1Shape`* `[long]`.
- **SC-003 — Reserved-slot accounting never exceeds capacity** (roadmap line 352).
  Fuzz: 1 000 seeded configurations over `capacity ∈ [1,64]`, `numChildSlots ∈ [0,16]`,
  `parentCount ∈ [0,64]`, `spawnRateHz` at maximum, `childrenPerEvent` at maximum, each advanced
  30 simulated minutes on a coarse grid. **Thresholds, all hard:** every write index observed lies
  in `[reserveBase(), capacity())`, **except** the FR-051 gap padding in
  `[parentCount, reserveBase())`, which is checked separately below; live-child count never exceeds
  `numChildSlots()`; no two live children share a slot; the returned count never exceeds
  `capacity()`; and — with `parentCount <= reserveBase()` — `getOverlapEngagementCount() == 0`.
  **FR-051 gap padding, hard:** whenever the call returns `capacity()` and
  `parentCount < reserveBase()`, every index in `[parentCount, reserveBase())` holds exactly
  `ratio == static_cast<float>(index + 1)` and `amplitude == 0.0f` after the call. The fuzz seeds
  those slots with a **poison pattern** before each call (NaN via a bit pattern through a volatile
  sink, `-1.0f`, and large garbage) so an implementation that leaves them alone fails here rather
  than downstream.
  **Observation method — a canary alone is not sufficient, and an earlier draft relied on one.**
  A canary *beyond* `capacity` detects only overruns past the end; a stray write **into the parent
  region** `[0, parentCount)` — the likelier off-by-one given FR-052's deliberate overlap handling —
  lands inside the array and is invisible to it. So the harness does **both**: (i) an out-of-capacity
  canary whose bytes must be bit-unchanged, and (ii) a **full snapshot of the array taken before
  every `processChunk`**, after which every index in `[0, min(parentCount, reserveBase()))` must be
  byte-identical to the snapshot, the sole documented exception being FR-052's overlap region when
  `parentCount > reserveBase()`.
  **Sticky-engaged arm (Clarification Q8, FR-051).** After the fuzz configuration has spawned at
  least one child and then run long enough for every live child to complete
  (`getLiveChildCount() == 0`), the next `capacity() - 1` calls each still return `capacity()` and
  still pad `[parentCount, reserveBase())` exactly as when children were live — the engine is never
  observed to fall back to returning `parentCount` once it has ever spawned, short of `prepare()` /
  `reset()`.
  **Live capacity change arm (Clarification Q3, FR-055).** 200 additional seeded runs: `prepare()`
  at `capacity = 64`, `numChildSlots = 8`; spawn until several children are live; `setCapacity(32)`
  (a shrink below at least one live child's slot index); continue driving `processChunk` for
  30 simulated seconds and assert (i) no write ever touches an index `>= 32` (the new capacity), (ii)
  every legacy child (slot `>= 32`) keeps advancing its lifecycle clock in the table
  (`getChildElapsedSeconds` strictly increases) with **no retiming** — its phase transitions land at
  the same latched offsets FR-030/FR-033 would have produced absent the shrink, (iii) no two live
  children ever share a slot, including a legacy child and a newly spawned one; then `setCapacity(64)`
  (grow back) and assert new spawns are eligible for slots `>= 32` only once any legacy child that
  still held them has completed.
  *`BloomEngine_SlotAccountingInvariantsUnderFuzz`.*
- **SC-004 — Long-render evolution: never static, never divergent** (roadmap line 353).
  A **30-minute** render of cloud + bloom at defaults, logging per second: spectral centroid
  (`tests/test_helpers/audio_features.h:27,88`), the count of partials above −60 dBFS of the
  strongest, and broadband RMS. **The partial count is read from the cloud, not from FFT bin peaks:**
  it is the number of `i < cloud.getActivePartialCount()` (`harmonic_cloud.h:950`) whose
  `cloud.getPartialCurrentAmplitude(i)` (`:959`) is above −60 dB of the largest such value. (The
  method changes the number materially and was previously unstated.)
  **Thresholds:** (a) *never static, measured **differentially*** — render the identical seed and
  cloud configuration twice, once bloom-engaged and once with `numChildSlots = 0` as the reference,
  and require `max |centroid_on(t) − centroid_off(t)| >= 5 %` of the reference run's mean centroid,
  **sustained for `>= 60 s` after at least one spawn instant**. An absolute "centroid stddev `>= 2 %`
  of its mean" is **deleted**: the cloud's own per-partial Brownian detune, mutation lane and
  per-partial envelopes move the spectrum continuously at defaults (Overview fact 2), so that
  threshold is a property of `HarmonicCloud` and passes with the bloom switched off entirely —
  it tested nothing this phase builds. The partial-count series taking `>= 3` distinct values is
  likewise kept only as a **reference-relative** claim: the bloom run must reach a count the
  reference run never reaches;
  (b) *never divergent* — RMS in the last 5 minutes is within **±1.5 dB** of RMS in minutes 5–10
  (the Membrum infinite-ring pattern, roadmap lines 522–524), peak sample `< 1.0`, and
  `cloud.stateFinite()` true throughout;
  (c) *spawn activity is real, pooled across seeds* — the spawn count over 30 minutes at the default
  1/240 Hz rate is Poisson with `λ = 7.5`, for which `P(X <= 4) = 0.13`: a single-seed
  `>= 5` floor is a coin flip on the seed and invites seed-shopping instead of investigation. So:
  run **5 seeds** and require `Σ getSpawnEventCount() >= 30` against a pooled expectation of
  `λ_total = 37.5` (`P(fail) < 1 %`), with the per-seed counts reported. `λ` is written into the
  test as a named constant so a future reader can recompute the interval rather than guess it.
  *`BloomEngine_ThirtyMinuteEvolutionTrajectory`* `[long]`.
- **SC-005 — Strongest-K selection is correct, and runs only on a spawn event** (FR-010, FR-011,
  FR-013).
  Hand a crafted amplitude array with known ordering, including exact ties and sub-threshold slots;
  assert the selected parents are exactly the K largest, ties resolved to the lower index, and that
  no slot at or below `1e-5f` is ever selected, and that no index at or above
  `min(parentCount, reserveBase())` is ever selected (FR-010). 20 crafted cases plus 200 random
  arrays.
  **The reference ordering must be stable, or the tie cases are arbitrated by accident.**
  `std::partial_sort` is **not** a stable sort, so comparing a stable requirement (FR-011: ties break
  by lower index) against it yields an arbitrary verdict on exactly the crafted tie cases this
  criterion says it covers. The reference is therefore a **`std::stable_sort` over `(amplitude,
  index)` pairs keyed on `(-amplitude, index)`** — equivalently, `std::partial_sort` with an
  explicit comparator that falls back to the lower index on equal amplitude. Either form is
  acceptable; an unqualified `std::partial_sort` is not.
  **FR-013 is asserted here:** after a run spanning many spawn events,
  `getParentScanCount() == getSpawnEventCount()`. A per-chunk scan fails this immediately, where
  SC-011's CPU budget would pass it by two orders of magnitude.
  *`BloomEngine_StrongestKParentSelection`.*
- **SC-006 — Child relationships are exact** (FR-020, FR-021, FR-022, FR-026).
  For 500 seeded spawn events: every **non-fallback** `Octave` child's ratio equals `2 ×` its
  parent's within **0.1 cent**; every **non-fallback** `Fifth` within **0.1 cent** of `1.5 ×`; every
  `DetunedNeighbour` offset lies in `[24, 50]` cents in magnitude; **no** child lands within `24`
  cents of any partial present at spawn **or of any sibling already accepted earlier in the same
  event** (FR-016); **no** child ratio falls outside `[0.5, 128]` (and a candidate that would have is
  counted in `getRejectedSpawnCount()`, never clamped into range — FR-021). A child is "non-fallback"
  when `getIsChildFallback(i)` is `false` (FR-061).
  **Fallback children land in the detuned band, not on the exact interval (Clarification Q2,
  FR-026).** Every child with `getIsChildFallback(i) == true` has relation `Octave` or `Fifth`
  (never `DetunedNeighbour`, per FR-026) and its offset from the **intended** exact interval
  (`2 ×` for Octave, `1.5 ×` for Fifth) lies in `[24, 50]` cents in magnitude — the same band as a
  `DetunedNeighbour` child, by construction (FR-026 reuses that draw).
  **The retry/fallback path is exercised, not dead code.** Run 500 additional events against a
  fixture with an exactly-harmonic parent spectrum (integer ratios, the Q2 hazard scenario) and
  assert `getRejectedSpawnCount() > 0` and `getFallbackChildCount() > 0` over the run.
  **No two children of one event share a ratio within the spacing (Clarification Q4, FR-016).** For
  every event that produced `>= 2` children, every pair's ratios differ by `>= 24` cents in the
  log-ratio domain.
  **Verified through the consumer, not only through the engine's own getters.** Every clause above
  reads `getChildRatio()` / `getChildAmplitude()`, so a correct ratio written to the *wrong slot*, or
  a wrong returned count, is invisible to all of them. So this criterion additionally drives a real
  `HarmonicCloud` and asserts, for a spawned child at slot `s`:
  `cloud.getPartialFrequencyHz(s)` (`harmonic_cloud.h:955`) equals `fundamentalHz * childRatio`
  within the same **0.1 cent** bound, and `cloud.getPartialTargetAmplitude(s)` (`:963`) rises
  monotonically over the fade-in. **Negative control (required, so the clause is known to fire):**
  repeat with `PrepareConfig::capacity` set **above** `cloud.getActivePartialCount()` and assert the
  criterion **fails** — that is Overview fact 1 (`:1469-1473`) made visible.
  *`BloomEngine_ChildRatioRelationships`.*
- **SC-007 — Zero allocation after prepare** (FR-071, roadmap line 520).
  `AllocationScope` (`allocation_detector.h:111`) around 10 000 `processChunk` calls spanning
  many spawn/death cycles, plus every setter and `triggerBloom()`. **Threshold: 0 allocations.**
  `getAllocatedBytes() == 0`. *`BloomEngine_NoAllocationAfterPrepare`.*
- **SC-008 — Determinism and block-partition invariance** (FR-006, FR-070, roadmap line 536).
  (a) Two instances, same seed and configuration, fed the same input, produce identical child
  tables and identical output arrays over 10 simulated minutes — compared through
  `compareFingerprints` (`render_fingerprint.h:122`) on the rendered audio and exactly on the
  integer/enum read surface. **No bit-exact float golden is checked in.**
  (b) The same total sample count delivered as `numSamples = 64`, `512`, `2048`, and a ragged
  `{1, 7, 383, 4096, …}` sequence yields the same child table and fingerprint-equal output.
  (c) **RNG stream position is independent of dormancy history (Clarification Q7, FR-041).** Two
  instances, same seed and configuration: instance A runs `N` control steps never dormant; instance B
  is held dormant for a seeded-random-length prefix of those `N` steps (during which `triggerBloom()`
  is never called) and then woken for the remainder. Compared over the steps **after** B wakes, for
  the same number of further elapsed steps, A and B draw from identical stream positions and produce
  fingerprint-equal output — proving the Bernoulli draw (and any armed-but-discarded event) consumed
  the stream during dormancy exactly as it would have while awake.
  *`BloomEngine_SeedDeterminism`*, *`BloomEngine_BlockPartitionInvariance`*.
- **SC-009 — Non-finite immunity** (FR-008, FR-009).
  In the `-fno-fast-math` TU only, with all non-finite values built from **bit patterns through a
  volatile sink** (never `std::numeric_limits<float>::quiet_NaN()`, which folds on the `-ffast-math`
  legs — `reference_fastmath_nan_in_tests`): inject NaN/Inf into (a) every float setter — rejected,
  previous value stands; (b) incoming `ratios[i]` / `amplitudes[i]` — that slot is never selected as
  a parent and is never copied into an owned slot; (c) the sample rate. After each,
  `stateFinite()` is true and every owned slot written is finite, `ratio > 0`, `amplitude >= 0`.
  *`BloomEngine_NonFiniteGuards`* (`bloom_engine_nonfinite_test.cpp`, the only TU added to
  `dsp/tests/CMakeLists.txt:557-900`).
- **SC-010 — Sample-rate change and re-prepare.**
  At 44 100, 48 000, 88 200, 96 000 and 192 000 Hz, **at `depth = 1.0` and `wake = 1.0`** (FR-042
  and FR-035 both scale the same per-step probability, so `1/spawnRateHz` is the expectation only
  there), the **expected inter-event time** of the internal clock is `1/spawnRateHz` within
  **±10 % over 2 000 events** (FR-041), and a child's fade-in duration in **seconds** is within
  **±0.5 %** of the configured value.
  **The event count is 2 000, not 200, and that is a correctness fix rather than a workload
  increase.** The inter-event time is geometric, so the sample mean of `N` draws has relative
  standard error `1/√N`: at `N = 200` that is 7.1 %, making a ±10 % band just **1.41 σ** — roughly
  16 % of seeds fail per sample rate and ~57 % of seed choices fail at least one of the five rates.
  A red result would be indistinguishable from a defect, and the cheapest fix available to an
  implementer would be to change the seed, which FR-072's stop-and-surface rule forbids. At
  `N = 2 000` the standard error is 2.2 % and ±10 % is ~4.5 σ. The band is stated in the test as the
  derived interval `mean ± 4/√N` so the arithmetic is visible rather than folded into a magic
  number. Re-preparing a live instance
  mid-lifecycle leaves it in the exact post-prepare state (no half-faded child, seed retained).
  *`BloomEngine_SampleRateIndependence`* `[long]`.
- **SC-011 — CPU budget** (FR-072, roadmap line 353).
  ns per 512-sample block at 48 kHz, worst case (`numChildSlots = 16` all live, `parentCount = 48`,
  `K = 8`, `childrenPerEvent = 4`, spawn rate at maximum), best-of-25 × 500 blocks after 400 warm-up.
  **Threshold: ≤ 10 667 ns/block** (0.1 % of one core). Percent reported, not asserted.
  Run alone (`node tools/run-cpu-tests.js dsp_systems_tests`).
  *`BloomEngine_CpuBudget`* `[.perf]`, plus the FR-073 `BloomEngine_StageCostProbe` `[.perf]`.
- **SC-012 — Shared components untouched** (FR-080, FR-081, roadmap lines 540–542).
  `git diff --name-only` for the phase contains no existing file under `dsp/include/`;
  `dsp_systems_tests`, `dsp_processors_tests` and the Seraphis cloud/morph/entropy/voice cases are
  green. Asserted as a checklist item in the compliance pass, plus a test of the **contract
  constants this spec actually depends on**, as `static_assert`s:
  `HarmonicCloud::kMaxPartials == 64` (`harmonic_cloud.h:138`, FR-050),
  `HarmonicCloud::kControlChunkSamples == 64` (`:144`, FR-007),
  `HarmonicCloud::kTargetAmpEpsilon == 1e-5f` (`:258`, FR-011's silent-parent threshold), and
  `EntropyProcessor::kMinRatioSpacingCents == 24.0f` (`entropy_processor.h:80`, FR-022).
  **`sizeof(HarmonicCloud)` is deliberately NOT pinned.** An earlier draft asserted it; that is an
  ABI figure for a class holding many 64-element float arrays plus smoothers, and it legitimately
  differs across MSVC / GCC / Apple Clang (padding, SIMD member alignment) and across build
  configurations. Pinning it would turn the cross-toolchain legs SC-013 exists to guard into a red
  that has nothing to do with FR-080.
  *`BloomEngine_CloudContractAssumptions`.*
- **SC-013 — Lints and portability** (roadmap lines 525, 537–538).
  `node tools/lint-odr.js`, `node tools/lint-layers.js`, `node tools/lint-nonfinite-symbols.js`,
  `node tools/lint-float-bit-goldens.js`, `node tools/lint-simd-aligned-loadstore.js` and
  `node tools/check-portability.js` all clean; the header compiles under g++/libstdc++ as well as
  MSVC (the Phase-3 WSL/MSYS2 precedent). Clang-tidy `dsp` target clean.
- **SC-014 — Off means off, bit-identically** (FR-042, FR-054, FR-035, FR-051).
  All three arms below are **from a cold start** — i.e. the engine has not spawned a child since
  `prepare()`/`reset()`, which is precisely the "not engaged" state in which FR-051 requires it to
  write **nothing**. (An earlier draft attached "from a cold start" only to the dormant arm, leaving
  the depth arm reading as an unconditional claim that contradicted the in-flight Edge Case, and
  left FR-051's padding writes contradicting the memcmp. FR-051's single "engaged" gate resolves
  both: no spawn ever ⇒ no owned-region write ever ⇒ the arrays are untouched.)
  (a) **`depth == 0`.** *Precondition, explicit:* `depth` is configured **before** `prepare()`, or
  snapped with the `LinearRamp::snapTo` idiom (`smoother.h:421`), so the smoothed depth is never
  non-zero — FR-042 ramps `depth` over `kGainRampMs = 50.0f` (≈ 37 control chunks at 48 kHz), and
  setting it to 0 *after* prepare leaves a window in which the FR-041 clock can arm a spawn and
  break the memcmp on a perfectly correct implementation. The assertion window starts at the first
  chunk after `prepare()`.
  (b) **`numChildSlots == 0`.**
  (c) **`setDormant(true)`.**
  In each arm, over 100 000 chunks: `processChunk` returns `parentCount`, both arrays are
  **bit-unchanged** (`std::memcmp == 0`), and the rendered cloud output is bit-identical to a render
  with no `BloomEngine` in the chain at all.
  (d) **Fractional wake scales the event rate linearly** (FR-035). With `wake ∈ {0.0, 0.25, 0.5,
  1.0}` at a fixed seed and `spawnRateHz` at maximum, `getSpawnEventCount()` over a fixed simulated
  duration is within the SC-010 interval of `wake ×` the `wake = 1.0` count, and is **exactly 0** at
  `wake = 0.0` — the arm that makes `setWake(0.0f)` and `setDormant(true)` demonstrably identical
  rather than merely asserted to be.
  *`BloomEngine_DisabledIsBitIdenticalPassThrough`.*
- **SC-015 — Worst-case boundedness soak** (roadmap lines 522–524).
  8 simulated hours (accelerated: control-grid stepping with no audio render) at the worst-case
  configuration of SC-011, across 25 seeds. **Thresholds:**
  (a) live-child count stays within `numChildSlots()`;
  (b) **every emitted child amplitude lies in `[0, latched target]`, and every latched target equals
  `parentAmplitude_at_spawn * childGain * depth / tiltGain(childSlotIndex)` within `1e-6`**
  (FR-023, Clarification Q1 — at the run's default `consumerTiltDb = 0`, `tiltGain` is exactly `1`
  for every slot, so this is `parentAmplitude_at_spawn * childGain * depth` unchanged). An absolute
  `[0, 1]` bound is
  **not** asserted: `HarmonicCloud` accepts amplitudes above 1 by design (doc block above
  `harmonic_cloud.h:769`), FR-023 deliberately does not clamp the latched target, and FR-009 (d)
  requires only `>= 0` — so an absolute bound would either fail on a correct engine or pass only
  because the fixture happened to synthesise parents `<= 1`, proving nothing;
  (c) every emitted ratio stays in `[0.5, 128]` (FR-021, by rejection);
  (d) `stateFinite()` true throughout;
  (e) **counters, against expectations written down rather than left to the implementer.**
  The configuration is slot-saturated and the naive `rate × time` is the wrong expectation: at
  `spawnRateHz = 0.05` and `childrenPerEvent = 4`, demand is `0.2` children/s, while 16 slots at the
  default `45 + 120 + 180 = 345` s lifetime sustain only `16/345 ≈ 0.046` children/s — roughly 77 %
  of offered children are refused by FR-025, and the realised death rate is **slot-limited, not
  rate-limited**. So assert:
  `getSpawnEventCount() ≈ spawnRateHz * depth * wake * T` (the exact Bernoulli mean, FR-041) within
  **±25 %**; `getCompletedChildCount() ≈ min(childrenPerEvent * events, numChildSlots * T /
  (fadeIn + hold + fadeOut))` within **±25 %**, with the test **reporting which branch of the `min`
  is active** so a future reader sees the saturation rather than re-deriving it; and
  `getSpawnedChildCount() + getRejectedSpawnCount()` accounts for every offered child.
  (f) **no stall:** `getLiveChildCount()` does not hold a single constant value for more than
  60 simulated minutes, and `getSpawnEventCount()` advances at least once per 60 simulated minutes
  (a stalled bloom is the "drone that dies overnight" failure).
  (The `getCompletedChildCount()` and `getSpawnedChildCount()` observables are added to FR-061 by
  this criterion; an earlier draft asserted against a "death counter" the read surface never had.)
  *`BloomEngine_EightHourAcceleratedSoak`* `[long]`.
- **SC-016 — Children land where the cloud can actually sound them** (roadmap line 352, FR-050,
  FR-051, Overview fact 1).
  The roadmap's one asserted Phase-7 criterion is reserved-slot accounting against **cloud**
  capacity, and the real audibility boundary is `activeCount_`, not `kMaxPartials`
  (`harmonic_cloud.h:1462-1463`, `:1469-1473`). SC-001 pins richness to 1.0 (so `activeCount_ == 64`
  and the hazard cannot fire) and SC-003's fuzz uses no cloud at all, so without this criterion the
  failure mode the Overview calls latent stays undetected.
  Drive a **real `HarmonicCloud` at `richness < 1.0`** (chosen so `getActivePartialCount() ≈ 32`),
  set `PrepareConfig::capacity = cloud.getActivePartialCount()` (`harmonic_cloud.h:950`) per FR-050
  and A-2, and spawn children. **Thresholds, all hard:** every live child's slot index is
  `< cloud.getActivePartialCount()`; every live child's
  `cloud.getPartialTargetAmplitude(slot) > 0` (`:963`) once past its fade-in onset — i.e. the child
  is **audible**, not merely written; `cloud.hasSpectralTarget()` is true after every
  `processChunk` → `setSpectralTarget` handoff, which is the direct test that the array was
  **accepted** rather than wholesale-rejected (`harmonic_cloud.h:814-818`), covering FR-051's gap
  padding and FR-009 (d) end to end. Repeat with `parentCount ∈ {0, 1, reserveBase()/2}` so the gap
  region is exercised, and with a **poisoned** gap (NaN through a volatile sink, `-1.0f`) so an
  unpadded implementation fails here.
  *`BloomEngine_ChildrenAreAudibleWithinCloudActiveCount`.*
- **SC-017 — Tilt-compensated children land at the intended level** (FR-023, Clarification Q1).
  Drive a real `HarmonicCloud` at `richness = 1.0` (`activeCount_ = 64`) with
  `cloud.setSpectralTiltDb(-6.0f)`, and `BloomEngine::setConsumerTiltDb(-6.0f)` matching it. Spawn
  children at `childGain = 1.0f`, `depth = 1.0f` (explicit overrides, matching SC-002's isolation
  fixture) across a spread of parent slots and reserved child slots so the tilt's slot-dependence is
  exercised. **Threshold:** once a child's fade-in has completed, `cloud.getPartialCurrentAmplitude
  (childSlot)` is within **±0.5 dB** of `parentAmplitude_at_spawn` (the intended
  `parentAmplitude * kChildGain * depth` level at these overrides) — i.e. the cloud's own
  slot-indexed tilt (`harmonic_cloud.h:1493`) is cancelled by FR-023's division to within the stated
  band, not merely non-zero (which SC-016 already covers at `tiltDb = 0`).
  **Negative control:** repeat with `BloomEngine::setConsumerTiltDb(0.0f)` while the cloud's tilt
  stays at `-6.0f` (a caller that forgot to sync the two) and assert the measured level is **outside**
  the ±0.5 dB band by more than 10 dB — so the criterion is known to fire on the exact mismatch Q1
  identified, not just on a correctly-wired fixture.
  *`BloomEngine_TiltCompensationMatchesIntendedLevel`.*
- **SC-018 — Owned-slot selection rotates rather than collapsing to one lane** (FR-056,
  Clarification Q6). Over 200 simulated spawn/death cycles at `numChildSlots = 8` (spawn a child,
  run it to completion, repeat), record the sequence of slot indices assigned. **Thresholds:** (a)
  every one of the 8 owned slots is used at least once within the first 16 cycles (round-robin
  guarantees full rotation within `numChildSlots` cycles, allowing one extra cycle for an occupied-
  slot skip); (b) two consecutive cycles never reuse the same slot when another owned slot is free;
  (c) with two children of one event spawned into different slots simultaneously (`childrenPerEvent
  >= 2`), the cursor still advances correctly and no slot is double-assigned (cross-checked against
  SC-003's no-shared-slot invariant); (d) the sequence of slot indices is identical across two runs
  with the same seed and identical to a run with a **different** seed (slot choice is Clarification
  Q6's "consumes no draw" claim, made concrete: the sequence must not change when only the RNG seed
  changes, since it is derived from cursor state, not the stream).
  *`BloomEngine_OwnedSlotRoundRobinRotation`.*

---

## Edge Cases

- **`processChunk(nullptr, …)` / `(…, nullptr, …)`** — no-op, no advance, returns `parentCount`
  (FR-005, the `entropy_processor.h:272-274` shape). A component that advanced on a rejected call
  would break FR-006.
- **`numSamples == 0`** — applies current state without advancing (FR-005). This is the path a
  Phase-10 `prepare()`/`reset()` uses to populate the array before the first render, exactly as
  `EntropyProcessor` documents at `:261-263`.
- **`parentCount == 0`** — legal. No eligible parents, so FR-014 consumes any armed event with no
  child; owned slots (if any child is still alive from before) continue their lifecycle. **And, if
  the engine is engaged, the whole region `[0, reserveBase())` is padded** with
  `ratio = index + 1`, `amplitude = 0` (FR-051), because the engine returns `capacity()` and the
  cloud reads **every** index below the returned count (`harmonic_cloud.h:814-818`, `:825-826`).
  Without that padding the caller's stale bytes would sound as garbage partials, or one NaN would
  wholesale-reject the array. Covered by SC-003's poison-pattern clause and SC-016.
- **`parentCount > reserveBase()`** — FR-052: overlap is overwritten, counter increments, no crash.
- **`capacity == 1`, `numChildSlots == 1`** — `reserveBase() == 0`; every parent slot overlaps.
  Legal, counter fires, still bounded. Covered by SC-003's fuzz.
- **`numChildSlots == 0`** — exact pass-through, FR-054/SC-014.
- **Spawn event with every owned slot live** — consumed without effect, `getRejectedSpawnCount()`
  increments (FR-025). **No stealing**, because stealing a sounding child is a click.
- **All three relation weights zero** — falls back to uniform (FR-060), rather than producing zero
  children forever.
- **A very long `fadeInSeconds` / `fadeOutSeconds` (up to the FR-030 maxima of 300 s / 600 s)** —
  honoured **as configured**, never retimed. The per-chunk amplitude increment falls below
  `HarmonicCloud::kTargetAmpEpsilon = 1e-5f`, which quantises the *target* into ~1e-5 (−100 dB)
  steps and nothing more: the cloud's mask compares against the **committed** value
  (`harmonic_cloud.h:841`), committed only inside a recompute (`:1481`), so sub-epsilon motion
  accumulates and trips the threshold (`:827-838`) instead of stalling, and the kernel's amplitude
  smoother (`kAmpSmoothTimeSec = 0.002f`, `:165`, `:290`) smooths the result. An earlier draft of
  this document had this edge case **backwards in two ways at once** — it named a *small*
  `fadeInSeconds` as the trigger (the step is *inversely* proportional to the fade time, so a small
  fade gives a *large* step and can never breach a lower bound) and prescribed clamping *upward*
  (which lengthens the fade and shrinks the step further, diverging rather than converging). Both
  the clamp and the floor are retracted; see FR-031. SC-002 (d)'s monotone/no-plateau assertion is
  what now covers the residual concern.
- **`holdSeconds == 0`** — legal: FadeIn hands straight to FadeOut. C1 continuity holds because both
  segments have zero derivative at the junction (FR-031).
- **Sample-rate change mid-life** — `prepare()` fully re-initialises; no child survives it
  (SC-010). A child whose remaining seconds were re-derived under a new rate would retime mid-flight,
  which FR-033 forbids for the same reason.
- **Very low sample rate** — floored at `kMinUsableSampleRate = 8000.0`
  (`resonance_drift_network.h:281`). Lifecycle times are in seconds and the FR-041 clock's per-step
  probability is rate-normalised, so nothing about the timing changes.
- **Very high sample rate (192 kHz)** — the per-control-chunk amplitude increment shrinks 4× against
  48 kHz, making the target-quantisation of a long fade correspondingly finer. **That is all it
  does.** An earlier draft called this "the single most likely silent-failure path in the phase" and
  required a rate-aware prepare-time check, on the retracted premise that a sub-epsilon step freezes
  the cloud's update — it does not (FR-031, and `harmonic_cloud.h:827-838` naming the opposite
  design as the broken one). The real 192 kHz risks are timing drift and event-rate drift, and those
  are what SC-010 measures at that rate: fade duration in **seconds** within ±0.5 %, and mean
  inter-event time within the derived interval.
- **Seed 0** — legal; `deriveStreamSeed` never hands a lane 0 (`core/random.h:102`, and the
  `harmonic_cloud.h:686-696` rationale for why that matters).
- **Dormancy toggled while children are in flight** — children run to completion, spawning stops
  (FR-035). Wake re-enables spawning with no fade, because nothing was gated. **An event armed
  (internal clock or `triggerBloom()`) during the dormant stretch is discarded on the same control
  step, never held for the wake instant** (Clarification Q7, FR-035/FR-043) — wake does not produce
  a catch-up burst.
- **All `kMaxSpawnAttempts` retries rejected, including the final detuned-fallback attempt**
  (Clarification Q2, FR-026) — that child slot in the event produces nothing; the event's other
  children (if any) are unaffected. `getRejectedSpawnCount()` reflects every failed attempt;
  `getFallbackChildCount()` and `getSpawnedChildCount()` are unaffected by this slot.
- **`setCapacity()` shrinks below a live child's slot** (Clarification Q3, FR-055) — that child is
  not killed or retimed; it keeps advancing internally and is simply excluded from every array write
  until it completes. `getLiveChildCount()` still counts it. A later `setCapacity()` growth back does
  not reclaim its slot while it is still alive (FR-053).
- **`depth` swept 1 → 0 while children are in flight** — children keep their **latched** spawn
  amplitude (FR-023); only future children are affected. A live `depth` multiply on a sounding child
  would make a macro sweep a gain gesture, which is Phase 10's job to decide, not this component's
  to impose. **This is therefore NOT a pass-through**: the engine stays *engaged*, owned slots keep
  being written, and `processChunk` keeps returning `capacity()` until the last child completes
  (FR-042, FR-051). SC-014's bit-identical arms are cold-start only, and do not contradict this.
- **Denormals** — child amplitudes approaching zero at the end of a 180 s fade-out reach exactly
  `0.0f` by construction (FR-030), not asymptotically, so no denormal accumulates. The library-wide
  FTZ/DAZ setup (`tests/test_helpers/enable_ftz_daz.h`, wired in `dsp_test_main.cpp`) applies
  regardless.

---

## Decisions taken where the roadmap is silent

- **D-1 — Array-in/array-out, not a `HarmonicCloud&`.** The engine takes the ratio/amplitude arrays
  rather than a cloud reference. Forced by the verified facts that `HarmonicCloud` has no
  per-partial ratio/amplitude setter (only `setSpectralTarget`, `harmonic_cloud.h:769`) and that
  `setSpectralTarget` compares against the *committed* target and skips bit-identical arrays
  (`:776-786`, `:833-846`) — so a bloom that wrote through the cloud would have to reconstruct the
  whole 64-slot array anyway. Keeping the arrays as the interface also makes the component testable
  with no cloud instance, keeps Layer 3 → Layer 3 coupling out of the header, and matches
  `EntropyProcessor::processChunk` (`entropy_processor.h:269`) exactly.
- **D-2 — Ratio bounds restated, `spectral_state.h` not included.** `[0.5, 128]` is copied from
  `SpectralState::kMinStateRatio` / `kMaxStateRatio` (`spectral_state.h:51-52`) as class-scoped
  constants with a source comment, rather than by including a Seraphis morph header that would drag
  `SpectralStateId` and the authored-state validity model into a component with no states.
- **D-3 — A bespoke lifecycle clock, not `MultiStageEnvelope` or `GrowthEnvelope`.** Measured caps:
  `MultiStageEnvelope::kMaxStageTimeMs = 10000.0f` (10 s, `multi_stage_envelope.h:65`) and
  `GrowthEnvelope::kMaxDuration = 60.0f` s (`growth_envelope.h:98`). Neither reaches 45 s fade-in ×
  180 s fade-out. Sixteen per-child `float` second-clocks plus a smoothstep is smaller than either
  component anyway, which is why roadmap line 348 calls the lifecycle manager the new part.
- **D-4 — The four analysis components named by reuse row line 115 are read and not consumed.**
  `HarmonicSnapshot` (96-slot Innexus capture format), `SpectralCoringEstimator` (needs an STFT,
  estimates residual), `FFTAutocorrelation` (pitch tool, allocates in `prepare`),
  `processSympatheticBankSIMD` (driven-resonator audio kernel). Roadmap lines 342–344 pre-authorise
  this: the partial state comes from the cloud, and those components "remain available if an
  audio-domain variant is wanted later". Recorded so a later reader does not read the reuse row as
  an unmet obligation.
- **D-5 — "CPU ≈ free" made a number.** 0.1 % of one core per voice = 10 667 ns/block at 48 kHz
  (FR-072). An unmeasurable budget is not a functional requirement, and roadmap line 526 says CPU
  budgets **are** FRs. The figure is deliberately tight — one twentieth of Phase 5's amended 1.5 %
  — because the roadmap's claim is that the children cost nothing beyond bookkeeping.
- **D-6 — Both trigger sources ship.** Roadmap line 349 offers "`SlowEventScheduler` (Phase 1) **or**
  continuous slow probability". The internal clock is required for the component to be testable and
  deterministic standalone (SC-004, SC-015 need spawns without a Phase-10 host); the external hook
  is required by the house rule that Phase 10 owns the scheduler (`noise_organism.h:842`). Shipping
  one would force the other to be bolted on later.
- **D-7 — Dormancy reading for a component with no chain** (FR-035). The cross-cutting rule
  (roadmap lines 527–535) is written for components with an audio chain to skip. Here, "skip the
  chain" has no referent; the justified reading is *no new spawns, clocks keep running, live
  children finish*. Stated in the spec rather than decided silently in code, per the rule's own
  "a spec that wants … must say what the listener would hear that justifies it": cutting a child's
  45-second swell at a dormancy edge is an audible click, and there is no compensating benefit.
- **D-8 — Latched spawn amplitude and ratio** (FR-023, FR-024). The alternative — tracking the
  parent live — makes every child an amplitude-follower of a partial that is itself drifting,
  mutating and entropy-scattered, which converts the bloom from "new harmonics grow" into "existing
  harmonics get a doubled echo". Latching is also what makes SC-002's measured fade shape
  attributable to the lifecycle.
- **D-9 — Tilt law restated, `tiltDb_`/`tiltGain` not read through a cloud reference** (FR-023,
  Clarification Q1). Compensating the cloud's slot-indexed tilt (Existing-components table, fact
  (l)) needs the same law the cloud applies, but D-1 forecloses holding a `HarmonicCloud&` and D-2
  already established the pattern for exactly this situation: restate the two constants involved
  (`kLog2TenOver20`, and the `[-12, 12]` tilt range) as class-scoped values with a source comment,
  rather than including `harmonic_cloud.h` to reach a private `tiltGain()` member function that is
  not part of its public surface. The caller (Phase 10) is responsible for keeping
  `BloomEngine::setConsumerTiltDb` in sync with `HarmonicCloud::setSpectralTiltDb`; this component
  has no way to read the cloud's live value without the reference D-1 rejected.

---

## Open Questions

**The roadmap defers no decision to this spec.** Its Open Questions list (lines 544–559) assigns
every item elsewhere: final name → Phase 11 (#1); ecosystem rule set → Phase 8 (#2); cavern space
topology → Phase 9 (#3); subharmonic placement → **already decided in Phase 6** (#4); voice count and
ghost placement → Phase 10 (#5); macro roster trim → Phase 10/12 (#6); MPE mapping → Phase 11/12
(#7). Phase 7's own roadmap text (lines 335–354) contains no "decide in spec" clause.

Everything this spec had to settle beyond the roadmap is recorded in *Decisions taken where the
roadmap is silent* (D-1 … D-8) with its evidence, and each is bound by an FR and a success
criterion. Two of them are the kind a user may wish to overrule before implementation, and are
flagged here **as decisions already taken, not as blockers**:

- **OQ-A (bound by D-5 / FR-072 / SC-011):** the 0.1 % per-voice CPU ceiling is this spec's
  interpretation of "≈ free". If the FR-073 stage probe later shows the ceiling is unreachable
  without cutting `kMaxChildren`, the **stop-and-surface rule** (`resonance_drift_network_perf_test.cpp:57-64`)
  requires the measured table be put to the user rather than the line moved — the same route that
  amended Phase 2 (1 → 1.75 %) and Phase 5 (1 → 1.5 %).
- **OQ-B (bound by D-7 / FR-035 / SC-014):** the dormancy reading for a chain-less component. If a
  future phase wants a dormancy edge to *kill* live children, that is a spec amendment with an
  audible-consequence justification, not an implementation choice.

---

## Traceability

| Roadmap statement | Line(s) | Covered by |
|---|---|---|
| New component L3 `systems/bloom_engine.h` | 340 | FR-001, FR-002, new-components table |
| Input: current partial state direct from `HarmonicCloud`, no FFT | 342–344 | FR-005, FR-010, D-1, D-4, Non-Goals |
| `HarmonicSnapshot`/`SpectralCoringEstimator` remain available | 344 | D-4, existing-components table |
| Picks strongest K partials | 346 | FR-010, FR-011, FR-013, FR-015, SC-005 |
| Spawns children: octave / fifth / detuned neighbour | 346 | FR-020, FR-021, FR-022, SC-006 |
| Into reserved cloud partial slots | 346 | FR-050, FR-051, FR-052, FR-053, SC-003, SC-016 |
| 45 s fade-in, minutes hold, 3 min fade-out per child | 347 | FR-030, FR-031, FR-033, SC-002 |
| Managed by a small lifecycle table (no allocation) | 347 | FR-034, FR-071, SC-007 |
| Trigger: `SlowEventScheduler` **or** continuous slow probability | 349 | FR-040, FR-041, FR-043, D-6 |
| Depth scaled by Life/Age macros | 350 | FR-042, SC-014 |
| Children appear/disappear with C1 envelopes, no clicks | 351 | FR-031, SC-001, SC-002 (c), (d) |
| Reserved-slot accounting never exceeds cloud capacity (asserted) | 352 | FR-050–FR-053, SC-003, **SC-016** (the cloud-side assertion; SC-003 alone asserts against the caller's promise, not against the cloud) |
| 30 min render: never static, never divergent | 353 | SC-004 |
| CPU ≈ free (bookkeeping only; partials render in the SIMD bank) | 353–354 | FR-013 (asserted by SC-005's scan-count clause, **not** by the CPU budget), FR-072, FR-073, D-5, SC-011 |
| Reuse row: peak analysis and partial banks exist, lifecycle manager is new | 115 | D-3, D-4, existing-components table |
| ODR sweep before any new class name | 127–129 | New-components table (sweep transcript) |
| RT safety: no allocation/locks/exceptions/IO | 520–521 | FR-004, FR-071, SC-007 |
| Boundedness is the theme-level FR; worst-case soak | 522–524 | SC-004 (b), SC-015 |
| Layer discipline + ODR sweep | 525 | FR-001, FR-003, SC-013 |
| CPU budgets are FRs, measured | 526 | FR-072, SC-011 |
| Dormancy rule | 527–535 | FR-035 (incl. fractional wake), D-7, SC-014 (c), (d) |
| No bit-exact float goldens | 536 | SC-008, SC-013 (`lint-float-bit-goldens.js`) |
| Portability; `check-portability.js`; SIMD lint | 537–538 | SC-013 |
| Naming conventions | 539 | FR-003, new-components table |
| Shared-component changes keep Seraphis green | 540–542 | FR-080, FR-081, SC-012 |

---

## Assumptions

- **A-1.** Phase 10's `VoragoVoice` calls `BloomEngine::processChunk` between its spectrum producer
  and `HarmonicCloud::setSpectralTarget`, once per 64-sample control chunk — the
  `seraphis_voice.h:1050-1054` position. This spec does not require it; FR-005/FR-006 make the
  component correct at any call size. Stated so the Phase-10 author does not have to re-derive it.
- **A-2.** Phase 10 sets `PrepareConfig::capacity` from `HarmonicCloud::getActivePartialCount()`
  (`harmonic_cloud.h:950`) rather than from `kMaxPartials`, per FR-050. If it does not, children
  above the cloud's richness-derived active count are silently inaudible — Overview fact 1.
  **The tripwire is SC-016, not `getOverlapEngagementCount()`.** An earlier draft named the overlap
  counter here; that was wrong. Per FR-052 the counter tracks `parentCount > reserveBase()` only and
  is completely blind to `capacity > getActivePartialCount()` — the two failures are unrelated.
  SC-016 is the criterion that drives a real cloud at `richness < 1.0` and asserts audibility, and
  it is the one a Phase-10 integration test should mirror.
- **A-3.** The roadmap's "45 s fade-in / 3 min fade-out" are **defaults**, not fixed constants;
  FR-030's configurable ranges are this spec's reading. A build that hard-coded them would fail
  SC-010's timing sweep at non-48 kHz rates for no benefit.
- **A-4.** No new test helper is required by this phase. Every metric SC-001 … SC-016 needs already
  exists: click detection (`artifact_detection.h:130`), centroid (`audio_features.h:88`),
  mean/stddev/median (`statistical_utils.h:41,76,90`), fingerprints (`render_fingerprint.h:122`),
  allocation detection (`allocation_detector.h:111`). If the plan stage finds a gap, it is added
  beside the existing helpers with a documented reason (the Phase 4/5/6 precedent). SC-016 needs no
  new helper either: it reads `HarmonicCloud`'s existing introspection surface
  (`getActivePartialCount` `:950`, `getPartialTargetAmplitude` `:963`, `hasSpectralTarget` `:868`).
- **A-5.** The four new TUs are registered in `dsp/tests/CMakeLists.txt` inside the enumerated
  `dsp_systems_tests` list (`:324-464`), and **only** `bloom_engine_nonfinite_test.cpp` is added to
  the `-fno-fast-math` block (`:556-900`) — the Phase 2/3/4/5/6 pattern, whose rationale the list
  states at length: the other TUs must prove the FR-008/FR-009 guards under the `/fp:fast` +
  `-ffast-math` mode the header actually ships in, and the perf TU must stay out because
  `-fno-fast-math` would move the figures its baselines are pinned to.

---

## Review notes

Every issue raised in the review of this document was **accepted and applied**; none was rejected.
Three were resolved by a mechanism different from the one suggested, and those deviations are
recorded here so the reviewer can see them rather than diff for them. **No threshold was relaxed to
dodge an issue** — the two thresholds that moved (SC-010's event count 200 → 2 000, SC-004 (c)'s
spawn floor pooled across 5 seeds) both moved in the *stricter* direction, because in each case the
issue proved the original band was a seed lottery rather than a gate.

- **RN-1 — FR-015 was relocated, not renumbered.** The issue offered "renumber FR-015 to FR-016 **or**
  move it under the FR-010 series". Renumbering would churn every reference to FR-015 in FR-060,
  FR-070, the Edge Cases and the traceability table for no semantic gain, and would leave a permanent
  gap in the FR-010 block. FR-015 is now filed under the *FR-010 series — Parent-spectrum analysis*
  heading, where its number belongs and where it reads correctly beside FR-013's scan rule and
  FR-014's empty-selection rule. The mismatch between the number and the enclosing heading — the
  actual defect — is gone.
- **RN-2 — SC-015's amplitude bound was restated relatively rather than clamped in FR-023.** The
  issue offered either. Adding a `<= 1.0` clamp to the latched child target would contradict
  `HarmonicCloud`'s own documented contract that "amplitudes above 1 are **ACCEPTED** — that is the
  point of the surface" (doc block above `harmonic_cloud.h:769`) and would silently attenuate a child
  relative to a parent that legitimately exceeds 1. FR-023 now says explicitly that the target is not
  clamped and why; SC-015 (b) asserts `[0, latched target]` and the latch formula to `1e-6`.
- **RN-3 — FR-051's gap was closed by padding, not by changing the return value.** The issue offered
  padding `[parentCount, reserveBase())` **or** returning `max(parentCount, highest occupied owned
  index + 1)` with a contiguity rule. Padding was chosen because it is exactly what `HarmonicCloud`
  already does above its own `count` (`ratio = index + 1`, `amplitude = 0`, `harmonic_cloud.h:825-826`),
  so the engine and its consumer agree by construction; the alternative introduces a variable return
  value whose correctness depends on which owned slots happen to be occupied this chunk, which is
  harder to assert and easy to get subtly wrong under FR-025 refusals.

Two corrections of fact are called out in place in the body, because each had load-bearing
consequences that a silent edit would have hidden:

- **The `HarmonicCloud` dirty-mask misreading.** Verified this session: the mask compares against
  `committedAmp_[i]` (`harmonic_cloud.h:841`), committed only inside the dirty-gated recompute
  (`:1481`), and the header names the *opposite* design as the one where "sub-epsilon per-chunk
  motion accumulates FOREVER and never trips the threshold" (`:827-838`). A sub-epsilon fade is
  therefore **quantised, never frozen**. The whole FR-031 step-size-floor apparatus — the
  prepare-time assert, the (inverted) upward clamp on user-configured fade times, SC-002 (c)'s
  unsatisfiable `1e-5` minimum-first-difference threshold, and the "single most likely silent-failure
  path in the phase" framing of the 192 kHz Edge Case — is retracted and replaced by SC-002 (d)'s
  monotone/no-plateau assertion, which fails only if an update really does stall.
- **The ODR sweep transcript was not accurate as written.** Re-running
  `grep -rn "Bloom" dsp/ plugins/ --include=*.h --include=*.cpp` this session shows a **third**
  near-name family the earlier transcript omitted — `SeraphisEngine`'s `BloomEvents` (`:293`),
  `kBloomPartialCap` (`:261`), `consumeBloomEvents()` (`:968`) and the `bloom*Mask_` members — which
  is the family conceptually closest to `BloomEngine` and therefore the one most worth recording.
  The transcript and the New-components table are corrected; the conclusion (0 hits for every
  `class`/`struct` probe) survives the corrected sweep.

---

## Clarifications

### Session 2026-09-14

- **Q1 (spectral tilt mis-levels every child, since `HarmonicCloud::tiltGain` is slot-indexed, not
  ratio-indexed):** Compensate at latch via a restated tilt law. Add `setConsumerTiltDb(float)` /
  `getConsumerTiltDb()` and a class-scoped restatement of `tiltGain(index) =
  exp2(tiltDb * log2N[index] * log2(10)/20)` (D-2/D-9 style, no `harmonic_cloud.h` include); divide
  the latched child amplitude by the tilt gain of the child's slot so it lands at
  `parentAmplitude * kChildGain * depth` after the cloud's own tilt is applied. One SC arm at
  `tiltDb = -6` on a real cloud render proves the child level matches within a stated dB band.
  [FR-023, FR-060, FR-061, SC-017]
- **Q2 (FR-022's 24-cent spacing rule rejects almost every Octave/Fifth child on a harmonic parent
  spectrum):** Bounded seeded retry — on a spacing or ratio-bound rejection, re-draw the parent
  and/or relation up to `kMaxSpawnAttempts = 4` (seeded, deterministic, RT-bounded) — with the final
  attempt, if still rejected, falling back to a detuned variant of the same relation (Octave →
  detuned octave, Fifth → detuned fifth, offset by a seeded 24–50 cents) rather than a further
  redraw. Every rejected attempt (first draw or retry) still increments `getRejectedSpawnCount()`;
  fallback successes are counted separately by a new `getFallbackChildCount()`. SC-006's 0.1-cent
  exact-ratio clause is scoped to non-fallback children, with a separate clause requiring fallback
  children to land within the 24–50 cent band of the intended interval.
  [FR-026, FR-061, SC-006]
- **Q3 (`PrepareConfig::capacity` is prepare-only, but the cloud's audible partial count can change
  at runtime via a live Richness macro):** Add `setCapacity(std::size_t)` plus getter: growth takes
  effect immediately; shrinkage is deferred — no new child is placed at or above the new capacity,
  and children already above it finish their lifecycle in place (temporarily inaudible, no click,
  never retimed). `PrepareConfig::capacity` remains only the initial value. FR-053's no-shared-slot
  invariant is unchanged. An SC arm covers shrink-then-grow with live children.
  [FR-053, FR-055, SC-003]
- **Q4 (how parents and relations are assigned across one event's children, and whether spacing is
  checked against same-event siblings):** Parents are drawn without replacement while the K-selection
  allows (falling back to with-replacement only once every parent has supplied a child); relation is
  drawn per child by weight, with `setRelationWeight` defaulting to `1.0` for each relation; the
  24-cent spacing rule is checked against the parent region, occupied owned slots, **and**
  already-accepted siblings of the same event. An SC arm proves no two children of one event share a
  ratio within the spacing.
  [FR-016, FR-022, FR-060, SC-006]
- **Q5 (are per-child lifecycle times jittered, and does every child of an event fade/hold/die in
  lockstep):** Seeded jitter on the Hold segment only, default ±50 % of the configured hold, latched
  per child at spawn; fade-in and fade-out durations remain latched **unjittered**, so SC-002's timing
  clauses continue to measure the configured values exactly. Added a setter/getter for the jitter
  fraction, with `0` disabling it. An SC arm proves two children of one event get different holds.
  [FR-036, FR-060, FR-061, SC-002]
- **Q6 (which free owned slot a new child takes, given that Overview fact 2 makes the slot
  determine the child's inherited drift/pan/mutation lane):** Round-robin cursor over the owned
  region `[reserveBase(), capacity())`, skipping occupied slots; one extra `std::size_t` of state,
  reset by `prepare()`/`reset()`; slot choice consumes no draw from the seeded RNG stream.
  [FR-056, SC-018]
- **Q7 (does the internal Bernoulli clock's RNG draw and an armed `triggerBloom()` behave the same
  regardless of dormancy/wake/depth, and is an armed event held across a dormancy edge):** The
  internal Bernoulli clock consumes exactly one draw per control step regardless of `p`, so the RNG
  stream is a pure function of elapsed control steps alone — two instances differing only in
  dormancy history stay bit-identical once compared over equal elapsed steps. An armed
  `triggerBloom()` (or internal-clock arm) is consumed and discarded on a dormant (or `wake == 0` /
  `depth == 0`) control step, never held — a dormancy edge is never an audible event, and wake simply
  re-enables spawning with no catch-up burst.
  [FR-035, FR-041, FR-043, SC-008]
- **Q8 (is FR-051's "engaged" latch sticky for the object's life, or does it clear when the last
  child completes):** The engaged latch is sticky until `prepare()`/`reset()`: once any child has
  been spawned, the engine returns `capacity()` and pads `[parentCount, reserveBase())` on every
  chunk for the rest of the object's life, even with zero live children. Stated explicitly in FR-051
  and in SC-003/SC-014 so an implementer does not optimise it away.
  [FR-051, SC-003, SC-014]

---

## Review notes (clarification session)

Every clarification answer above lands as a change to at least one FR and, where the answer
introduces a new observable, at least one SC — no clause is carried only by this log. Three answers
(Q4, Q5, Q6) also fill in values the original FRs left unstated (`setRelationWeight`'s default,
whether hold is jittered, which free slot is chosen) rather than reversing a stated behaviour, so
those edits read as completions of FR-015/FR-016, FR-030 series and FR-050 series rather than as
corrections. No threshold named in the original Success Criteria was relaxed by any answer; SC-006's
0.1-cent clause is narrowed in *scope* (to non-fallback children) because Q2 introduces a second,
distinct class of child (fallback) that the original criterion had no way to describe, not because
the exact-ratio bound itself was loosened for the children it still applies to.
