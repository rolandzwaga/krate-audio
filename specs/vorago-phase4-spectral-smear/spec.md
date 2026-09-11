# Feature Specification: Vorago Phase 4 — Spectral Smear

**Spec slug:** `vorago-phase4-spectral-smear`
**Roadmap source:** `specs/Vorago-roadmap.md` → Part A → Phase 4 (lines 243–259); reuse-inventory row
`L4 Spectral Smear` (line 112); ODR note (lines 127–129); cross-cutting constraints (lines 490–511).
**Layer:** one new Layer 2 component, `dsp/include/krate/dsp/processors/spectral_smear.h` (roadmap line 248).
**Test target:** `dsp_processors_tests` (enumerated source list, `dsp/tests/CMakeLists.txt:156-294` —
`add_executable(dsp_processors_tests` opens at `:156` and the list closes at `:294`, which is *after*
the three Vorago Phase-1 TUs at `:291-293`); the `-fno-fast-math` block is
`dsp/tests/CMakeLists.txt:510-821` (`if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")` at `:510`,
`set_source_files_properties(` at `:511`, `PROPERTIES COMPILE_FLAGS "-fno-fast-math
-fno-finite-math-only"` at `:821`; the most recent entries sit in its tail, around `:775-820`).
**Depends on:** nothing from Vorago Phases 1–3. The analysis/synthesis pair is `STFT` /
`OverlapAdd` (`primitives/stft.h:35` / `:204`), the bin store is `SpectralBuffer`
(`primitives/spectral_buffer.h:45`), the RNG is `Xorshift32` + `deriveStreamSeed`
(`core/random.h:41` / `:102`) — all shipped and read this session.
**Plugin work:** none. The Vorago plugin starts at Phase 11; phases 1–10 are KrateDSP-only.

## Overview

Spectral Smear is Vorago's fog. It is a Layer 2 stereo processor that sits on the **global** bus
(roadmap architecture diagram, line 78 — between the subharmonic engine and the cavern space) and
makes the drone sound *far away and old* without adding a single reflection. It does this with one
STFT round trip per channel and two independent per-bin operations inside it:

1. **Magnitude smearing in time** — a normalised one-pole leaky integrator per bin, running at the
   frame rate, with a **frequency-dependent** time constant so low bins hold their energy for seconds
   while high bins follow the input almost immediately (roadmap line 250, "lows smear longer").
   This is the "distance / age" half: spectral detail stops being crisp, transients stop arriving
   intact, and the spectrum becomes a slow-moving average of itself.
2. **Phase decoherence** — a per-bin phase perturbation redrawn every hop (roadmap line 250, "phase
   decoherence amount"). This is the "fog" half: a tonal partial stops being a line and becomes a band
   the width of the analysis window.

The two halves are separately controllable, separately measurable, and **separately mis-attributed by
the roadmap's own success criterion** — see the note under SC-001, which is the single most important
sentence in this spec.

**The magnitude half does not exist anywhere in KrateDSP.** The phase half exists **twice** and both
copies are embedded, phase-only, and pinned by their own determinism criteria:
`AtmosphereEngine::pumpBlur` (`systems/atmosphere_engine.h:2303`) states at `:2316` "MAGNITUDE IS
NEVER WRITTEN — only the phase moves, so the stage is a decoherer and not a filter", and
`AetherReverb`'s spectral-diffusion stage says the same at `effects/aether_reverb.h:620-621`
("MAGNITUDES ARE NEVER TOUCHED (FR-061)"). The roadmap's reuse row (line 112) describes the Atmosphere
copy as "embedded **per-grain**"; that is **stale** — it is a bus-wide stereo STFT stage downstream of
the grain sum, not per-grain (`atmosphere_engine.h:744-745`, `pumpBlur(n)` called once per control
chunk on the mixed `busL_`/`busR_`). The correction matters because it removes the only reading under
which "extract the existing blur" would have been a mechanical refactor.

**Nothing is extracted, and neither shipped engine is touched** — the roadmap's own conditional
("refactor that engine to consume the shared component **only if it is a genuine drop-in**", line 253)
is discharged with evidence in FR-070 and D-5, not with a preference.

What this phase *does* reuse, verbatim and with citations, is the hard-won **knowledge** in those two
stages: the mandatory 75 %-overlap-plus-synthesis-window geometry (`primitives/stft.h:224-227`), the
frame-major/channel-minor loop order (`atmosphere_engine.h:2273-2298`), the warm-up **counter** that
makes reported latency exactly `fftSize` for every block partition (`aether_reverb.h:643-660`), and
the incoherent-summation level loss that phase randomisation causes and its measured make-up curve
(`aether_reverb.h:623-643`, table at `:2775`). Each of those is a bug this phase would otherwise
rediscover.

## Clarifications

### Session 2026-09-11

- **Q1 — Is the per-bin magnitude memory zero-initialised, or primed from the first frame?** → Prime
  from the first analysed frame after `prepare()`, `reset()` and a poison clear (`state[k] = mag[k]`
  via one per-channel flag), then integrate normally; no start-up fade, unity steady-state gain and
  FR-063's boundedness unaffected; the warm-up discard used by SC-001/SC-004 is `2 * fftSize`, not
  `5 * tau`. [FR-004, FR-022, FR-062, SC-001, SC-004, SC-016, SC-018]
- **Q2 — SC-004's flux metric: measurement geometry, the form of "reduction", and the input band?** →
  Flux = mean over frames of `sum_k |Δmag_f[k]|` divided by `sum_k mag_f[k]`, per band, on an
  independent analysis STFT at the component's own `fftSize`/hop (fixed at 1024/hop 256 on both sides
  for arm (e)'s `kMinFftSize` comparison only); "reduction" is always the ratio
  `flux(amount=0)/flux(amount=1)`, never the subtractive form `1 − flux(1)/flux(0)`; SC-004's input
  noise is band-limited to `[20 Hz, 16 kHz]` so both measurement bands carry energy; the helper is a
  plain function added to `tests/test_helpers/`, reused by every SC-004 arm. [SC-004]
- **Q3 — SC-017: what exactly is `f`, "elapsed frames"?** → Corrected to
  `f = max(0, floor((n − fftSize)/hopSize) + 1)`, matching FR-013's frame advance inside
  `while (canAnalyze())`; the control grid stays tied to analysis frames; no implementation change.
  [SC-017]
- **Q4 — Ratify or reject SC-001's re-attribution of the roadmap's flatness criterion?** → Ratified by
  amending the roadmap (`specs/Vorago-roadmap.md` Phase 4 success criteria, line 257) to read
  "spectral-flatness increase monotonic with decoherence amount; per-bin magnitude flux reduction
  monotonic with smear amount"; the amendment is applied in the plan stage. This spec now satisfies the
  roadmap literally — the Traceability DEVIATION row is removed and Open Question 3 is closed as
  ratified. [SC-001, SC-004]
- **Q5 — Tilt versus the per-bin tau re-clamp: is the saturation intended?** → Keep
  `kTiltExponentRange = 0.75` and the per-bin clamp; document the saturation (at the shipped defaults
  the clamp engages below ~95 Hz at `tilt = +1`) as normative behaviour, not a bug. [FR-032]
- **Q6 — The `enabled = false` read surface, and the default value of `PrepareConfig::enabled`?** → A
  prepared-but-disabled instance still answers geometry queries (`getFftSize()`, `getHopSize()`,
  `getNumBins()`, `getSampleRate()`) with the same clamped/snapped values an enabled twin reports, and
  FR-053's applied reads mirror their targets directly; `getLatencySamples()` and `getAllocatedBytes()`
  stay `0`. `PrepareConfig::enabled` now defaults to `false` (fog is opt-in), a deliberate divergence
  from `AtmosphereEngine::PrepareConfig::blurEnabled_`'s `true` default, recorded in the header comment.
  [FR-002, FR-018, FR-019, FR-052, D-11, SC-002]
- **Q7 — SC-005's soak configuration: what does "sample-rate-accelerated" mean?** → No acceleration:
  48 kHz, reference geometry (`fftSize = 2048`, hop 512, Hann), the full 30 minutes of real audio,
  tagged `[long]`, run in isolation like every other timing-adjacent case even though it asserts levels
  rather than wall clock. [SC-005]

## Scope

In scope:

- One new Layer 2 component, `SpectralSmear`, at
  `dsp/include/krate/dsp/processors/spectral_smear.h` (roadmap line 248), header-only, stereo in /
  stereo out, 100 % wet, constant reported latency.
- The **STFT round trip** built on the shipped `STFT` / `OverlapAdd` / `SpectralBuffer`
  (roadmap line 250), at a prepare-time FFT size with a fixed 75 % overlap.
- **Per-bin magnitude smearing**: normalised leaky integrator per bin per channel, frequency-dependent
  time constants, lows longer (roadmap line 250).
- **Phase decoherence amount**: per-bin phase perturbation redrawn every hop, with the coherence
  make-up that incoherent overlap-add requires (roadmap line 250).
- **Smear amount and tilt as modulation targets** (roadmap line 255) — plain scalar setters an external
  owner writes from a `TidalModulator`; the component owns no modulator (Phase 2/3 precedent).
- Unit tests covering the roadmap's five Phase-4 success criteria (lines 257–259) plus the cross-cutting
  gates from roadmap lines 490–511 (boundedness soak, seed determinism, sample-rate change, block-
  partition invariance, zero allocation, layer/ODR lints, portability, no bit-exact goldens).

## Non-Goals (owned by later phases, or deliberately excluded)

- **Amending `AtmosphereEngine` or `AetherReverb`.** FR-070 records the verified reasons neither can
  consume this component as a drop-in. Both headers must be byte-unchanged at the end of this phase
  (SC-014).
- **A dry/wet mix control.** The component is a series fog stage; `smearAmount = 0` **and**
  `decoherence = 0` is the transparency point (FR-021, FR-041), reached by an exact-identity gate
  rather than by a parallel dry path, so there is no second latency-alignment problem to get wrong.
  A mix is Phase 10's business if the voice architecture wants one.
- **Owning a modulator or a scheduler.** Following `NoiseOrganism::setSourceWake`
  (`systems/noise_organism.h:844`) and `ResonanceDriftNetwork::setPeakWake`, every modulation target
  here is a plain scalar setter. `SpectralSmear` is **not** a `ModulationSource` and adds no
  `ModSource` enumerator.
- **Placement in the signal chain.** Phase 10 (`VoragoEngine`) decides that Spectral Smear sits after
  the voice sum and the subharmonic engine and before the cavern space. This phase produces a stereo
  block and a control surface; nothing drives it yet.
- **Spectral damping, tilt-EQ, or any magnitude *shaping*.** The integrator has unity steady-state
  gain by construction (FR-020); this component never changes the long-term average spectrum, only how
  fast it is allowed to change. Static spectral tilt already exists (`processors/spectral_tilt.h:88`)
  and is not duplicated here. The `tilt` control in FR-032 tilts the **time constants**, not the
  spectrum.
- **Bin-to-bin (frequency-axis) smearing.** `SpectralGate::setSmearing`
  (`processors/spectral_gate.h:442`) already does a frequency-axis box average — of *gate gains*,
  inside the gate, kernel sized `1 + amount·(fftSize/64 − 1)` (`:703-711`). It is not a time-axis
  process, it is not extractable, and it is not what roadmap line 250 asks for. Recorded so its
  absence is not read as an oversight.
- **Freeze / infinite hold.** `SpectralFreezeOscillator` and `AetherReverb`'s freeze already own that;
  the finite `kMaxSmearSeconds = 10 s` bound (FR-031) is what keeps every reachable pole strictly below
  1 and therefore keeps this component from becoming a second, undocumented freeze — FR-023's
  `kMaxPole` clamp is a backstop below that bound, not the mechanism (FR-023 carries the arithmetic).
- **Oversampling, multi-resolution or adaptive FFT sizing.** One prepare-time geometry, no runtime
  size changes — a latency that moves mid-render is a click plus a host renegotiation
  (`aether_reverb.h:614-616`).

## Existing components (verified this session)

Every row was opened and read in this session; signatures are quoted from the file.

| Component | Header (verified) | What Phase 4 reuses / relies on |
|---|---|---|
| `STFT` (L1) | `primitives/stft.h:35` | **The analyser.** `prepare(size_t fftSize, size_t hopSize, WindowType window = WindowType::Hann, float kaiserBeta = 9.0f) :58`, `reset() :90`, `pushSamples(const float*, size_t) :104`, `canAnalyze() :134`, `analyze(SpectralBuffer&) :144`, `fftSize() :178`, `hopSize() :179`, `latency() :183` (returns `fftSize_`), `isPrepared() :185`. **Three load-bearing facts.** (a) `canAnalyze()` is `samplesAvailable_ >= fftSize_` (`:137`) — the **first** frame needs a full `fftSize`, which is the entire origin of the reported latency. (b) `analyze()` consumes only `hopSize_` (`:171`), so a drain loop must be `while (canAnalyze())`, not `if`. (c) `pushSamples` has **no overflow guard** — the ring is `fftSize * 8` (`:78`) and the header says so at `:76-77`; FR-012's internal 64-sample chunking is what makes overflow structurally unreachable rather than merely unlikely. Not modified. |
| `OverlapAdd` (L1) | `primitives/stft.h:204` | **The synthesiser.** `prepare(size_t fftSize, size_t hopSize, WindowType, float kaiserBeta, bool applySynthesisWindow = false) :229`, `reset() :277`, `synthesize(const SpectralBuffer&) :289`, `samplesAvailable() :319`, `pullSamples(float*, size_t) :329`, `isPrepared() :368`. **The binding constraint this phase inherits** is the header's own contract at `:224-227`: the synthesis window is *"Required for spectral modification processors … at >=75% overlap where Hann² satisfies COLA. Must NOT be used at 50% overlap (Hann² not COLA)."* This component modifies spectra, so the geometry is forced: hop = fftSize/4 with `applySynthesisWindow = true` (FR-010). Second fact: `synthesize()` always accumulates at offset 0 (`:300-308`) and the per-frame hop offset comes **only** from `pullSamples` shifting the buffer left (`:349-357`) — two synthesises without an intervening pull of exactly `hopSize` destroy COLA and present as a windowing bug (`atmosphere_engine.h:2284-2294` documents having hit this). Third: `pullSamples` returns **silently** when `numSamples > samplesReady_` (`:339-342`) without zeroing the destination, so a wrong pull size is a stale-buffer read, not a detectable failure. Not modified. |
| `SpectralBuffer` (L1) | `primitives/spectral_buffer.h:45` | **The bin store**, one per channel. `prepare(size_t fftSize) :61` (allocates `fftSize/2+1` bins, `:62`), `reset() :71`, `getMagnitude(size_t) :84`, `getPhase(size_t) :91`, `setMagnitude(size_t,float) :98`, `setPhase(size_t,float) :106`, `numBins() :166`, `data() :148/:156`, `isPrepared() :169`. **The cost model this phase is designed around:** the class keeps a lazy dual representation with dirty flags, so polar↔Cartesian conversion happens **at most once per frame at a representation boundary**, not per accessor call (`:7-12`, `ensurePolarValid() :179`, `ensureCartesianValid() :190`), each a SIMD bulk call into Layer 0. Writing **both** magnitude and phase therefore costs the *same* two bulk conversions as writing phase alone — the magnitude half of this component is close to free relative to the round trip it shares. Out-of-range bin indices return `0.0f` / are ignored (`:85`, `:99`), so the per-bin loops need no bounds branch. Not modified. |
| `computePolarBulk` / `reconstructCartesianBulk` (L0) | `core/spectral_simd.h:37` / `:46` | Consumed **indirectly**, only through `SpectralBuffer`'s lazy boundaries. `void computePolarBulk(const float* complexData, size_t numBins, float* mags, float* phases) noexcept` and `void reconstructCartesianBulk(const float* mags, const float* phases, size_t numBins, float* complexData) noexcept` — Highway-dispatched. This component calls neither directly (it has no raw spectrum of its own), but they are why FR-020's per-bin magnitude write is cheap. Not modified. |
| `FFT` / `Complex` (L1) | `primitives/fft.h:128` / `:55` | Owned privately by `STFT`/`OverlapAdd`; never touched here. Supplies the geometry bounds this component clamps inside: `kMinFFTSize = 256` (`:44`), `kMaxFFTSize = 8192` (`:47`). |
| `AtmosphereEngine` (L3) | `systems/atmosphere_engine.h:2303` (`pumpBlur`) | **The phase-decoherence prior art — read in full, and deliberately NOT consumed or refactored** (FR-070, D-5). Four verified facts. (a) The perturbation law is `spectrum.setPhase(k, spectrum.getPhase(k) + blurAmount * kPi * blurRng_.nextFloat())` over `k` in `[1, numBins-1)` (`:2344-2346`), skipping DC and Nyquist because their phase is not free in a real spectrum (`:2325-2328`). (b) **Magnitudes are never written** (`:2316`). (c) At a settled amount of exactly `0.0f` the stage takes an exact-identity skip that still **burns the same per-bin RNG draws** (`:2332-2342`) so the stream position its SC-010 pins is unchanged — this phase copies the *shape* of that gate (FR-021, FR-041) and its draw-burning rule (FR-043). (d) The loop order is normative: frame-major / channel-minor with the amount smoother advanced once per frame-pair *before* the value is read (`:2273-2283`, `:2310-2311`), the pull inside the drain loop (`:2284-2294`), and L strictly before R (`:2295-2298`). Constants read: `kBlurSmoothMs = 50.0f` (`:282`), `kMinBlurFftSize = 256` / `kMaxBlurFftSize = 4096` (`:319-320`), `kBlurSalt = 0x2000` (`:332`), `kControlChunkSamples = 64` (`:271`), `blurHopSize_ = blurFftSize_ / 4` with an `assert` that the 75 % geometry holds (`:419`, `:458-459`), `getLatencySamples()` returning `blurEnabled_ ? blurFftSize_ : 0` (`:1129`). **Not modified.** |
| `AetherReverb` (L4) | `effects/aether_reverb.h:605-665` | **The second phase-decoherence prior art — read, and NOT consumed** (this component is Layer 2 and could not include a Layer 4 header in any case; `tools/lint-layers.js:74` fails a lower layer reaching up). Three facts this phase reuses as *knowledge*: (a) the geometry is identical to the one FR-010 adopts — "fftSize = diffusionFftSize_ and hop = fftSize/4 (75 % overlap) with applySynthesisWindow = true. THAT COMBINATION IS MANDATORY" (`:610-614`), `diffusionFftSize_ = std::clamp(std::bit_floor(requestedFft), 256, 4096)` (`:1624-1625`); (b) **coherence make-up** — "Independently randomised per-frame phases sum INCOHERENTLY, so OverlapAdd's COLA factor … is wrong by a level that grows with the amount — about 6 dB at amount 1" (`:623-626`), with five measured knots `{1.0000, 1.0799, 1.3435, 1.7746, 1.9996}` shipping as `kCoherenceMakeup` (`:2775`) interpolated by `Interpolation::cubicHermiteInterpolate` (`core/interpolation.h:84`) in `coherenceMakeup(float) :4019`, applied **as a scalar on the pulled time-domain samples, never on the bins** (`:635-637`), and expected to transfer across fftSize because the incoherent/coherent ratio `sqrt(sum w^4)/sum w^2` depends on the window and the overlap *count*, not on fftSize (`:640-643`); (c) the **warm-up counter** rule — draining "as soon as something is available" yields a partition-dependent offset of `(ceil(fftSize/slice) − 1) * slice` (960 / 1020 / 1022 samples for slices 64 / 30 / 7 at fftSize 1024), "Never fftSize, and never the same twice", so the stage emits literal `0.0f` for exactly `fftSize` output samples first (`:643-660`). Two independent RNG streams, `kSmearSaltL = 3` / `kSmearSaltR = 4` (`:1549-1550`). **Not modified.** |
| `SpectralGate` (L2) | `processors/spectral_gate.h:89` | **The Layer-2 STFT-processor shape template**, and a near-name hazard. Shape copied: prepare-time FFT-size clamp + power-of-two snap (`:149-160`), `kMinFFTSize = 256` / `kMaxFFTSize = 4096` / `kDefaultFFTSize = 1024` (`:96-98`), member layout `STFT stft_; OverlapAdd overlapAdd_; SpectralBuffer inputSpectrum_, outputSpectrum_;` (`:727-730`), `getLatencySamples() :459`, `kSmoothingTimeMs = 50.0f` (`:129`). **Hazard:** it already has a member called `setSmearing(float) :442` / `getSmearing() :449` with `kMinSmearAmount`/`kMaxSmearAmount` (`:125-126`) — a *frequency-axis box average of gate gains*, `smearKernelSize_ = 1 + amount*(fftSize/64 − 1)` forced odd (`:703-711`), applied by `applySmearingOptimized() :606`. Different axis, different object, member names so no ODR question; called out so a reader does not assume duplication. Its geometry is **hop = fftSize/2** with **no** synthesis window (`:170-174`) — legal for it because it only scales magnitudes, and explicitly *not* legal here (see the `OverlapAdd` row). |
| `Xorshift32` / `deriveStreamSeed` (L0) | `core/random.h:41` / `:102` | Per-channel decoherence streams. `nextFloat()` is **bipolar `[-1,+1]`** (`:59-63`) — load-bearing for FR-040's `±amount·π` perturbation. `constexpr std::uint32_t deriveStreamSeed(std::uint32_t base, std::size_t salt) noexcept` (`:102-103`), lowbias32 finaliser with a guaranteed-non-zero result, load-bearing because `Xorshift32::seed()` silently substitutes its default for 0 (`:73-74`). |
| `OnePoleSmoother` (L1) | `primitives/smoother.h:134` | Control smoothing for amount / decoherence / tilt. `configure(float smoothTimeMs, float sampleRate) :160`, `setTarget :170`, `getCurrentValue :191`, `process() :197`, `advanceSamples(size_t) :243`, `isComplete() :232`, `snapTo(float) :263`. `advanceSamples` is O(1) but costs one `std::pow` per call unless the smoother has converged (`:244` early-return); **this component does not use it** — FR-035 configures each smoother on the *frame* clock and advances it with `process()`, three calls per frame. Both methods snap on completion (`process()` at `:199-201`, `advanceSamples` at `:251-253`), which is why an exactly-`0.0f` settled value is reachable and stable and the exact-identity gates in FR-021/FR-041 are reachable at all; `snapToTarget() :257` is the `prepare()`-time path. |
| `detail::isNaN` / `detail::isInf` / `detail::isFinite` (L0) | `core/db_utils.h:99` / `:260` / `:118` (float), `:125` (double) | The `-ffast-math`-proof finiteness tests (bit pattern behind an opaque barrier, `:250-259`). FR-008 forbids `std::isnan`/`std::isinf`/`std::isfinite`; `tools/lint-nonfinite-symbols.js` enforces it. |
| `Interpolation::cubicHermiteInterpolate` (L0) | `core/interpolation.h:84` | The interpolator for FR-042's coherence-make-up knots, identical to `AetherReverb`'s use at `:4029-4033`. |
| `kMaxAudioFreqHz` (L0) | `core/audio_constants.h:25` (`20000.0f`) | Upper anchor of the time-constant frequency law (FR-030). |
| `NoiseOrganism` / `ResonanceDriftNetwork` (L3) | `systems/noise_organism.h:150,178,180,277`; `systems/resonance_drift_network.h:135-136,144,393` | **Convention source only** — not included (Layer 3 is above this component). `kControlChunkSamples = 64`, `kGainRampMs = 50.0f`, `kOutputClamp = 4.0f`, a `clampEngagements_` counter, `getAllocatedBytes()`, and the "out-of-range slot returns the documented neutral" read-surface rule are the Vorago house style this component matches. |
| Perf-test idiom | `dsp/tests/unit/processors/vorago_p1_perf_test.cpp:1-90`; `dsp/tests/unit/systems/atmosphere_engine_perf_test.cpp:139-250, 437-640` | The measurement basis SC-013 inherits: **ns per 512-sample block at 48 kHz**, best-of-N after warm-up, gated against a checked-in baseline × 1.5, with `static_assert(kBaseline * kRegressionFactor <= kReferenceNs)` and `static_assert(kBaseline >= kReferenceNs / 50.0)` binding the absolute figure at compile time, tagged `[.perf]` so the per-push CI filter `~[performance]~[perf]~[benchmark]~[!benchmark]~[long]` excludes it. Also the source of the **measured** cost anchor in FR-060: Atmosphere's blur stage (stereo STFT↔OverlapAdd at fftSize 1024, 75 % overlap, full phase randomisation) costs **~23 000 ns per 512-sample block** (`atmosphere_engine_perf_test.cpp:376`), confirmed by the `(b) − (a)` difference of its own baselines (`:241-242`, 111 815 − 86 305 ≈ 25 500 ns). |
| Test helpers | `tests/test_helpers/` | `signal_metrics.h:326 calculateSpectralFlatness(const float*, size_t, float sampleRate)` for SC-001; `render_fingerprint.h:58 kSampleTolerance = 5.0e-4f`, `:61 kMetricTolerance = 2.5e-4`, `:122 compareFingerprints` for SC-009/SC-011; `allocation_detector.h:48 AllocationDetector` / `:111 AllocationScope` for SC-007; `artifact_detection.h:38 ClickDetectorConfig` / `:72 ClickDetection` for the parameter-jump arm of SC-012; `statistical_utils.h` for window statistics. |

## New components

ODR sweep run **this session**, verbatim, from the repo root:

```
$ grep -rn "class SpectralSmear" dsp/ plugins/      -> 0 hits (exit 1)
$ grep -rn "class SmearStage"    dsp/ plugins/      -> 0 hits (exit 1)
$ grep -rn "class BinSmear"      dsp/ plugins/      -> 0 hits (exit 1)
$ grep -rn "class SpectralFog"   dsp/ plugins/      -> 0 hits (exit 1)
$ grep -rn "SpectralSmear" dsp/ plugins/ tools/     -> 0 hits (exit 1)
$ ls dsp/include/krate/dsp/processors/spectral_smear.h -> No such file or directory
```

| Class / symbol | Layer | Header path | ODR sweep result |
|---|---|---|---|
| `SpectralSmear` | 2 | `dsp/include/krate/dsp/processors/spectral_smear.h` (new) | **0 hits**, as is the identifier `SpectralSmear` anywhere in `dsp/`, `plugins/` or `tools/`. Near-name symbols that **do** exist and are not shadowed: `SpectralGate` (`processors/spectral_gate.h:89`) and its members `setSmearing`/`getSmearing` (`:442`/`:449`) — members, not types; `SpectralTilt` (`processors/spectral_tilt.h:88`); `SpectralDistortion` (`processors/spectral_distortion.h:111`); `SpectralMorphFilter` (`processors/spectral_morph_filter.h:67`); `SpectralBuffer` (`primitives/spectral_buffer.h:45`); `SpectralTransientDetector` (`primitives/spectral_transient_detector.h`); `AetherReverb`'s `kSmearSaltL/R` and `smearRngL_/R_` (`effects/aether_reverb.h:1549-1550`) — private members of a Layer 4 class. None collide. |
| `SpectralSmear::PrepareConfig` (nested struct) | 2 | same header | Nested, following `AtmosphereEngine::PrepareConfig` (`atmosphere_engine.h:369`), `NoiseOrganism::PrepareConfig` (`noise_organism.h:190`) and `AetherReverb::PrepareConfig` (`aether_reverb.h:1577`). Three unrelated nested `PrepareConfig` structs already coexist, so nesting is the established, collision-free form. |

No new top-level free functions, no new enum types, no new enumerators on existing enums, no new
`ModSource` values. `tools/lint-odr.js` and `tools/lint-layers.js` must pass on the result (SC-015).

## Functional Requirements

### FR-001 series — Component contract and lifecycle

- **FR-001** — `SpectralSmear` is a Layer 2 class in `namespace Krate::DSP`, declared in
  `dsp/include/krate/dsp/processors/spectral_smear.h`, header-only. Its includes reach **down only**:
  `primitives/stft.h`, `primitives/spectral_buffer.h`, `primitives/smoother.h`, `core/random.h`,
  `core/db_utils.h`, `core/interpolation.h`, `core/audio_constants.h`, `core/window_functions.h`,
  plus `<algorithm> <array> <bit> <cmath> <cstddef> <cstdint> <vector>`. It includes **no** Layer 3 or
  Layer 4 header.
- **FR-002** — Construction is trivial and allocation-free. All allocation happens in
  `void prepare(double sampleRate, const PrepareConfig& config) noexcept`, whose fields are
  `std::size_t fftSize = 2048` (see FR-011) and `bool enabled = false` (see FR-019). `prepare()` is the
  only allocating method; every other public method is `noexcept` and allocation-free.
- **FR-003** — The render entry point is
  `void processBlock(float* left, float* right, std::size_t numSamples) noexcept`, in-place on both
  channels. It returns immediately (leaving the buffers untouched) if `!isPrepared()`, if the instance
  was prepared with `enabled = false` (FR-019), if either
  pointer is null, or if `numSamples == 0`. There is no mono entry point and no single-sample
  `process()`: an STFT stage has nothing useful to say about one sample, and `SpectralGate`'s
  single-sample adapter buffers (`spectral_gate.h:759-761`) are the cost of having tried.
- **FR-004** — `void reset() noexcept` clears every audio-domain state — `STFT`, `OverlapAdd` and
  `SpectralBuffer` per channel, the per-bin magnitude memories, the output FIFO and its cursors — and
  **re-arms the warm-up counter to `fftSize`** (FR-014). It does **not** change any control value, any
  derived coefficient table, or the seed. It does re-derive the two RNG streams from the stored seed
  (FR-044), so `reset()` makes the render reproducible from the top. **It also re-arms the per-channel
  priming flag** (FR-022): the next analysed frame after `reset()` primes the magnitude memory to that
  frame's magnitudes rather than integrating from zero (Clarifications Q1).
- **FR-005** — `void setSeed(std::uint32_t seed) noexcept` stores the seed and re-derives both
  decoherence streams through `deriveStreamSeed` (FR-044). It does not reset audio state.
- **FR-006** — All parameter setters are callable at any time from the control thread of the owner's
  choosing (the component is not internally synchronised; the owner calls setters and `processBlock`
  from the same thread, as every Vorago component does). No setter allocates, locks, throws, or
  performs I/O.
- **FR-007** — The component has an internal **control grid**: all control smoothers are advanced
  exactly once per analysis frame, by one frame, immediately **before** the frame reads them
  (`atmosphere_engine.h:2310-2311`). There is no per-sample control work anywhere in `processBlock`
  except the final output scalar, clamp and FIFO pop.
- **FR-008** — Finiteness is tested only with `Krate::DSP::detail::isNaN` / `isInf` / `isFinite`
  (`core/db_utils.h:99/:260/:118`). `std::isnan`, `std::isinf` and `std::isfinite` are forbidden
  anywhere in the component or its tests (`tools/lint-nonfinite-symbols.js`).
- **FR-009** — Every public setter clamps a non-finite argument to the parameter's documented default
  before clamping to range, i.e. a NaN write is inert rather than poisoning
  (`atmosphere_engine.h:911` is the transcribed shape: `std::clamp(isFinite(x) ? x : dflt, lo, hi)`).

### FR-010 series — STFT geometry, latency and the drain loop

- **FR-010** — **Geometry is fixed at prepare and is not runtime-variable.** `hopSize = fftSize / 4`
  (75 % overlap), analysis window `WindowType::Hann`, synthesis via `OverlapAdd::prepare(fftSize,
  hopSize, WindowType::Hann, 9.0f, /*applySynthesisWindow=*/true)`. This combination is **mandatory,
  not preferred**: `primitives/stft.h:224-227` requires the synthesis window for spectral-modification
  processors at ≥ 75 % overlap and forbids it at 50 %, where Hann² is not COLA. The 75 % relationship
  is asserted at runtime: `assert(hopSize_ * 4 == fftSize_)` (`atmosphere_engine.h:458-459` precedent).
- **FR-011** — `PrepareConfig::fftSize` is clamped to `[kMinFftSize, kMaxFftSize] = [512, 4096]` and
  then **snapped DOWN to a power of two** (`std::bit_floor`, `aether_reverb.h:1624` precedent).
  Default `2048`. The minimum is **512, not the 256 that `FFT`/`SpectralGate`/`AtmosphereEngine`
  allow**, and the reason is a requirement, not taste: the frequency-dependent time-constant law
  (FR-030) needs enough bins below 200 Hz to be expressible, and at 256 / 48 kHz a bin is 187.5 Hz
  wide — the whole "lows smear longer" clause would live in a single bin. SC-004 (e) measures the law
  at `kMinFftSize` precisely to keep this honest.
- **FR-012** — `processBlock` consumes the caller's block in internal chunks of at most
  `kProcessChunkSamples = 64` (`atmosphere_engine.h:271` convention). This bounds `STFT`'s
  `samplesAvailable_` to `fftSize + 64` at all times, which is what makes the unguarded
  `pushSamples` ring (`stft.h:78`, `fftSize * 8`) structurally safe rather than merely large enough.
  The component therefore accepts **any** `numSamples` with no `maxBlockSamples` field and no
  caller-side contract.
- **FR-013** — Inside one chunk the order is, verbatim in shape from `atmosphere_engine.h:2273-2298`:
  push both channels → `while (stft_[0].canAnalyze())` { advance the three control smoothers by one
  frame **once**; for `ch` in `{0, 1}` in that order: `analyze` → per-bin magnitude smear (FR-020)
  → per-bin phase decoherence (FR-040) → `synthesize` → `pullSamples(scratch, hopSize)` → write
  `hopSize` samples into the channel FIFO; advance the shared FIFO cursors } → pop exactly
  `chunkSamples` from the FIFO into the caller's buffers. **Three invariants ride on this order and
  each fails silently if disturbed:** (a) frame-major/channel-minor, so both channels share one control
  value and the smoothers advance once per hop of audio and not twice (asserted by **SC-017** through
  FR-053's applied reads — nothing else can see it); (b) the pull is inside the
  drain loop and is always exactly `hopSize`, never the chunk size — `OverlapAdd::synthesize`
  accumulates at offset 0 unconditionally (`stft.h:300-308`) and `pullSamples` returns silently on an
  oversized request (`stft.h:339-342`); (c) L is always processed before R, which is part of the
  determinism contract even though FR-044 gives the channels independent streams (it fixes the order
  of *any* future shared state).
- **FR-014** — **Latency is exactly `fftSize` samples for every block partition, and it is produced by
  a counter, not by an emptiness test.** After `prepare()` and after `reset()`,
  `warmupRemaining_ = fftSize`; while it is non-zero the FIFO pop emits literal `0.0f` and decrements.
  Draining "as soon as something is available" instead yields a partition-dependent offset of
  `(ceil(fftSize/chunk) − 1) * chunk` — never `fftSize`, and never the same twice
  (`aether_reverb.h:643-660`, which measured 960 / 1020 / 1022 samples for chunk sizes 64 / 30 / 7 at
  fftSize 1024). SC-002 and SC-011 assert this directly.
- **FR-015** — `[[nodiscard]] std::size_t getLatencySamples() const noexcept` returns `fftSize_` when
  prepared **and enabled**, and `0` otherwise (unprepared, or prepared with `enabled = false`). It is
  **constant for a prepared instance**: no control value, including `smearAmount = 0` and
  `decoherence = 0`, changes it. The only switch that changes it is the prepare-time `enabled` flag
  (FR-019), which cannot move mid-render — exactly the shape `AtmosphereEngine::getLatencySamples()`
  ships (`blurEnabled_ ? blurFftSize_ : 0`, `atmosphere_engine.h:1129`).
- **FR-016** — The output FIFO is one `std::vector<float>` per channel of capacity
  `std::bit_ceil(fftSize + kProcessChunkSamples + hopSize)` with a shared power-of-two mask and shared
  read/write/count cursors (`atmosphere_engine.h:462-464` precedent). Sized at `prepare()`, never
  resized.
- **FR-017** — `[[nodiscard]] std::size_t getAllocatedBytes() const noexcept` reports the exact heap
  footprint **of the component's own vectors** after `prepare()`, reproducing every `resize`/`assign`
  in `prepare()` (`noise_organism.h:94` precedent). **Scope is normative, because the two readings of
  "the footprint" differ by a factor of five:** the figure covers the three pole tables (FR-034), the
  per-frame tilt-resolved scratch pole table, the two per-channel magnitude memories (FR-022), the two
  output FIFOs (FR-016) and the hop scratch buffers, and it **excludes** the heap the sub-objects own
  (`STFT::inputBuffer_` is `fftSize * 8` floats per channel alone, `stft.h:78`, plus
  `windowedFrame_`/`window_` (`stft.h:189-191`); `OverlapAdd::outputBuffer_` is `fftSize * 2`
  (`stft.h:265`) plus `ifftBuffer_` and `synthesisWindow_` (`stft.h:372-374`);
  `SpectralBuffer`'s `data_`/`mags_`/`phases_`,
  `spectral_buffer.h:61-66`, `:206-210`; and pffft's own setup inside `FFT`). SC-008 asserts the owned
  figure and *reports* the sub-object figure. It is the assertion surface for SC-008.
- **FR-018** — Query surface, all `noexcept` and `[[nodiscard]]`: `isPrepared()`, `isEnabled()`,
  `getFftSize()`, `getHopSize()`, `getNumBins()`, `getSampleRate()`, plus the applied-value reads in
  FR-053 and the public make-up statics in FR-042. Reads on
  an unprepared instance return `0` / the documented neutral, never undefined values
  (`noise_organism.h:855-859` rule). On a **prepared-but-disabled** instance (FR-019), the geometry
  reads — `getFftSize()`, `getHopSize()`, `getNumBins()`, `getSampleRate()` — return the same
  clamped/snapped values an identically configured **enabled** instance would report; only
  `getLatencySamples()` and `getAllocatedBytes()` collapse to `0` with `enabled` (Clarifications Q6).
- **FR-019** — **Prepare-time enable, so a fog-less patch pays neither the cost nor the latency.**
  `PrepareConfig::enabled` defaults to **`false`** (Clarifications Q6 — fog is opt-in, matching
  FR-052's "nothing turns itself on" applied to latency as well as to the effect). When it is `false`,
  `prepare()` allocates nothing
  (`getAllocatedBytes() == 0`), `isPrepared()` is still `true`, `isEnabled()` is `false`,
  `getLatencySamples()` returns `0` (FR-015), and `processBlock` leaves both buffers **bit-identical**
  — it is a true bypass, not a zero-parameter round trip. **A disabled instance still answers geometry
  queries**: `getFftSize()`, `getHopSize()`, `getNumBins()` return the clamped/snapped values for the
  requested `fftSize`, and `getSampleRate()` returns the prepared rate, exactly as an enabled twin would
  (FR-018) — so Phase 10 can lay out its bus and decide its host-latency report before enabling
  anything. FR-053's applied reads on a disabled instance return the corresponding **target** reads
  directly (no frame ever runs to smooth them, so there is nothing to mirror but the target). The flag
  is **not** runtime-settable: moving
  latency mid-render is a click plus a host renegotiation (`aether_reverb.h:614-616`). Both shipped
  prior-art stages provide exactly this switch — `AtmosphereEngine` (`blurEnabled_`, latency at
  `atmosphere_engine.h:1129`) and `AetherReverb` (`spectralEnabled_ = config.spectralDiffusionEnabled`,
  `aether_reverb.h:1626`) — and without it a Vorago patch with `smearAmount = 0` and `decoherence = 0`
  would still pay the full stereo STFT round trip (SC-013 (a)) **and** 2048 samples = 42.7 ms of
  unconditional latency on the **global** bus for a stage doing nothing. Phase 10 owns the decision of
  what to pass here; this phase owns making the choice available. **This is a deliberate divergence
  from `AtmosphereEngine::PrepareConfig`'s `blurEnabled_ = true` default (`atmosphere_engine.h:371`) —
  the header's `PrepareConfig::enabled` declaration must carry a comment recording it, with the reason
  that Atmosphere's blur lives inside a voice layer the owner already chose, while this stage sits on
  the global bus.** Derivation in D-11.

### FR-020 series — Per-bin magnitude smear (roadmap line 250)

- **FR-020** — For each channel, each frame, each bin `k` in `[0, numBins)`, the component maintains a
  **normalised one-pole leaky integrator** on magnitude:
  `state[k] = (1 − p(k)) * mag[k] + p(k) * state[k]`, with `p(k) = poleTable(k)` (FR-034).
  **The pole depends on frequency and tilt only — never on `smearAmount`** (FR-021 explains why).
  Normalisation by `(1 − p)` is a requirement, not a detail: it makes the steady-state gain exactly
  unity at every `p`, which is what lets FR-021's identity claim and SC-003's null test coexist with a
  smear knob that changes nothing about the long-term average spectrum (a Non-Goal above).
- **FR-021** — **`smearAmount` is a magnitude-domain blend, not a pole scale; zero is an
  exact-identity gate.** The magnitude written is
  `setMagnitude(k, mag[k] + amount * (state[k] − mag[k]))`, where `amount` is the settled smoothed
  smear amount in `[0, 1]` and `state[k]` is FR-020's integrator, which runs **unconditionally** at
  the full `poleTable(k)`. At `amount == 0.0f` exactly the blend degenerates to `mag[k]` and the
  component **must skip the magnitude write entirely** (leaving the analysed spectrum untouched) while
  still updating `state[k]` so the memory is never stale. This is an *exact-identity* gate on a settled
  value reachable because `OnePoleSmoother::process()` snaps on completion — `if (std::abs(current_ −
  target_) < kCompletionThreshold) { current_ = target_; }`, `primitives/smoother.h:197-201`, which is
  the method FR-035 mandates (**not** `advanceSamples`, which FR-035 forbids here) — never a threshold:
  any non-zero amount, however small, takes the full path (`atmosphere_engine.h:2332-2342` states the
  same rule for its own gate).
  **Why the blend and not `p(k) = amount * poleTable(k)`:** pole scaling compresses the whole useful
  travel of the knob into the last fraction of a percent below 1.0, which would make a control the
  roadmap names as a *modulation target* (line 255, "fog rolls in via `TidalModulator`") do nothing
  over 99 % of a modulator's sweep. The arithmetic is in D-12. The blend is also **cheaper** (one
  multiply-add per bin instead of one multiply plus the per-bin pole product) and preserves every other
  claim: it is a convex combination of two unity-steady-state-gain quantities, so FR-020's unity gain
  and FR-063's boundedness hold unchanged, and at `amount == 1` the output magnitude **is** the
  integrator, which is what makes SC-004 (c)'s analytic decay measurement mean what it says.
- **FR-022** — The magnitude state arrays are `std::vector<float>` of `numBins` per channel, allocated
  in `prepare()`, zero-filled in `prepare()` and `reset()`. **The memory is primed, not integrated from
  zero (Clarifications Q1).** A per-channel priming flag is set whenever the state is zero-filled
  (`prepare()`, `reset()`) or cleared by a poison event (FR-062 (a)); the **next analysed frame** after
  the flag is set writes `state[k] = mag[k]` for every bin directly, clears the flag, and every
  subsequent frame integrates normally (FR-020). There is no start-up fade on the global bus, and
  FR-020's unity steady-state gain and FR-063's boundedness are unaffected — priming only changes the
  memory's first value, not the law that updates it.
- **FR-023** — `poleTable(k)` is clamped to `[0.0f, kMaxPole]` with `kMaxPole = 0.99999f`.
  **The binding bound on the hold time is `kMaxSmearSeconds = 10 s` (FR-031), not this clamp**, and the
  spec says so rather than claiming a guard it does not need: the pole is
  `exp(−hopSize / (sampleRate * tau))`, so reaching `kMaxPole` would need
  `hopSize / (sampleRate * tau) ≤ 1.0e−5`, whereas the largest reachable hop is `kMaxFftSize / 4 = 1024`
  and the largest `tau` is 10 s, giving `1024 / (44100 * 10) = 2.32e−3` (pole 0.99768) at 44.1 kHz and
  `1024 / (192000 * 10) = 5.33e−4` (pole 0.99947) even at 192 kHz — three orders of magnitude below the
  clamp. `kMaxPole` is therefore a **defensive backstop** against a future range change or a degenerate
  derived `tau`, asserted white-box on `poleTable()` at synthetic extremes rather than through a render.
  What SC-005 (v) actually asserts is the `kMaxSmearSeconds` bound: that the tail decays at the rate the
  configured `tau` implies, so the component can never become the second undocumented freeze the
  Non-Goals exclude.
- **FR-024** — **Denormal safety.** Magnitude states decay geometrically toward zero and are the one
  place in this component where denormals are reachable at long time constants. The component relies
  on the process-wide FTZ/DAZ the repo enables (`tests/test_helpers/enable_ftz_daz.h`, applied in
  `dsp/tests/dsp_test_main.cpp`) **and additionally** flushes any state below
  `kDenormalFloor = 1.0e-20f` to `0.0f` inside the same per-bin loop, so the component is correct on a
  host that has not set the MXCSR bits. The flush is an ordered comparison, not a bit test, and is
  therefore `-ffast-math`-proof.
- **FR-025** — DC (bin 0) and Nyquist (bin `numBins − 1`) **do** take the magnitude smear. Unlike
  phase (FR-040), their magnitude is a free real quantity, and excluding them would leave two
  unsmeared spikes in the fog.

### FR-030 series — Frequency-dependent time constants and tilt (roadmap lines 250, 255)

- **FR-030** — The per-bin time constant follows a **log-frequency interpolation between two endpoint
  values**: with `fLow = 20.0f` Hz and `fHigh = kMaxAudioFreqHz = 20000.0f`
  (`core/audio_constants.h:25`), and `u(k) = clamp(log(f_k / fLow) / log(fHigh / fLow), 0, 1)` where
  `f_k = k * sampleRate / fftSize` (bin 0 uses `u = 0`),
  `tau(k) = tauLow * pow(tauHigh / tauLow, u(k))` seconds.
  `tauLow > tauHigh` by default, which **is** the roadmap's "lows smear longer" (line 250).
- **FR-031** — The endpoints are settable: `void setSmearTimeLow(float seconds) noexcept` and
  `void setSmearTimeHigh(float seconds) noexcept`, both clamped to
  `[kMinSmearSeconds, kMaxSmearSeconds] = [0.02f, 10.0f]`. Defaults `tauLow = 3.0f`,
  `tauHigh = 0.25f`. They are **not** modulation targets (FR-033) — they are patch-level shape
  controls; changing one rebuilds the pole tables (FR-034, FR-036).
- **FR-032** — **Tilt** rotates the law about a fixed pivot. `void setSmearTilt(float tilt) noexcept`,
  clamped to `[-1, +1]`, default `0`. `tilt = 0` is exactly the FR-030 law. Positive tilt lengthens
  low-bin time constants and shortens high-bin ones (more low-biased fog); negative tilt flattens and
  then inverts the law. The law is
  `tauTilted(k) = tau(k) * pow(fPivot / max(f_k, fLow), tilt * kTiltExponentRange)` with
  `fPivot = 1000.0f` Hz and `kTiltExponentRange = 0.75f`, so `tilt = 0` is the identity — the same
  "identity at zero, bounded exponent range" shape as `HarmonicCloud::setSpectralGravity`
  (`systems/harmonic_cloud.h:478`, law `pow(n, 1 + g*kGravityExponentRange)` with range `0.1f` at
  `:198`). `tauTilted` is re-clamped to `[kMinSmearSeconds, kMaxSmearSeconds]` per bin. **The law
  rotates about the pivot until the per-bin clamp engages, and that saturation is normative, not a bug
  (Clarifications Q5):** at the shipped defaults (`tauLow = 3.0`, `tauHigh = 0.25`), `tilt = +1` gives
  `tau(20 Hz) = 3.0 * 50^0.75 = 56.4 s`, so every bin below ~95 Hz sits on the
  `kMaxSmearSeconds = 10 s` clamp — the same bound SC-005 (v) relies on. `kTiltExponentRange` is not
  shrunk to avoid the clamp: doing so would cost tilt its audible travel and would still clamp at
  non-default `tauLow` values.
- **FR-033** — **`smearAmount`, `decoherence` and `smearTilt` are the three modulation targets**
  (roadmap line 255). Each is a plain `void set*(float) noexcept` scalar clamped to its range; the
  component owns no `TidalModulator`, no `BrownianDrift` and no scheduler. The owner (Phase 10) writes
  a modulator's value into the setter — the `NoiseOrganism::setSourceWake` (`noise_organism.h:844`)
  and `ResonanceDriftNetwork::setPeakWake` precedent.
- **FR-034** — **Tilt must be cheap enough to modulate continuously, so it is applied by table
  interpolation, not by per-bin `pow` at run time.** `prepare()` (and any change to `tauLow` or
  `tauHigh`) builds **three** pole tables of `numBins` floats each — `poleNeg[k]`, `poleZero[k]`,
  `polePos[k]` — evaluated at `tilt = −1, 0, +1` via
  `pole = exp(−hopSize / (sampleRate * tauTilted(k)))`, each clamped per FR-023. At run time
  `poleTable(k) = (t <= 0) ? lerp(poleNeg[k], poleZero[k], t + 1) : lerp(poleZero[k], polePos[k], t)`
  for the smoothed tilt `t`, i.e. two multiplies and an add per bin per frame, no transcendental.
  The blend is evaluated **once per frame into a fourth, tilt-resolved scratch table** and both channels
  read it — the control value is shared by construction (FR-013 invariant (a)), so evaluating it per
  channel would be pure duplication. That scratch table is sized at `prepare()` like the other three
  and is part of FR-017's reported footprint.
  **The interpolated form IS the specified law** — it is not an approximation of some other law that a
  test could find a discrepancy against. It is monotone in `t` bin-by-bin (a linear blend of two
  ordered endpoints) and stays inside `[0, kMaxPole]` by construction.
- **FR-035** — `smearAmount`, `decoherence` and `smearTilt` are each smoothed by an `OnePoleSmoother`
  configured at `kControlSmoothMs = 50.0f` (`atmosphere_engine.h:282`) **on the frame clock**, i.e.
  `configure(kControlSmoothMs, frameRate)` with `frameRate = sampleRate / hopSize`
  (`spectral_gate.h:166`, `:184-187` precedent), advanced once per frame by `process()` — not by
  `advanceSamples(hopSize)`, because the smoother's own clock is already the frame clock. Each
  smoother is `snapTo`'d in `prepare()` and left alone by `reset()`.
- **FR-036** — The pole tables are rebuilt **only** on `prepare()` and on `setSmearTimeLow` /
  `setSmearTimeHigh` (each costing `3 * numBins` `exp` calls, and documented as control-thread cadence,
  not per-block). Tilt, amount and decoherence never rebuild anything.

### FR-040 series — Phase decoherence (roadmap line 250)

- **FR-040** — For each channel, each frame, for `k` in `[1, numBins − 1)`:
  `setPhase(k, getPhase(k) + d * kPi * rng[ch].nextFloat())` where `d` is the settled smoothed
  decoherence in `[0, 1]` and `nextFloat()` is bipolar (`core/random.h:59-63`), so the perturbation is
  uniform on `±d·π`: `0` is the identity and `1` is full decoherence. **DC and Nyquist are excluded**
  because their phase is not free in a real spectrum (`atmosphere_engine.h:2325-2328`).
- **FR-041** — **Exact-identity gate**, the twin of FR-021: at a settled `d == 0.0f` exactly, the
  per-bin phase write is skipped entirely, leaving the spectrum untouched, which is *more* exactly
  transparent than adding zero (adding zero still forces a polar round trip and re-rounds the
  spectrum). Any non-zero `d` takes the full path.
- **FR-042** — **Coherence make-up.** Independently randomised per-frame phases sum incoherently, so
  `OverlapAdd`'s COLA factor — computed once at prepare (`stft.h:249-262`) and applied unconditionally
  (`:300-308`) — is wrong by a level that grows with `d` (about 6 dB at `d = 1`;
  `aether_reverb.h:623-626`). The component applies a make-up gain `g(d)` **as a scalar on the pulled
  time-domain samples, never on the bins** (ramped across the hop rather than stepped at its boundary —
  FR-046), so FR-020's unity-gain claim holds literally. `g` is a
  five-knot table at `d = 0, 0.25, 0.5, 0.75, 1.0` interpolated with
  `Interpolation::cubicHermiteInterpolate` (`core/interpolation.h:84`), end tangents clamped, exactly
  as `AetherReverb::coherenceMakeup` (`:4019-4033`).
  **The knots are measured for this component, not assumed.** `AetherReverb`'s shipped values
  `{1.0000, 1.0799, 1.3435, 1.7746, 1.9996}` (`:2775`) are the *expected* answer because our geometry
  is identical (Hann, 75 %, synthesis window on) and the incoherent/coherent ratio
  `sqrt(sum w^4)/sum w^2` depends on the window and the overlap count, not on fftSize
  (`:640-643`). SC-006 measures our own; if any knot differs from the shipped value by more than 2 %,
  our measured table ships and the discrepancy is written into the header with its cause.
  **The table and the interpolant are public**, following `AetherReverb`'s shape
  (`static constexpr float kCoherenceMakeup[kCoherenceKnotCount]`, `aether_reverb.h:2774-2776`;
  `[[nodiscard]] static float coherenceMakeup(float) noexcept`, `:4019-4033`):
  `static constexpr std::size_t kCoherenceKnotCount = 5`,
  `static constexpr float kCoherenceMakeup[kCoherenceKnotCount]` and
  `[[nodiscard]] static float coherenceMakeup(float d) noexcept` are members of `SpectralSmear`. This
  is not convenience: SC-006 has to divide the make-up back out to measure the raw incoherent loss, and
  nothing else on the query surface (FR-018, FR-053) exposes `g(d)` — without these statics the
  criterion is not executable and its knot arm is circular.
- **FR-043** — At a settled `d == 0.0f`, FR-041's skip **still burns the same per-bin RNG draws** so
  each stream's position is identical to the full path's: **the stream position depends only on the
  number of frames elapsed, never on whether the gate engaged**
  (`atmosphere_engine.h:2332-2342` states the same rule, burning one draw per bin per channel per frame
  regardless of the gate). The observable consequence is *not* that a post-park segment is independent
  of the park's length — it is that at any **absolute** sample index the streams are where the frame
  count puts them, which is what SC-009 (c) measures over a fixed absolute window.
- **FR-044** — **Two independent streams**, `rngL_` / `rngR_`, seeded
  `deriveStreamSeed(seed_, kSaltDecohereL = 0x1000)` and
  `deriveStreamSeed(seed_, kSaltDecohereR = 0x2000)` (`core/random.h:102`), following
  `AetherReverb`'s `kSmearSaltL`/`kSmearSaltR` (`:1549-1550`) rather than `AtmosphereEngine`'s single
  shared stream. Reason: with one stream the two channels' perturbations are coupled by stream
  position, so the statistical independence of L and R depends on `numBins` parity and on channel
  order; with two streams they are i.i.d. by construction and a per-channel measurement (SC-001's
  stereo arm) means what it says. `setSeed` and `reset` both re-derive; nothing else touches them.
- **FR-045** — *(code-review-only requirement — no criterion can discharge it, and that is stated
  rather than papered over.)* Decoherence is applied **after** the magnitude write in the same frame, so
  the magnitude memory always integrates the *analysed* magnitudes and never a value the component
  itself perturbed. Since FR-040 writes only phase, swapping the two orderings is **bit-identical
  today**, so no render can distinguish them; it is specified anyway so that any future
  magnitude-domain addition cannot silently create a feedback path. **Named enforcement:** the
  compliance pass verifies it by inspection, and the header must carry a comment at the ordering site
  stating the ordering and this reason. Its Traceability row says "by inspection", not a criterion.
- **FR-046** — **The make-up gain is ramped across the hop, not stepped at the hop boundary.** `g(d)`
  is evaluated once per frame from the settled smoothed `d`, and the `hopSize` pulled samples are
  multiplied by a per-sample linear interpolation from the **previous** frame's `g` to the current
  frame's `g` (`prevMakeup_` is stored per instance, initialised to `coherenceMakeup(d)` at `prepare()`
  and re-initialised on `reset()`). This is a **deliberate deviation from `AetherReverb`**, which
  multiplies one constant `g` over the whole hop (`const float g = coherenceMakeup(amount); for (i <
  hop) dstL[i] *= g;`, `aether_reverb.h:4097-4102`). The deviation is required because `decoherence` is
  a modulation target here (FR-033) and FR-035's 50 ms smoothing runs on the **frame** clock (93.75 Hz
  at the reference geometry, ~4.7 frames), so a `0 → 1` step moves `d` to ~0.62 after one frame and `g`
  from 1.000 to ~1.50 at a single sample boundary — a ~50 % instantaneous amplitude step, ~11.6 dB
  above the peak inter-sample delta of a 1 kHz tone at 48 kHz (`A·2π·1000/48000 = 0.131·A`) and a
  textbook 5-sigma derivative outlier for `ClickDetector` (`tests/test_helpers/artifact_detection.h:99`
  documents the sigma rule). SC-012 (c) is the enforcing measurement.

### FR-050 series — Control surface

- **FR-050** — Control setters, all `noexcept`, all clamped per FR-009:
  `setSmearAmount(float) [0,1] default 0`, `setDecoherence(float) [0,1] default 0`,
  `setSmearTilt(float) [-1,+1] default 0`, `setSmearTimeLow(float) [0.02,10] s default 3.0`,
  `setSmearTimeHigh(float) [0.02,10] s default 0.25`, `setSeed(std::uint32_t)`.
- **FR-051** — Matching target reads: `getSmearAmount()`, `getDecoherence()`, `getSmearTilt()`,
  `getSmearTimeLow()`, `getSmearTimeHigh()`, `getSeed()`. These return what was **set**.
- **FR-052** — **Shipped defaults are transparent.** `smearAmount = 0` and `decoherence = 0` means a
  prepared-and-**enabled** instance is a latency-only pass-through (SC-003). **`PrepareConfig::enabled`
  also defaults to `false` (FR-019, Clarifications Q6), so an instance prepared with a default
  `PrepareConfig` is a true bypass — zero latency, zero footprint — until Phase 10 opts in.** A Vorago
  patch that wants fog turns it on; nothing turns itself on.
- **FR-053** — **Applied reads, distinct from target reads**: `getAppliedSmearAmount()`,
  `getAppliedDecoherence()`, `getAppliedSmearTilt()` return the *smoothed* values the last frame
  actually used — the surface that lets a test assert the smoother's cadence rather than infer it
  (`atmosphere_engine.h:1151-1156` precedent). Without them, FR-013's "advance once per frame-pair"
  invariant is unobservable and no criterion sweeping settled values can catch a regression in it —
  **SC-017 is the criterion that reads them**, and it is what closes that silent-failure mode. (A
  double advance is invisible to every other criterion: it merely halves the smoothing time, which is
  still click-free and still costs the same.)
- **FR-054** — `[[nodiscard]] std::size_t getClampEngagements() const noexcept` and
  `[[nodiscard]] std::size_t getPoisonEngagements() const noexcept` expose the two safety counters
  (FR-061, FR-062), zeroed by `prepare()` and `reset()` only (`noise_organism.h:277` precedent).

### FR-060 series — Safety, budget, footprint

- **FR-060** — **CPU budget is a functional requirement** (roadmap lines 259, 497): the whole
  component, stereo, at the default geometry and worst-case control settings, costs **≤ 0.5 % of one
  core at 48 kHz**, i.e. ≤ 53 333 ns per 512-sample block (`512 / 48000 * 1e9 = 10 666 667 ns`).
  **The number this phase is actually built against is tighter, and the two must not be confused.**
  SC-013 inherits the Phase-1 gate idiom, whose `static_assert(kBaseline * kRegressionFactor <=
  kReferenceNs)` (`vorago_p1_perf_test.cpp:109-110`) with `kRegressionFactor = 1.5` caps any
  checked-in baseline at
  **`kReferenceNs / kRegressionFactor = 53 333 / 1.5 = 35 555 ns/block = 0.333 % of one core`** — the
  same "effective ceiling" figure Phase 1 writes into its own baseline comment
  (`vorago_p1_perf_test.cpp:92-93`, "kReferenceNsPerBlock / kRegressionFactor = 7111.1 ns/block").
  **35 555 ns is the binding figure for this phase and the figure the compliance row is measured
  against**; the roadmap's 0.5 % is the outer bound that the 1.5× regression headroom must fit inside.
  A measurement in the band **[35 556, 53 333] ns (0.334–0.5 %)** is therefore *not* shippable even
  though it satisfies the roadmap sentence: no baseline can be checked in for it, so it triggers the
  lever ladder below and then the stop-and-surface rule, exactly as an over-budget measurement does.
  There is no case in which SC-013's assert and FR-060's budget disagree about a verdict.
  The projection this spec is written against is **measured, not assumed**: Atmosphere's structurally
  identical stereo blur stage (STFT↔OverlapAdd, fftSize 1024, 75 % overlap, full per-bin phase
  randomisation) costs **~23 000–25 500 ns per 512-block** on the reference machine
  (`atmosphere_engine_perf_test.cpp:376`, and the `(b) − (a)` baseline difference at `:241-242`),
  i.e. ~0.22–0.24 %. This component adds one multiply-add for the integrator, one multiply-add for
  FR-021's blend, one compare (FR-024) and one shared per-frame table lerp per bin, plus
  the magnitude half of a polar round trip `SpectralBuffer` already pays for (`spectral_buffer.h:7-12`),
  which projects to **~0.25–0.30 %, i.e. 26 667–32 000 ns/block — under the 35 555 ns effective
  ceiling with 10–33 % of margin**. (The earlier "~0.35 %" reading of this projection was arithmetically
  unshippable: 37 333 ns × 1.5 = 56 000 ns exceeds `kReferenceNs`, so no baseline could have been
  checked in for it.) **The cost is
  approximately geometry-independent**: at fixed 75 % overlap the per-block frame count is `2048/N`
  and the per-frame cost is `O(N log N)` plus `O(N)`, so the products go as `2048·log N` and `1024`
  respectively — the same reasoning Atmosphere recorded when it declined to drop its blur FFT size as
  a cost measure (`atmosphere_engine_perf_test.cpp:327-332`).
  **If the measured figure exceeds 35 555 ns/block, the response is to reduce cost, never to raise the
  baseline and never to relax the budget.** The pre-approved levers, in order: (i) skip the per-bin
  magnitude write under FR-021's gate and the per-bin phase write under FR-041's (both already
  specified); (ii) fuse the integrator and FR-021's blend into one pass over the bins reading the
  shared per-frame tilt-resolved pole table (FR-034), so the per-bin work is two FMAs and a compare;
  (iii) raise the default `fftSize` (cheaper per block at 75 % overlap
  by the `log N` term). If none suffices, **stop and surface to the user** with the measurement — do
  not ship a quietly relaxed number.
- **FR-061** — **Output clamp.** After the make-up gain and before the FIFO write, every sample is
  clamped to `±kOutputClamp = 4.0f` (`noise_organism.h:180`, `resonance_drift_network.h:144`), with
  `clampEngagements_` incremented once per engaging **sample**. The clamp is an ordered comparison
  (`std::clamp`), not a bit test, so it survives `-ffast-math`.
- **FR-062** — **Non-finite handling, and a deliberate deviation from `AtmosphereEngine`.** Once per
  frame per channel the component accumulates the frame's magnitudes into one scalar and tests it with
  `detail::isFinite` — one call per frame, not per bin (`atmosphere_engine.h:2250-2260` cost rule).
  If the accumulator is non-finite the component (a) zeroes that channel's magnitude memory **and
  re-arms that channel's priming flag** (FR-022), so the next analysed frame primes the memory from its
  own magnitudes rather than recovering from zero, (b)
  zeroes the frame's spectrum so the frame synthesises silence, (c) increments `poisonEngagements_`,
  and (d) **continues** — it does not latch.
  `AtmosphereEngine` latches on the same condition (`:2252-2260`); this component must not, because it
  sits on the **global** bus post-voice-sum (roadmap line 78), where latching converts one poisoned
  frame into a dead instrument that only `reset()` can revive. The magnitude memory is the only state
  that can carry poison forward, and (a) clears **and re-primes** it, so recovery is automatic within
  one frame after the poisoned one, not merely within `fftSize` samples (Clarifications Q1; SC-016 (iii)
  is the enforcing measurement).
- **FR-063** — **Boundedness.** With finite bounded input, output is bounded for **every** reachable
  combination of `smearAmount`, `decoherence`, `smearTilt`, `tauLow`, `tauHigh` and `seed`. The
  argument is structural, not empirical: the integrator is a convex combination (FR-020) with pole
  strictly below 1 (FR-023), so its state is bounded by the input's magnitude supremum; phase
  perturbation does not change magnitude; the make-up gain is bounded by `max(kCoherenceMakeup) ≈ 2.0`
  (FR-042); and the clamp is an unconditional backstop (FR-061). SC-005 measures it anyway.
- **FR-064** — **Zero allocation after `prepare()`.** `processBlock` and every setter — including
  `setSmearTimeLow`/`setSmearTimeHigh`, which overwrite three already-sized tables rather than
  resizing them — perform no heap traffic. Asserted by `AllocationScope` (SC-007).
- **FR-065** — **Sample-rate correctness.** `tau` is specified in **seconds**, so the pole
  `exp(−hopSize / (sampleRate * tau))` must be re-derived at every `prepare()`. A render at 44.1, 48
  and 96 kHz must exhibit the same magnitude-memory decay time in seconds (SC-010).

### FR-070 series — Shared components stay untouched

- **FR-070** — **`AtmosphereEngine` and `AetherReverb` are not refactored to consume `SpectralSmear`,
  and this discharges the roadmap's conditional at line 253 with evidence.** Neither is a drop-in, for
  reasons read this session. Reasons 1–3 each independently carry the conclusion; reason 4 is a
  clarification of what the layer rule does *not* say, not a fourth independent reason:
  1. **Wrong operation.** Both are phase-only and say so normatively — `atmosphere_engine.h:2316`
     ("MAGNITUDE IS NEVER WRITTEN … the stage is a decoherer and not a filter"),
     `aether_reverb.h:620-621` ("MAGNITUDES ARE NEVER TOUCHED (FR-061)"). This component's defining
     feature is the magnitude integrator. Substituting it either changes their output or requires
     `smearAmount = 0` forever, in which case nothing was shared but a `while` loop.
  2. **Incompatible RNG contracts.** Atmosphere draws **per bin per channel from one stream**, which
     its own FR-060/N-9 documents as intended progressive stereo decorrelation; AetherReverb draws from
     **two** (`kSmearSaltL`/`kSmearSaltR`, `:1549-1550`). This component specifies two (FR-044). One
     shared implementation cannot satisfy all three, and each engine's stream position is pinned by its
     own determinism criterion (Atmosphere's SC-010, cited at `:2338-2341`).
  3. **Different plumbing.** Atmosphere's blur writes into a FIFO whose cursors are shared with the
     freeze leg's delay-matching (`:494-499`, `:563-590`) and whose latency is reported jointly
     (`:1129`, `:1141-1142`); AetherReverb's stage is prepare-time-switched with a warm-up counter tied
     to its own SC-011/SC-018 partition invariants (`:649-660`, `:1866`). Extracting either would
     rewrite load-bearing lifecycle code in a shipped 1.0 engine for no behavioural gain.
  4. **Direction, not layer.** `SpectralSmear` (L2) cannot reach up into either engine, so any
     unification would have to be **the engines consuming it** — and reasons 1–3 rule that out on
     behaviour, RNG contract and lifecycle grounds. To be precise about what the layer rule does and
     does not forbid: `tools/lint-layers.js:74` fails an include only when
     `layerIndex(toLayer) > layerIndex(fromLayer)`, so a Layer 3 or Layer 4 engine including a Layer 2
     header is **legal and routine** — `AtmosphereEngine` already includes
     `processors/grain_scheduler.h`, `processors/grain_span_simd.h` and
     `processors/spectral_freeze_oscillator.h` (`atmosphere_engine.h:153-155`). Layer is therefore no
     obstacle in the consumption direction at all; only the (irrelevant) reverse direction is blocked.
  The roadmap's instruction in that case is explicit: *"otherwise leave Atmosphere untouched — no
  speculative unification"* (lines 253–254). SC-014 asserts both headers are byte-unchanged.
- **FR-071** — No shipped component is amended by this phase at all. `git diff --stat` at the end of
  the phase must show changes only in: the new header, the four new test TUs, `dsp/tests/CMakeLists.txt`,
  `dsp/lint_all_headers.cpp` (the compile-all-headers TU, if it enumerates), and this spec directory.
- **FR-072** — Consumer suites that must stay green because they share the primitives this component
  uses (`STFT`, `OverlapAdd`, `SpectralBuffer`): `dsp_primitives_tests`, `dsp_processors_tests`,
  `dsp_systems_tests`, `dsp_effects_tests`, `seraphis_tests`, `innexus_tests` (SC-014).

## Success Criteria

Measurement conventions: renders at 48 kHz unless stated; "reference geometry" is
`fftSize = 2048`, hop 512; features via `tests/test_helpers/`. No criterion uses a bit-exact float
golden (roadmap line 504); `render_fingerprint.h` tolerances are used where a render must be pinned.
Test-case names are the sketch the build implements; each becomes a compliance row.

- **SC-001 — Decoherence raises spectral flatness, monotonically.**
  *(`SpectralSmear_FlatnessVsDecoherence`, `[long]`)*
  Input: 1 kHz sine at −12 dBFS, 40 s. Sweep `decoherence` over `{0, 0.25, 0.5, 0.75, 1.0}` as five
  separate renders (`smearAmount = 0`, tilt 0).
  **Measurement (a) — tiled flatness, not one call on the whole buffer.**
  `calculateSpectralFlatness` (`tests/test_helpers/signal_metrics.h:326`) analyses **one** Hann-windowed
  frame of at most **4096 samples taken from the START** of whatever span it is handed
  (`:335-337` caps `fftSize` at 4096; `:349-352` windows `signal[0..fftSize)`), so handing it a 20 s
  span would measure 85 ms of it and leave the render length, the span and the warm-up discard
  inoperative — and a single 85 ms estimate of a per-frame-randomised stochastic signal is far too
  noisy to carry a monotonicity gate. The metric is therefore the **mean over N ≥ 200 non-overlapping
  4096-sample windows tiling the last 20 s** of each render (20 s at 48 kHz = 234 windows), after
  discarding `2 * fftSize` warm-up samples. **Threshold on the tiled mean:** the five values are
  non-decreasing, and each of the four steps shows a relative increase ≥ 10 %.
  *(Wherever else this helper is used on a span longer than 4096 samples, the same tiling applies.)*
  **Measurement (b) — the line becomes a band, against an analytic prediction.** The floor-anchored
  ratio `flatness(1.0)/flatness(0)` is **not** used: `flatness(0)` is the leakage/round-off floor of a
  windowed pure tone, which differs between MSVC, GCC and AppleClang (`-ffast-math`), so a threshold
  over it is a threshold over a toolchain artefact. Instead measure the **out-of-mainlobe energy
  fraction** `ρ(d)`: average the power spectra of ≥ 100 non-overlapping 8192-sample frames (5.86 Hz per
  bin at 48 kHz) over the same steady region, and take
  `ρ = 1 − E(1 kHz ± 2·sampleRate/fftSize) / E(all bins above DC)` — the band is the analysis window's
  Hann mainlobe half-width, ±46.9 Hz at the reference geometry.
  Per-frame phases drawn uniformly on `±dπ` leave a coherent carrier of `sin(dπ)/(dπ)` and scatter the
  rest into hop-rate sidebands, so the prediction is `ρ(d) = 1 − (sin(dπ)/(dπ))²`, i.e.
  `{0, 0.19, 0.59, 0.91, 1.00}` at the five sweep points.
  **Threshold:** `ρ` is non-decreasing; `ρ(0) ≤ 0.02`; `ρ(1.0) ≥ 0.80`; each of the four steps is
  ≥ 0.10 absolute; and each measured `ρ(d)` is within ±0.10 of the analytic value. Every quantity here
  is an energy *fraction*, so it is invariant to the make-up gain and to the drive level.
  **Third arm (stereo):** inter-channel correlation of the output falls monotonically as
  `decoherence` rises, reaching ≤ 0.3 at `decoherence = 1` — the observable consequence of FR-044's
  two streams.
  > **The roadmap's wording is "spectral-flatness increase monotonic with *smear amount*" (line 257).
  > This spec attaches the flatness criterion to *decoherence* instead, and the user ratified the
  > correction (Clarifications Q4, session 2026-09-11): the roadmap's Phase 4 success-criteria line is
  > amended to read "spectral-flatness increase monotonic with decoherence amount; per-bin magnitude
  > flux reduction monotonic with smear amount," so this spec satisfies the roadmap literally rather
  > than deviating from it — there is no DEVIATION row in Traceability for this any longer.** The
  > reason is analytic, not
  > preferential, and is recorded here so the change is visible rather than discovered mid-build: a
  > normalised leaky integrator (FR-020) has unity steady-state gain and does not change the long-term
  > magnitude spectrum of a **stationary** input at all, so a flatness-vs-`smearAmount` sweep on any
  > stationary test signal measures nothing and cannot discriminate a correct implementation from a
  > no-op. Phase decoherence *does* convert a spectral line into a band the width of the analysis
  > window, which is exactly a flatness increase. The roadmap's intent — "the fog knob is measurable in
  > the spectrum" — is preserved; its attribution is corrected. The magnitude half gets its own
  > discriminating criterion in SC-004. (Phases 2 and 3 each had to rewrite success criteria *during*
  > the build after measurement showed they could not discriminate; this one is rewritten before.)
- **SC-002 — Latency is reported correctly, and it is exact.** *(`SpectralSmear_Latency`)*
  (a) `getLatencySamples() == fftSize` for every prepared geometry in `{512, 1024, 2048, 4096}`, and
  `0` before `prepare()`.
  (b) Feed a Kronecker impulse at input sample `impulseIndex = 2 * fftSize` — **not at sample 0** —
  with all controls at defaults; the output's peak sample index equals
  `impulseIndex + getLatencySamples()` **exactly** (not within a tolerance), and every sample in
  `[0, getLatencySamples())` is exactly `0.0f` (which is FR-014's warm-up counter under test).
  *Why the impulse cannot sit at sample 0:* `Window::generateHann` is the periodic variant, so
  `window[0] = 0.5 − 0.5·cos(0) = 0.0f` exactly (`core/window_functions.h:111-120`); `STFT::analyze`
  reads "the oldest `fftSize` samples" from the stream origin and advances only forward by `hopSize`
  (`primitives/stft.h:144-171`), so input sample 0 is covered by frame 0 **only**, at window index 0,
  where the window is zero. An impulse there is annihilated, the whole output is identically `0.0f`,
  and `argmax` of an all-zero buffer is index 0 — a correct implementation would fail. Even a
  surviving impulse at sample 0 would land in the `OverlapAdd` COLA ramp-up (the first
  `fftSize − hopSize` output samples are covered by fewer than `numOverlaps` frames, `stft.h:249-262`,
  `:300-308`) and be attenuated by an unspecified factor. `2 * fftSize` is comfortably inside the
  steady region.
  (c) Repeat (b) with `smearAmount = 1`, `decoherence = 1`
  and each tilt extreme: `getLatencySamples()` is unchanged and the first `getLatencySamples()` output
  samples are still exactly `0.0f` (FR-015 — no control changes latency). No peak-index claim is made
  in this arm: at `decoherence = 1` there is no impulse left to locate.
  (d) **Prepare-time disable (FR-019).** With `PrepareConfig{.fftSize = 2048, .enabled = false}`:
  `isPrepared()` is `true`, `isEnabled()` is `false`, `getLatencySamples() == 0`,
  `getAllocatedBytes() == 0`, and a 10 s render of white noise comes back **bit-identical** to the
  input (a true bypass, FR-019).
  (e) **Disabled-instance geometry reads match an enabled twin (Clarifications Q6).** Prepare two
  instances at the same `fftSize`: one `enabled = true`, one `enabled = false`. `getFftSize()`,
  `getHopSize()`, `getNumBins()` and `getSampleRate()` are identical between the two. Set the same
  `smearAmount`/`decoherence`/`smearTilt` targets on both: on the disabled instance,
  `getAppliedSmearAmount()`, `getAppliedDecoherence()` and `getAppliedSmearTilt()` equal the just-set
  **target** reads exactly (no frame ever runs to smooth them), while `getLatencySamples()` and
  `getAllocatedBytes()` stay `0` and the enabled twin reports its normal non-zero values for both.
- **SC-003 — Transparent at zero: null test within tolerance.** *(`SpectralSmear_NullAtZero`)*
  Input: 20 s of white noise at −12 dBFS, plus a second arm with a 5-partial tone. With
  `smearAmount = 0`, `decoherence = 0` and tilt swept across `{−1, 0, +1}` (tilt must be inert when
  amount is zero), align the output by `getLatencySamples()` and compare to the input over the steady
  region.
  **Threshold:** peak absolute residual ≤ `1.0e-4` and residual RMS ≤ **−80 dBFS** relative to the
  input RMS. (Hann at 75 % overlap with the synthesis window is COLA, `stft.h:224-227`, so the round
  trip is analytically exact; the tolerance covers float round-off in the FFT pair only.)
- **SC-004 — Magnitude smear does what it claims, and lows smear longer.**
  *(`SpectralSmear_MagnitudeMemory`, `[long]`)*
  This is the discriminating criterion for the half SC-001 cannot see.
  **Flux, defined here (Clarifications Q2) — no such helper exists yet in `tests/test_helpers/`**
  (`signal_metrics.h`, `spectral_analysis.h`, `audio_features.h` and `statistical_utils.h` were checked
  and none defines one): add a plain function, not a class, to `tests/test_helpers/` and reuse it from
  every arm below.
  `flux(band) = mean over frames f of [ (sum over bins k in band of |mag_f[k] − mag_{f−1}[k]|) /
  (sum over bins k in band of mag_f[k]) ]`, i.e. the frame-to-frame L1 magnitude difference normalised
  by the frame's own magnitude sum, computed **on an independent analysis STFT over the component's
  output** — not the component's own internal `SpectralBuffer`. **"Reduction" always means the ratio
  `flux(amount = 0) / flux(amount = 1)`, never the subtractive form `1 − flux(1)/flux(0)`**: at the
  spec's default endpoints the ratio form reads `4.81` at tilt 0 and `113.6` at tilt +1, while the
  subtractive form reads `1.10` and `1.91` and **fails the factor-of-2 gate in (b) on a correct build**.
  **Analysis geometry** is the component's own `fftSize`/`hopSize` for arms (a)–(d); for arm (e), which
  compares a `kMinFftSize = 512` run against the reference-geometry run, the analysis STFT is fixed at
  `fftSize = 1024`, hop `256`, **on both sides**, so the two flux values are computed on the same grid
  and are numerically comparable.
  (a) **Flux falls with amount, at every point of the sweep.** Input: noise band-limited to
  **`[20 Hz, 16 kHz]`** (so both the `[20, 200]` Hz and `[4 k, 12 k]` Hz bands of (b) carry energy),
  amplitude-modulated at 4 Hz. Compute `flux` over the full band of the **output** for `smearAmount` in
  `{0, 0.25, 0.5, 0.75, 1.0}` at tilt 0, `decoherence = 0`, after discarding `2 * fftSize` warm-up
  samples (FR-022's priming rule, Clarifications Q1 — not `5 * tau`). **Threshold:** `flux` is
  non-increasing as `amount` rises; `flux(1.0) ≤ 0.5 * flux(0)`; and — the **anti-vacuity clause** —
  each of the four steps falls by at least `0.08 * flux(0)`, so an
  implementation in which `smearAmount` does nothing below 1.0 fails.
  This last clause is only reachable because FR-021 makes `amount` a magnitude-domain **blend**: under
  the rejected `p(k) = amount * poleTable(k)` mapping the sweep points would carry effective time
  constants of 0, 7.6 ms, 15 ms, 36 ms and 1.68 s at the reference geometry (D-12), i.e. the first four
  are all "no smearing" to within a frame or two and only the endpoint clause would carry information.
  Under the blend the effective flux is linear in `amount`, so the four steps are even by construction.
  (b) **Lows smear longer.** At `smearAmount = 1`, tilt 0, compute the per-band **reduction**
  `flux(0)/flux(1)` (as defined above) separately for `[20, 200]` Hz and `[4 k, 12 k]` Hz.
  **Threshold:** `reduction([20,200]) / reduction([4k,12k]) ≥ 2`.
  (c) **The time constant is real.** Controls pinned at **`smearAmount = 1`, `decoherence = 0`,
  `tilt = 0`, `tauLow = 3.0`, `tauHigh = 0.25`** — stated because the quantity is undefined without
  them: at the shipped default `smearAmount = 0` (FR-052) the identity gate fires and the decay is
  zero, and at any intermediate amount the output is FR-021's blend of the raw and smeared magnitudes
  rather than the integrator itself. At `amount == 1` the written magnitude **is** `state[k]`, so the
  decay is exactly `exp(−t/tau(k))`. Render ≥ 25 s: gate a 100 Hz tone off after 10 s and measure the
  output's decay to −40 dB of its steady level. **Threshold:** within ±25 % of the analytic
  `tau(100 Hz) * ln(100)` implied by FR-030 at the configured endpoints — at these values
  `u(100 Hz) = ln(5)/ln(1000) = 0.233`, `tau(100 Hz) = 3.0 · (0.25/3.0)^0.233 = 1.68 s`, so the target
  is **7.74 s**. The tolerance accommodates window spreading, not a wrong law.
  (d) **Tilt moves the ratio in the documented direction.** Re-run (b) at `tilt = +1` and `tilt = −1`:
  the low/high reduction ratio `reduction([20,200]) / reduction([4k,12k])` is strictly larger at `+1`
  and strictly smaller at `−1` than at `0`.
  (e) **The law survives the minimum geometry.** Re-run (b), with the component prepared at
  `fftSize = kMinFftSize = 512` but the **flux-measuring analysis STFT fixed at `fftSize = 1024`, hop
  `256`** (Clarifications Q2, option C folded in for this arm only, so the two runs are numerically
  comparable): the factor-of-2 separation still holds. This is the criterion that justifies FR-011's
  raised minimum.
- **SC-005 — Bounded and non-degenerate under a worst-case soak.**
  *(`SpectralSmear_BoundednessSoak`, `[long]`)*
  **Configuration is fixed, not accelerated (Clarifications Q7): 48 kHz, reference geometry
  (`fftSize = 2048`, hop 512, `WindowType::Hann`), a full 30 minutes of real audio.** At FR-060's own
  projection of ~25 000 ns per 512-sample block this is ≈168 750 blocks ≈ 4–5 s of wall clock, so there
  is no motive to accelerate the sample rate — doing so would change the bin spacing and the
  tau-in-frames ratio the soak is meant to measure. Input = pink noise at −6 dBFS
  with **60 s gaps of digital silence — which is ≥ 6 · `kMaxSmearSeconds`, the sizing rule the gap
  clause (v) below depends on** — controls driven to their extremes on a slow schedule
  (`smearAmount` and `decoherence` sweeping 0↔1, tilt sweeping ±1, both time endpoints at their
  extremes on alternate passes). The schedule additionally **parks the controls at a fixed reference
  setting (`smearAmount = 1`, `decoherence = 0.5`, `tilt = 0`, default endpoints) for one full 60 s
  pink-noise-active window in each 5-minute pass** — six *reference windows* in all — so that (iv) has
  windows it can legitimately compare.
  **Thresholds:** (i) every output sample finite (`detail::isFinite`); (ii) peak ≤ `kOutputClamp`;
  (iii) `getClampEngagements() == 0` at −6 dBFS input — the clamp is a backstop, not a working part;
  (iv) the RMS of the **six reference windows** drifts by ≤ 0.5 dB across the render (no creep, no
  death). The windows are measured **only over pink-noise-active audio** — a 60 s window that landed in
  a silent gap would read `−inf` dBFS, and the statistic would not be defined over the signal it was
  applied to — and **only at equal control settings**: SC-006 allows the coherence make-up ±0.5 dB of
  its own, so two windows at different `decoherence` values may legitimately differ by ~1 dB and
  comparing them would measure the make-up curve rather than creep.
  (v) **the tail decays at the rate the configured `tau` implies** — during each silent gap the output
  falls by **at least 40 dB relative to its pre-gap RMS within `5 * tauMax` seconds**, where
  `tauMax = max(tauLow, tauHigh)` **as configured for that pass** (`tauLow < tauHigh` is legal and
  unclamped per the Edge Cases, so `tauLow` alone is the wrong constant to reference), and is
  monotonically non-increasing through the remainder of the gap to within a 1 dB ripple allowance.
  At the extreme pass `tauMax = kMaxSmearSeconds = 10 s`, so the clause needs 50 s of the 60 s gap:
  FR-020's state decays as `exp(−t/tau)`, and −40 dB relative is `t = tau·ln(100) = 4.61·tau`.
  The threshold is stated **relative to the pre-gap level, never as an absolute dBFS floor**: an
  absolute −80 dBFS from a −6 dBFS-driven steady level needs `tau·ln(10^(74/20)) = 8.5·tau` = 85 s at
  the extreme pass, which exceeds the gap itself — a correct implementation would fail it. This clause
  asserts the `kMaxSmearSeconds` bound (FR-031), which is what actually prevents an infinite hold;
  FR-023's `kMaxPole` is unreachable at the shipped ranges and is asserted white-box instead.
  **Isolation (Clarifications Q7).** Although this criterion asserts signal levels rather than
  wall-clock timing, it is a multi-minute render like every other `[long]` case and must be run alone,
  nothing else executing concurrently; the `[long]` tag already excludes it from the per-push CI lane,
  so it runs in the nightly `[long]` lane.
- **SC-006 — Coherence make-up is measured, not assumed.** *(`SpectralSmear_CoherenceMakeup`)*
  With `smearAmount = 0`, render 30 s of white noise at each of the five knot values of `decoherence`
  and measure output RMS / input RMS **with the make-up gain divided out** — divided out through the
  component's own public `SpectralSmear::coherenceMakeup(d)` (FR-042), which exists precisely so this
  criterion is executable: nothing else on the query surface (FR-018, FR-053) exposes `g(d)` and there
  is no setter that disables it.
  **Thresholds, split so that neither arm is circular:**
  (a) **Knots (the informative arm).** Each measured raw ratio at knot `i` is within **2 %** of
  `1 / SpectralSmear::kCoherenceMakeup[i]` **as shipped in the header at the time the test runs**. Note
  what the ±0.5 dB round-trip arm would claim here on its own: if the shipped knots are the reciprocals
  of the values measured in this same test, "make-up applied ⇒ RMS restored" is a tautology. This arm
  is the one with content, and it is the arm that fails if the table drifts from the geometry.
  (b) **Interpolant.** With the make-up **applied**, output RMS is within **±0.5 dB** of input RMS at
  **ten intermediate values** of `decoherence` (the knots themselves are covered by (a)) — here the
  cubic-Hermite interpolant, not the table, is under test.
  (c) **Transfer check.** Each measured raw knot is reported against `AetherReverb`'s shipped
  `kCoherenceMakeup` (`aether_reverb.h:2774-2776`); agreement within 2 %
  confirms the transfer argument at `:640-643` and the shipped values are transcribed with a citation,
  otherwise the measured table ships and the divergence is documented in the header (FR-042).
- **SC-007 — Zero allocation after prepare.** *(`SpectralSmear_NoAllocation`)*
  Inside an `AllocationScope` (`tests/test_helpers/allocation_detector.h:111`): 5 000 `processBlock`
  calls at block sizes `{1, 7, 30, 64, 65, 511, 512, 2048}` interleaved with every setter in FR-050
  (including `setSmearTimeLow`/`setSmearTimeHigh`, which must also allocate nothing) and with
  `setSeed`. **Threshold:** `getAllocationCount() == 0`.
- **SC-008 — Footprint is reported and bounded.** *(`SpectralSmear_AllocatedBytes`)*
  `getAllocatedBytes()` matches an independently computed expectation **exactly** for each geometry in
  `{512, 1024, 2048, 4096}`, and is **≤ 128 KiB at `fftSize = 4096` stereo**.
  **The scope of "the footprint" is FR-017's and is load-bearing here**, because the two readings give
  opposite verdicts: the figure is the component's **own** vectors — three pole tables plus the
  per-frame tilt-resolved scratch table (`4 · 2049 · 4 B = 32.8 KiB`), two magnitude memories
  (`2 · 2049 · 4 B = 16.4 KiB`), two FIFOs (`2 · bit_ceil(4096 + 64 + 1024) · 4 B = 64 KiB`) and the
  hop scratch (`2 · 1024 · 4 B = 8 KiB`), ≈ 121 KiB — and **excludes** the sub-objects, whose own heap
  at this geometry is already ~512 KiB on its own (`STFT::inputBuffer_` at `fftSize * 8` floats per
  channel = 256 KiB, `stft.h:78`; `windowedFrame_` + `window_` = 32 KiB; `OverlapAdd::outputBuffer_` at
  `fftSize * 2` per channel = 32 KiB plus `ifftBuffer_` + `synthesisWindow_` = 64 KiB; two
  `SpectralBuffer`s ≈ 64 KiB). The sub-object total is **computed and reported by the test as an
  informational figure**, not asserted — it is `STFT`/`OverlapAdd`/`SpectralBuffer`/`FFT` policy, not
  this component's.
  Second arm: with `enabled = false` (FR-019) `getAllocatedBytes() == 0` at every geometry.
- **SC-009 — Seed determinism, including across a zero crossing.**
  *(`SpectralSmear_SeedDeterminism`)*
  (a) Two instances with the same seed, same prepare, same control script produce renders whose
  `compareFingerprints` (`render_fingerprint.h:122`) passes at the shipped tolerances
  (`kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4`). (b) Different seeds produce renders
  that **fail** the same comparison (the anti-vacuity arm). (c) Two control scripts that differ **only**
  in the length of a mid-render park at `decoherence == 0` — both parks a whole number of frames, both
  **ending at or before absolute sample `S`** — produce fingerprints over the **fixed absolute window
  `[S + 2 * fftSize, end)`** that pass `compareFingerprints`. The window is pinned absolutely on
  purpose: FR-043 burns one draw per bin per channel per frame regardless of the gate, so the stream
  position is a function of **elapsed frames**, not of dwell time; two such renders match at any given
  absolute sample index and do **not** match when aligned to "the moment the park ends". Stating it the
  latter way would fail a correct implementation and pass an implementation that skipped the draws
  entirely, i.e. exactly inverted the criterion. (d) `reset()` followed by the same script reproduces
  (a).
- **SC-010 — Sample-rate independence.** *(`SpectralSmear_SampleRateIndependence`)*
  Run SC-004 (c)'s decay measurement at 44 100, 48 000 and 96 000 Hz with identical control values —
  **`smearAmount = 1`, `decoherence = 0`, `tilt = 0`, `tauLow = 3.0`, `tauHigh = 0.25`, repeated here
  rather than referenced, because the measured quantity is undefined without them** (analytic target
  `tau(100 Hz) · ln(100) = 7.74 s`, unchanged across rates since `tau` is specified in seconds, FR-065).
  **Threshold:** the measured −40 dB decay times agree within ±10 % across all three rates, and each is
  within ±25 % of the analytic target.
  Second arm: `getLatencySamples() == fftSize` at every rate (latency is a sample count, not a time).
- **SC-011 — Block-partition invariance.** *(`SpectralSmear_PartitionInvariance`)*
  The same 60 s input rendered as one 512-sample-blocked pass and as a pass with a pseudo-random block
  schedule drawn from `{1, 7, 30, 63, 64, 65, 300, 512, 1024, 4096}` produces outputs that pass
  `compareFingerprints` at the shipped tolerances, **sample-aligned with no offset correction** —
  which is the direct assertion of FR-014's counter (a "drain when available" implementation fails
  this with a partition-dependent offset).
- **SC-012 — No time-domain smearing artifacts: bounded pre-echo, and no clicks on control jumps.**
  *(`SpectralSmear_PreEchoAndClicks`)*
  (a) **Pre-echo is confined to one window.** Input: 2 s digital silence, then a 1 kHz tone burst at
  −6 dBFS. Align by `getLatencySamples()`. Measure RMS in the window `[onset − 2·fftSize,
  onset − fftSize)` — strictly more than one analysis window before the onset — for every corner of
  `{smearAmount, decoherence} ∈ {0,1}²` and every tilt extreme. **Threshold:** ≤ **−90 dBFS**
  relative to the burst's peak, at every setting. (The window `[onset − fftSize, onset)` is *expected*
  to carry energy — that is inherent STFT window spread, not an artifact — and is reported, not
  asserted against a fixed number; the assertion is that the smear does not extend pre-onset energy
  **beyond** the window it is allowed to occupy.)
  (b) **Post-onset tail is expected and tracks tau.** The same render's decay after burst-off is
  longer at `smearAmount = 1` than at `0` — the anti-vacuity arm proving (a) does not pass merely
  because the stage does nothing.
  (c) **No clicks on control jumps.** Step `smearAmount` 0→1, `decoherence` 0→1 and `tilt` −1→+1 in a
  single block during a sustained tone; `ClickDetection` (`tests/test_helpers/artifact_detection.h:72`)
  reports zero clicks, and the peak inter-sample delta does not exceed the un-stepped render's by more
  than 6 dB. This is what FR-035's 50 ms smoothing **and FR-046's per-hop make-up ramp** are for: the
  `decoherence 0 → 1` arm is the one that fails without FR-046, because a per-hop-constant `g` would
  jump 1.000 → ~1.50 at a single sample boundary (~11.6 dB above a 1 kHz tone's peak inter-sample
  delta) on the first frame after the step.
- **SC-013 — CPU: roadmap budget 0.5 % global, binding effective ceiling 0.333 % (35 555 ns/block).**
  *(`SpectralSmear_CpuBudget`, `[.perf]`)*
  Basis: **ns per 512-sample block at 48 kHz**, best-of-25 over 500 blocks after 400 warm-up blocks,
  nothing else running (`node tools/run-cpu-tests.js`). Reference
  `kReferenceNs = 10 666 667 * 0.005 = 53 333 ns`. Three configurations, each with its own checked-in
  baseline: (a) defaults (both gates identity — the transparent cost); (b) reference geometry,
  `smearAmount = 1`, `decoherence = 1`, tilt modulated every block (the worst case);
  (c) `fftSize = 512`, same worst-case controls (the highest frame-rate geometry).
  Each baseline carries `static_assert(kBaseline * 1.5 <= kReferenceNs)` and
  `static_assert(kBaseline >= kReferenceNs / 50.0)`, and the runtime check is
  `REQUIRE(measured <= kBaseline * 1.5)`. **The first assert is what makes the effective ceiling
  `kReferenceNs / 1.5 = 35 555 ns/block (0.333 % of one core)`, and that — not the roadmap's 0.5 % —
  is the figure each compliance row is measured against (FR-060).** Each baseline's comment states its
  measured value as a percentage of a core and repeats the ceiling, in the Phase-1 comment form
  (`vorago_p1_perf_test.cpp:92-97`). A measurement in `[35 556, 53 333] ns` is not shippable: it cannot
  be encoded as a baseline, so it takes FR-060's lever ladder and then the stop-and-surface rule.
  Baselines are measured on the reference machine and their
  provenance (machine, five consecutive idle runs, spread) is written into the TU
  (`vorago_p1_perf_test.cpp:84-90` format). **Over budget ⇒ reduce cost per FR-060's ordered levers,
  then stop and surface. Never raise a baseline, never shrink the workload.**
- **SC-014 — Shared components untouched, consumers green.** *(a build + diff gate, no new test)*
  `git diff --stat` for the phase shows **only** `dsp/include/krate/dsp/processors/spectral_smear.h`
  under `dsp/include/`. `dsp/include/krate/dsp/systems/atmosphere_engine.h` and
  `dsp/include/krate/dsp/effects/aether_reverb.h` are byte-unchanged. All of `dsp_primitives_tests`,
  `dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`, `seraphis_tests`, `innexus_tests`
  pass.
- **SC-015 — Layer, ODR, portability and lint gates.** *(tooling)*
  `node tools/lint-layers.js`, `node tools/lint-odr.js`, `node tools/lint-nonfinite-symbols.js`,
  `node tools/lint-float-bit-goldens.js`, `node tools/lint-simd-aligned-loadstore.js` and
  `node tools/check-portability.js` all pass. The four new TUs are registered **by name** in
  `dsp/tests/CMakeLists.txt`'s `dsp_processors_tests` list (the list is enumerated, not globbed — an
  unregistered TU silently drops out and its cases never run), and
  `spectral_smear_nonfinite_test.cpp` **only** is additionally listed in the `-fno-fast-math` block;
  the other three must not be, so the FR-008 guards are proved in the FP mode the header ships in.
- **SC-016 — Non-finite input is survivable and non-latching.** *(`SpectralSmear_NonFinite`, in the
  `-fno-fast-math` TU)*
  Inject NaN and ±Inf **built from bit patterns through a volatile sink** (never
  `std::numeric_limits<>::quiet_NaN()`, which folds to finite garbage under `-ffast-math`) into one
  channel for one block, at `smearAmount = 1`.
  **Thresholds:** (i) no output sample is non-finite after the injecting block;
  (ii) `getPoisonEngagements() >= 1`; (iii) the component **recovers on its own**, with no `reset()`
  call, and both the *origin* and the *window* of that measurement are stated because the plausible
  readings straddle the bound: measured from the **last injected sample** `E`, the output RMS over the
  absolute window `[E + 2 * fftSize, E + 3 * fftSize)` is within **0.5 dB** of the un-injected
  reference's RMS over the **same absolute window** — reachable in one frame because FR-062's poison
  clear re-arms the priming flag (Clarifications Q1); a zero-initialised recovery would miss this
  threshold by 12–26 dB at the reference geometry's low/mid/high bins; (iv) the silent gap is bounded in shape as well as
  in length — **no more than `ceil(fftSize / hopSize) + 1 = 5` consecutive frames synthesise silence**.
  The arithmetic behind both: FR-062 zeroes the magnitude memory and the frame's spectrum but does
  **not** clear `STFT`'s input ring, which keeps returning the injected samples for `fftSize` more
  samples (`primitives/stft.h:144-171` reads the oldest `fftSize` samples), so a one-block (512-sample)
  injection at the reference geometry is covered by 4 consecutive analysis frames and the last of them
  contributes over a further `fftSize` output samples of overlap-add. Any window overlapping that gap
  would fail the 0.5 dB comparison however correct the implementation; `[E + 2·fftSize, E + 3·fftSize)`
  is clear of it. Arms (iii) and (iv) are the direct assertion of FR-062's deliberate deviation
  from `AtmosphereEngine`'s latch.
- **SC-017 — The control grid advances once per frame, not once per channel and not once per block.**
  *(`SpectralSmear_ControlCadence`)*
  FR-013 (a) names "the smoothers advance once per hop of audio and not twice" as an invariant that
  **fails silently** — a double advance merely halves the 50 ms smoothing time, which is still
  click-free (SC-012 (c)) and still costs the same (SC-013), so no other criterion changes verdict.
  FR-053's applied reads exist to make it observable, and this is the criterion that reads them.
  With everything else static, issue `setSmearAmount(1.0)` at a known sample index, then sample
  `getAppliedSmearAmount()` after each of the first 64 blocks and assert the trajectory matches
  `1 − coeff^f` to within `1e-4`, where `coeff` is `OnePoleSmoother`'s coefficient at
  `configure(50 ms, sampleRate / hopSize)` and **`f = max(0, floor((samplesProcessed − fftSize) /
  hopSize) + 1)`** is the number of **elapsed frames** — not `floor(samplesProcessed / hopSize)`, which
  over-counts by `fftSize/hopSize − 1 = 3` frames at every 75 %-overlap geometry because FR-013 only
  advances the smoothers inside `while (stft_[0].canAnalyze())`, and `canAnalyze()` is
  `samplesAvailable_ >= fftSize_` (`primitives/stft.h:137`) — the first frame does not exist until
  `fftSize` samples have been pushed (Clarifications Q3). The control grid stays tied to the analysis
  frames; this is a correction to the criterion's formula, not a change to FR-013's behaviour — not
  blocks, not frame-channel pairs. Run it for at least two block partitions
  (512, and the pseudo-random schedule of SC-011) and two `fftSize` values (2048 and 512). A
  double-advance shows up immediately as `f` doubling; a per-block advance shows up as the trajectory
  decoupling from `hopSize`.
- **SC-018 — Priming is real: the first written frame after `reset()` matches the analysed frame.**
  *(`SpectralSmear_MagnitudePriming`)* At `smearAmount = 1` (so the written magnitude **is** `state[k]`,
  FR-021), `decoherence = 0`, tilt 0: call `reset()`, then push exactly one analysis frame's worth of a
  1 kHz tone burst and compare the **first** frame the component writes to the magnitudes `STFT` itself
  analysed for that frame. **Threshold:** per-bin absolute difference ≤ `1e-6`. A zero-initialised
  implementation fails this by 12–26 dB (the same shortfall SC-016 (iii) would show at its recovery
  window) rather than by a rounding-sized margin, so the arm cannot pass by accident. Repeated after a
  poison clear (inject one non-finite sample, let FR-062 (a) fire, then feed the same tone burst): the
  next written frame matches its analysed magnitudes to the same `1e-6` tolerance (Clarifications Q1).

## Edge Cases

**RT-safety boundaries**
- `processBlock` with `numSamples` of 0, 1, 7, 63, 64, 65, and values far larger than `fftSize`
  (16 384) — FR-012's chunking must hold; no allocation (SC-007), no `STFT` ring overflow.
- `processBlock` before `prepare()`, with a null `left` or null `right` — returns with buffers
  untouched (FR-003).
- Every setter called before `prepare()` — values are stored and clamped; no table rebuild touches a
  zero-sized vector (`setSmearTimeLow` on an unprepared instance must be inert, not a crash).
- Every setter called in the same control pass as `processBlock` — values take effect at the next
  frame boundary via the smoothers (FR-035), never mid-frame.
- `setSmearTimeLow` / `setSmearTimeHigh` called every block: allocation-free (FR-064) but
  `3 * numBins` `exp` calls each — documented as control-thread cadence, and SC-013 (b) does **not**
  include them (tilt is the modulation target, not the endpoints).

**Parameter extremes**
- `smearAmount` and `decoherence` at exactly 0.0f and exactly 1.0f, and at the smallest positive float
  above zero (which must take the *full* path, not the identity gate — FR-021/FR-041 are exact-value
  gates, not thresholds).
- `smearTilt` at exactly ±1.0f, and at 0.0f reached both by `setSmearTilt(0)` and by a smoother
  settling to zero (`snapToTarget`, `primitives/smoother.h:257`).
- `tauLow == tauHigh` (a flat, frequency-independent law): legal, and SC-004 (b)'s separation must
  then vanish rather than invert.
- `tauLow < tauHigh` (inverted law — highs smear longer): legal and not clamped; it is a valid patch,
  and the component must not silently swap the endpoints.
- `tauLow` and `tauHigh` at `kMaxSmearSeconds = 10 s` with `fftSize = 4096` at 96 kHz — the longest
  reachable pole, `exp(−1024 / (96000 · 10)) = 0.998935`. What bounds the hold here is
  `kMaxSmearSeconds`, not `kMaxPole = 0.99999f`, which is three orders of magnitude away and unreachable
  at every shipped range (FR-023 carries the arithmetic). SC-005 (v)'s relative decay must complete
  within `5 · tauMax = 50 s` of the 60 s gap.
- **White-box:** `poleTable()` evaluated at synthetic extremes (a derived `tau` far above
  `kMaxSmearSeconds`, and a degenerate `sampleRate * tau` product) stays inside `[0, kMaxPole]` — a unit
  assertion on the table, since no render can reach the clamp.
- `tauLow` and `tauHigh` at `kMinSmearSeconds = 0.02 s` with **`fftSize = 4096` at 44.1 kHz** — the only
  shipped configuration where `hopSize / (sampleRate * tau)` exceeds 1
  (`1024 / (44100 · 0.02) = 1.161`), giving a pole of `exp(−1.161) = 0.313`. The integrator must remain
  a convex combination at that value, i.e. the pole stays in `[0, kMaxPole]` and the written magnitude
  tracks the input within one or two frames. (At `fftSize = 512` / 44.1 kHz the ratio is
  `128 / (44100 · 0.02) = 0.145` and the pole is 0.865 — the previous wording named that geometry and
  described a state it cannot reach. A *negative* pole is unreachable for any input, since `exp(−x)` is
  strictly positive for every finite `x`, so no clause asserts against one.)
- Non-finite arguments to every setter (FR-009): inert.
- `setSeed(0)`: `deriveStreamSeed` guarantees a non-zero derived seed (`core/random.h:102-113`), so
  both streams stay live — asserted, because `Xorshift32::seed()` would otherwise silently substitute
  its default and collapse the two streams onto one if both salts hashed to 0.

**Sample-rate and geometry changes**
- `prepare()` called twice with different sample rates, and with different `fftSize` values — all
  tables, FIFO capacities and the warm-up counter re-derived; no stale pole table from the previous
  rate (SC-010).
- `prepare()` with `fftSize` of 0, 1, 100, 513, 5000, `SIZE_MAX` — clamped to `[512, 4096]` then
  `bit_floor`'d; 513 → 512, 5000 → 4096 (FR-011).
- `prepare()` with a sample rate of 0 or negative — treated as unprepared; `isPrepared()` false.
- `prepare()` with `enabled = false`, then every setter called and a full render pushed through
  (FR-019): buffers bit-identical, `getLatencySamples() == 0`, `getAllocatedBytes() == 0`, no counter
  moves; then `prepare()` again with `enabled = true` on the same instance — the component allocates
  and behaves exactly as a freshly prepared one (SC-002 (d), SC-008's second arm).
- `reset()` mid-render: output returns to `fftSize` samples of exact silence (the warm-up counter is
  re-armed, FR-004) rather than resuming mid-tail. This is a **discontinuity by design** — the owner
  is expected to reset only at note-off/transport boundaries — and it is asserted so that a future
  "smoother reset" is a deliberate change, not an accident.

**Seed determinism**
- Identical seed, identical script, different **block partitioning** → identical output (SC-011);
  identical seed but a different *number of frames* elapsed before a control change → the streams have
  advanced by the frame count, not by the block count, which is what FR-013's once-per-frame advance
  guarantees.
- A render that parks at `decoherence == 0` for a variable whole number of frames: stream positions
  must still match (FR-043, SC-009 (c)).
- The two channels must never produce identical perturbation sequences even though both streams derive
  from the same base seed (FR-044's disjoint salts) — asserted by SC-001's correlation arm.

## Decisions taken where the roadmap is silent

- **D-1 — Stereo in / stereo out, per-channel independent bins, one shared control clock.** The
  roadmap places Spectral Smear in the **Global** block (line 78), after the voice sum; both shipped
  spectral stages are stereo with per-channel STFTs and a shared control advance
  (`atmosphere_engine.h:2273-2283`, `aether_reverb.h:1816-1823`). A mono component would force the
  owner to instantiate two and to duplicate the control clock, which is exactly the mistake FR-013's
  invariant (a) exists to prevent.
- **D-2 — Geometry is prepare-time and fixed at 75 % overlap with the synthesis window.** Forced by
  `stft.h:224-227` for a spectral-modification processor; not a choice. `fftSize` default 2048
  (42.7 ms latency at 48 kHz) rather than Atmosphere's 1024, because a drone instrument's global fog
  stage benefits from the low-frequency bin resolution and — by the `log N` argument in FR-060 — pays
  almost nothing for it.
- **D-3 — Minimum `fftSize` is 512, not the 256 that `FFT` (`fft.h:44`), `SpectralGate` (`:95`) and
  `AtmosphereEngine` (`:319`) permit.** Requirement-driven: FR-030's law needs bins below 200 Hz, and
  256/48 kHz gives one. SC-004 (e) is the enforcing measurement.
- **D-4 — No dry/wet mix.** (FR-021's `amount` blend is *inside the spectral frame*, between the
  analysed magnitude and the integrator state of the same frame — it shares the round trip's latency
  exactly and is not a parallel dry path, so none of what follows is affected by it.)
  The identity gates (FR-021, FR-041) *are* the transparency point, and they
  are exactly transparent rather than transparent-within-a-crossfade. A mix control would add a
  latency-matched dry path — a second thing to get wrong — for no roadmap requirement. Phase 10 may
  place a mix around the component if the voice architecture wants one.
- **D-5 — Neither shipped engine is refactored.** Fully evidenced in FR-070 and asserted by SC-014.
  This is the roadmap's own conditional (line 253) resolved on the "otherwise" branch.
- **D-6 — Two independent decoherence RNG streams (AetherReverb's shape), not one shared stream
  (Atmosphere's).** Derivation in FR-044.
- **D-7 — Tilt is applied by three-table linear interpolation, and the interpolation *is* the
  specified law.** FR-034. The alternative — a per-bin `pow` per frame — costs ~`numBins`
  transcendentals per frame for a control the roadmap explicitly names as a modulation target
  (line 255), i.e. one that moves continuously. Defining the law as the interpolation removes any
  question of approximation error and keeps the runtime cost at two multiplies per bin.
- **D-8 — Non-finite handling clears and continues; it does not latch.** Derivation in FR-062. This is
  a deliberate, documented deviation from `AtmosphereEngine`'s latch (`:2252-2260`), justified by
  global-bus placement.
- **D-9 — The Dormancy cross-cutting rule (roadmap lines 498–503) does not apply to this component,
  and that is recorded rather than passed over.** Dormancy governs components with sleep/wake life
  cycles (Phase 3 peaks, Phase 5 loops, Phase 8 agents). `SpectralSmear` has no slots and no life
  cycle. Its structural analogue is the exact-identity gate: at zero the *processing* is skipped, the
  *state* keeps tracking (FR-021) and the *RNG lanes keep advancing* (FR-043) — the same shape applied
  to a single stage instead of to a slot table. No 50 ms re-entry ramp is needed because the gate's
  entry and exit are continuous in the parameter (a pole of `0 + ε` differs from a pole of `0` by ε),
  unlike a gain gate.
- **D-10 — The magnitude integrator runs at the frame rate, not at a further-decimated control rate.**
  It is already the cheapest thing in the frame (one multiply-add per bin against an `O(N log N)`
  FFT), and decimating it would make `tau` depend on the decimation factor for no measurable saving.
- **D-11 — There is a prepare-time `enabled` flag, and no runtime bypass.** FR-019. Both prior-art
  stages this component models itself on ship exactly this switch (`AtmosphereEngine`'s `blurEnabled_`
  behind `getLatencySamples()`, `atmosphere_engine.h:1129`; `AetherReverb`'s
  `spectralEnabled_ = config.spectralDiffusionEnabled`, `aether_reverb.h:1626`), and without it a
  Vorago patch with no fog pays the full stereo STFT round trip (SC-013 (a) measures precisely that
  configuration at roughly Atmosphere's ~0.2 %) **and** 2048 samples = 42.7 ms of unconditional latency
  on the **global** bus — a cost Phase 10 would have no way to decline. The flag is prepare-time, not
  runtime, because a latency that moves mid-render is a click plus a host renegotiation
  (`aether_reverb.h:614-616`); Phase 10 owns what to pass, this phase owns making the choice exist.
  **The default is `false` (Clarifications Q6), diverging deliberately from
  `AtmosphereEngine::PrepareConfig::blurEnabled_`'s `true` default (`atmosphere_engine.h:371`): that
  stage lives inside a voice layer the owner already chose, while this one sits on the global bus, so a
  Phase-10 owner who omits the field pays nothing rather than silently buying 42.7 ms of latency and
  ~0.2 % of a core.**
- **D-12 — `smearAmount` is a magnitude-domain blend (FR-021), not a scale on the pole.** The rejected
  form `p(k) = amount * poleTable(k)` is arithmetically unusable for a *modulation target*. At the
  reference geometry (fftSize 2048, hop 512, 48 kHz) with the default `tauLow = 3.0` /
  `tauHigh = 0.25`, `tau(100 Hz) = 1.68 s` and `poleTable(100 Hz) = exp(−512/(48000·1.68)) = 0.99366`.
  The applied poles at `amount ∈ {0, 0.25, 0.5, 0.75, 1}` would then be
  `0, 0.2484, 0.4968, 0.7452, 0.99366`, whose effective time constants
  `−hopSize / (sampleRate · ln p)` are `0, 7.6 ms, 15 ms, 36 ms, 1.68 s`: the entire audible travel of
  the knob sits in the last fraction of a percent below 1.0, so a `TidalModulator` sweeping 0 → 1
  (roadmap line 255, "fog rolls in") would do nothing for 99 % of its sweep and everything in the last
  1 %, and SC-004 (a)'s intermediate sweep points could not discriminate a correct implementation from
  one where `smearAmount` is a no-op below 1.0. The alternatives that keep the pole parametrisation
  even — interpolating in the `tau` or `log(1 − p)` domain — all need a transcendental **per bin per
  frame** (~1025 `exp` calls per 512-block at every geometry, 20–40 µs against a 35 555 ns ceiling),
  which the budget cannot afford. The blend is even by construction (flux is linear in `amount`),
  continuous at zero, cheaper than the rejected form, and leaves the pole a pure function of frequency
  and tilt, which is what makes SC-004 (c)'s analytic decay target well defined.

## Open Questions

The roadmap's own Open Questions (lines 513–527) assign no decision to Phase 4: item 3 is
Phase 9, item 4 is Phase 6, items 5–6 are Phase 10, items 1/2/7 are Phases 8 and 11. No roadmap
statement about this phase is *ambiguous*. Item 3 below, once a deviation pending ratification, is now
**resolved by roadmap amendment** (Clarifications Q4); the remaining two are **decided but
measurement-contingent**, each carrying a named stop-and-surface rule rather than a quiet default:

1. **The coherence-make-up knots** (FR-042). The spec's expectation is that `AetherReverb`'s shipped
   table transfers unchanged, and the transfer argument is cited; SC-006 measures ours anyway and names
   the 2 % threshold at which we ship our own instead.
2. **The CPU baseline** (FR-060, SC-013). The projection (~0.25–0.30 % against the 0.333 % effective
   ceiling that SC-013's `static_assert` imposes, itself inside the roadmap's 0.5 %) rests on
   a measured figure from a structurally identical shipped stage, but it is a projection. If the
   measurement lands over budget, FR-060 names the three levers in order and then requires stopping and
   surfacing — never a relaxed budget. The binding figure is the **35 555 ns effective ceiling**, not
   the roadmap's 0.5 %; FR-060 derives why and SC-013 encodes it.
3. **RESOLVED — the roadmap-threshold re-attribution (Clarifications Q4, session 2026-09-11).**
   Roadmap line 257 read "spectral-flatness increase monotonic with **smear amount**"; this spec
   attaches the flatness criterion to **`decoherence`** instead and gives `smearAmount` its own
   discriminating criterion (SC-004, flux-based). The user ratified the correction under option C: the
   roadmap's Phase 4 success-criteria line is amended to read "spectral-flatness increase monotonic
   with decoherence amount; per-bin magnitude flux reduction monotonic with smear amount," which this
   spec now satisfies literally. The analytic argument stands as the reason for the amendment — a
   normalised leaky integrator has unity steady-state gain and cannot move the long-term spectrum of a
   stationary input, so a flatness-vs-`smearAmount` sweep measures nothing and cannot distinguish a
   correct implementation from a no-op — and Traceability no longer carries a DEVIATION row for this
   line.

## Traceability

| Roadmap statement (line) | Requirements | Criteria |
|---|---|---|
| "New component (L2, `processors/spectral_smear.h`)" (248) | FR-001, FR-002 | SC-015 |
| "STFT (existing `STFT`/`SpectralBuffer`) → … → reconstruct" (250) | FR-010, FR-011, FR-012, FR-013, FR-014, FR-016, FR-019 | SC-002 (incl. (d)), SC-003, SC-011, SC-017 |
| "per-bin magnitude smearing (leaky integrator per bin …)" (250); **and, as amended (257, Clarifications Q4), "per-bin magnitude flux reduction monotonic with smear amount"** | FR-020, FR-021, FR-022, FR-023, FR-024, FR-025 | SC-004 (a), SC-005 (v) |
| *(ordering discipline, no roadmap line)* | FR-045 | **By inspection at the compliance pass** — the two orderings are bit-identical today, so no criterion can discharge it; the header must carry the comment FR-045 names |
| "frequency-dependent time constants — lows smear longer" (250) | FR-030, FR-031, FR-032, FR-034, FR-036, FR-065 | SC-004 (b)(c)(d)(e), SC-010 |
| "+ phase decoherence amount" (250) | FR-040, FR-041, FR-042, FR-043, FR-044, FR-046 | SC-001, SC-006, SC-009 (c), SC-012 (c) |
| "Extracted/generalized from the per-grain blur inside `AtmosphereEngine` (refactor … only if it is a genuine drop-in; otherwise leave Atmosphere untouched — no speculative unification)" (252–254) — **the "per-grain" characterisation is corrected in the Overview: the shipped stage is bus-wide, not per-grain** | FR-070, FR-071, FR-072 | SC-014 |
| "Smear amount and tilt are modulation targets (fog rolls in via `TidalModulator`)" (255) | FR-021, FR-033, FR-035, FR-050, FR-051, FR-053 | SC-004 (a), SC-012 (c), SC-013 (b), **SC-017** (the cadence invariant FR-053 exists to make observable) |
| "spectral-flatness increase monotonic with decoherence amount" (257, **amended from "smear amount"** — Clarifications Q4, session 2026-09-11, roadmap amendment applied in the plan stage) | FR-040, FR-042 | SC-001 |
| "latency reported correctly" (257) | FR-014, FR-015, FR-019 | SC-002, SC-011 |
| "transparent at 0% (null test within tolerance)" (258) | FR-010, FR-021, FR-041, FR-052 | SC-003 |
| "no time-domain smearing artifacts (pre-echo metric)" (258) | FR-010, FR-014, FR-035 | SC-012 |
| "CPU ≤ 0.5% global" (259) | FR-034, FR-060 | SC-013 |
| RT safety, pools sized at prepare (491–492) | FR-002, FR-003, FR-006, FR-012, FR-016, FR-017, FR-019, FR-064 | SC-007, SC-008 |
| Boundedness soak (493–495) | FR-031 (`kMaxSmearSeconds`, the binding bound), FR-023 (`kMaxPole`, white-box backstop), FR-061, FR-063 | SC-005 |
| Layer discipline + ODR sweep (496) | FR-001, New-components table | SC-015 |
| CPU budgets are FRs (497) | FR-060 | SC-013 |
| Dormancy (498–503) | **Not applicable** — derivation in D-9; the structural analogue is FR-021/FR-043 | SC-009 (c) |
| No bit-exact float goldens (504) | — | SC-009, SC-011 (both via `render_fingerprint.h` tolerances) |
| Portability, non-finite handling (505–506) | FR-008, FR-009, FR-024, FR-062 | SC-015, SC-016 |
| Naming conventions (507) | FR-001, FR-050 (`k`-prefixed constants, trailing-underscore members, camelCase methods) | SC-015 |
| Shared-component changes keep Seraphis green (508–510) | FR-070, FR-071, FR-072 | SC-014 |

**Edge Cases are compliance-tracked, not prose.** Each item maps to a named case: RT-safety boundaries
to `SpectralSmear_RenderPathBoundaries`; parameter extremes to `SpectralSmear_ControlClamps` and
`SpectralSmear_MagnitudeMemory`; sample-rate/geometry changes to
`SpectralSmear_SampleRateIndependence` and `SpectralSmear_Geometry`; seed determinism to
`SpectralSmear_SeedDeterminism`.

**Planned test translation units** (all registered by name in `dsp/tests/CMakeLists.txt`'s
`dsp_processors_tests` list, which is enumerated and not globbed):

| TU | Criteria |
|---|---|
| `dsp/tests/unit/processors/spectral_smear_test.cpp` | SC-002 (incl. the `enabled = false` arm), SC-003, SC-007, SC-008, SC-009, SC-011, SC-012 (c), SC-017 + the clamps / geometry / boundaries cases and FR-023's white-box `poleTable()` assertion |
| `dsp/tests/unit/processors/spectral_smear_spectral_test.cpp` | SC-001, SC-004, SC-005, SC-006, SC-010, SC-012 (a)(b) — the `[long]` set |
| `dsp/tests/unit/processors/spectral_smear_perf_test.cpp` | SC-013 — `[.perf]` only |
| `dsp/tests/unit/processors/spectral_smear_nonfinite_test.cpp` | SC-016 only — **the one TU listed in the `-fno-fast-math` block** |

## Assumptions

1. **The component's input is the summed, subharmonic-processed voice bus at roughly −12 dBFS per
   channel.** The roadmap's Global block (lines 74–83) fixes the position; the level is an assumption
   used only to choose test drive levels, and every criterion is stated relative to the drive rather
   than in absolute dBFS wherever that is possible. SC-005 (iii)'s "clamp never engages at −6 dBFS" is
   the one place the assumption is load-bearing, and it is stated at a level 6 dB hotter than the
   nominal one for margin.
2. **The FR-060 projection transfers from `AtmosphereEngine`'s measured blur stage.** Both are stereo
   STFT↔OverlapAdd at 75 % overlap with a per-bin operation; this one writes magnitude as well as
   phase but pays no extra polar conversion for it (`spectral_buffer.h:7-12`). The projection is
   evidence for the budget being reachable, not a substitute for SC-013's measurement.
3. **Phase 10, not this phase, decides the shipped Vorago fog settings.** The defaults in FR-031 and
   FR-050 are a musically plausible starting point chosen so that the shipped state is transparent
   (FR-052), not a tuned patch.
4. **The reference machine for SC-013's baselines is the one Phases 1–3 used** (13th Gen Intel Core
   i9-13900HX, Windows 11, MSVC Release, `build/windows-x64-release`), with the runs performed through
   `node tools/run-cpu-tests.js` so the isolation and 20 s settle rules apply.

## Review notes (revision 2)

Every issue raised in the adversarial review of revision 1 was **accepted**; none was rejected and no
threshold was relaxed to dodge one. Two of the fixes changed a *requirement* rather than a criterion,
so they are called out here because a reader holding revision 1 will not otherwise see them:

1. **FR-021's mapping changed** from `p(k) = amount * poleTable(k)` to a magnitude-domain blend between
   the analysed magnitude and the integrator state. The review demonstrated that the old mapping put
   the entire audible travel of `smearAmount` in the last fraction of a percent below 1.0, which makes
   the roadmap's own "modulation target" framing (line 255) unbuildable and SC-004 (a)'s sweep
   non-discriminating. The arithmetic and the rejected alternatives are in D-12. This is a *tightening*:
   the blend is cheaper, is continuous at zero, keeps the pole a pure function of frequency and tilt,
   and lets SC-004 (a) gain an anti-vacuity clause it could not previously carry.
2. **FR-019 (prepare-time `enabled`) was added.** Revision 1 asserted "there is no hard bypass" with no
   derivation and no decision entry, which would have made Phase 10 pay ~0.2 % of a core and 42.7 ms of
   latency on the global bus for a stage doing nothing. Both prior-art stages ship this switch. D-11
   carries the derivation; SC-002 (d) and SC-008's second arm gate it.

Three fixes replaced a measurement that could not produce the asserted state, rather than a threshold:
SC-002's impulse moved off sample 0 (the periodic Hann window annihilates it there), SC-001's flatness
became a tiled mean plus an analytically predicted out-of-mainlobe energy fraction (the helper analyses
at most 4096 samples from the *start* of its span), and SC-005 (v) became a decay **relative** to the
pre-gap level against `max(tauLow, tauHigh)` (an absolute −80 dBFS floor needs 8.5·tau, longer than the
gap). SC-006 gained the public `kCoherenceMakeup` / `coherenceMakeup()` surface it needs to be
executable at all, SC-017 was added to close FR-013 (a)'s declared silent-failure mode, and FR-046 was
added because FR-042's per-hop-constant make-up gain would have failed SC-012 (c) by ~11.6 dB on a
`decoherence` step.

One deviation from a literal roadmap line (SC-001's re-attribution of the flatness criterion) was
carried as Open Question 3, pending user ratification, and as an explicit DEVIATION row in Traceability.
The user ratified it by amending the roadmap line itself (Clarifications Q4, session 2026-09-11), so it
is now a **satisfied roadmap line**, not a deviation — see the Clarifications section and the resolved
Open Question 3 above.
