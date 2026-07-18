# Innexus Audit — Implementation Plan

> Executor contract: work top-to-bottom, one work item (WI) at a time. This repo is **strict test-first** — for every WI, write the failing test FIRST, run it, confirm it FAILS for the stated reason, THEN make the change, THEN confirm it PASSES. Do not batch. Do not skip the build step. All paths are Windows absolute where load-bearing. Never push. Never `git commit --amend`.
>
> **Two build/test targets are in play.** Most WIs live in `plugins/innexus/` and use `innexus_tests`. WIs whose file lives under `dsp/include/krate/dsp/processors/` (the shared DSP library) build+run under **`dsp_processors_tests`** instead, and their tests go in `dsp/tests/unit/processors/`, NOT `plugins/innexus/tests/`. Each WI states which target applies.
>
> CMake full path on Windows is mandatory: `"C:/Program Files/CMake/bin/cmake.exe"`.

---

## Dated summary table (2026-07-17)

| Category | High | Medium | Low | Total | Status |
|---|---|---|---|---|---|
| Confirmed bugs (correctness) | 5 | 3 | — | 8 | Fix now |
| Confirmed wrong-implementation | 2 | 2 | — | 4 | Fix now |
| Confirmed optimizations | 3 | 4 | — | 7 | Fix (measure before/after) |
| Plausible (skeptic-split) | — | 9 | — | 9 | **Verify first**, then fix or drop |
| Low-severity candidates | — | — | 16 | 16 | Quick sweep (verify each) |
| **Total distinct findings** | 10 | 18 | 16 | **44** | — |

Notes on de-duplication:
- Confirmed residual-synth-prepare-on-audio-thread appears twice in the source list (both cite `processor.cpp:2012`); merged into **WI-3**.
- Confirmed "Long STFT never consumed" (`sample_analyzer.cpp:296`) and Plausible P1 ("Long-window STFT dead path") are the **same defect**; handled once in **WI-1**. Low candidate C5 ("Long-window FFT wasted") is the CPU corollary of the same root cause — folded into WI-1.
- The Evolution/Blend per-sample cluster (confirmed #6 high, #7 medium-opt, #12 high-opt, #4 medium-opt) shares one root fix; unified in **WI-6**.

Counts by confirmed severity map: 10 High findings, 18 Medium, 16 Low. All "High" and "Medium confirmed" items are fix-now; the 9 Plausible items each carry a mandatory re-verify gate (one skeptic already refuted each); the 16 Low candidates are unverified and batched into a single verify-then-fix sweep.

---

# Section 1 — Signal-path reference (executor orientation)

Innexus (AU `aumu`) analyzes audio into per-frame harmonic snapshots (`HarmonicFrame` + `ResidualFrame`) and additively resynthesizes them with evolution / physics / modulation shaping.

**Two input sources → one frame contract**, selected by atomic `inputSource_`:
- **Sample mode** (`≤0.5`): drag-dropped WAV → `SampleAnalyzer` (background `std::thread`) → immutable `SampleAnalysis` published to the audio thread by atomic pointer swap (`currentAnalysis_.exchange`, old ptr → `pendingDeletion_`, freed only in `setActive`/dtor). Voice advances `currentFrameIndex`.
- **Sidechain mode** (`>0.5`): aux bus 0 downmixed to mono → `LiveAnalysisPipeline` **on the audio thread**, one frame per STFT hop.

**Analysis pipeline (both paths share):** `PreProcessingPipeline` (DC removal → 30 Hz HPF → transient suppression → noise gate) → YIN pitch → dual Blackman-Harris STFT (**short 1024/512** = temporal tracking; **long 4096/2048** = low-freq resolution) → `PartialTracker` (Hungarian frame-match + harmonic sieve, sieve runs **only when `f0.voiced`**) → `HarmonicModelBuilder` (dual-timescale smoothing) → residual bands. Offline adds a **two-pass polyphony guard**: if F0 is unstable/noisy, pass 2 re-runs with a fixed F0 and `voiced=false` (sieve OFF) so all peaks survive — this is the *conditional* poly fix; `bandwidth` is set to 0 in pass 2.

**Frame selection** (`voice_.morphedFrame`): Recall │ Capture │ ManualFreeze+morph(lerp) │ confidence-gated freeze (+`SpectralDecayEnvelope`), with per-sample Evolution/Blend overrides, then `applyModulatorAmplitude` → `applyHarmonicPhysics` → `broadcastFrameToVoices` (copy global frame to each active voice).

**Per voice (per-sample):** `HarmonicOscillatorBank` (48 Gordon-Smith MCF sines; freq/amp/**phase** seeded from partials, phase state NOT reset across frames = continuity) + exciter{Impact│Bow│Residual} → resonator{Modal│Waveguide, equal-power crossfade} → `BodyResonance` → `PhysicalModelMixer(residual↔physical)`. Then × freezeRecov × antiClick × velocity × exprVol × pan × ADSRgain.

**Master (per-sample):** Σ voices × `1/√activeCount` → global `SympatheticResonance` → `× masterGain` → tanh cubic soft-limiter → `out[L]/[R]`. Block epilogue captures feedback buffer + `sendDisplayData`.

```
              params (normalized atomics) ── processParameterChanges (per-block)
                                                     │
 MIDI ─► processEvents ─► handleNoteOn ─► VoiceAllocator (poly, L3) ─► initVoiceForNoteOn
                                                     │                        │
 Sidechain aux bus ─► downmix mono ─► [feedback mix] ─► liveAnalysis_         │
        (per-block)                                   │ (HarmonicFrame/Residual @ hop)
 SampleAnalysis (bg thread) ─► currentAnalysis_ ──────┤                        │
                                                      ▼                        ▼
                    FRAME SELECTION (voice_.morphedFrame):
                    Recall│Capture│ManualFreeze+Morph(lerp)│ConfGate+SpectralDecay
                          │  ▲Evolution(per-smp)  ▲Blend(per-smp)
                          ▼
           applyModulatorAmplitude ─► applyHarmonicPhysics ─► broadcastFrameToVoices
                                                                   │ (copy to each voice)
   ┌──────────────────────── PER VOICE (per-sample) ──────────────┼─────────────┐
   │ HarmonicOscillatorBank(48 MCF, freq/amp/phase from partials) │ processStereo→vL,vR
   │            +mod freq mult +detune +stereo spread             │             │
   │ Exciter{Impact│Bow│Residual}.process(feedbackVel) ─► excitation             │
   │ Resonator{Modal│Waveguide (xfade)}.process(excitation) ─► physicalSample    │
   │      └► BodyResonance.process ─► PhysicalModelMixer(res,phys,mix)=monoMix   │
   │ vL = vL*harmLevel + monoMix                                                 │
   │ ×freezeRecov ×antiClick ×velocity ×exprVol ×pan ×ADSRgain ×releaseGain      │
   └──────────────────────────── Σ voices ──────────────────────────────────────┘
                 │ ×1/√activeCount ×srcXfade ×freezeRecov
                 ▼
   + SympatheticResonance(monoSum, L3) ─► ×masterGain ─► softLimit ─► out[L],out[R]
                 │
                 └► feedbackBuffer_ (sidechain, per-block) ; sendDisplayData
```

**Load-bearing invariants (do not break):**
1. **Never globally disable the mono harmonic sieve.** It runs only when `f0.voiced`. Poly/pass-2 disable it *conditionally* via a neutral `voiced=false` F0. Any change touching `partial_tracker.h` must preserve mono-sieve behavior.
2. **Per-partial `bandwidth` (0=sine..1=noise) is the historical noise bug.** Inflated bandwidth injects noise on *every* partial. Physics/modulators touch amplitudes only. Treat any bandwidth write with suspicion; keep it clamped `[0,1]`.
3. **No allocations or frees on the audio thread.** `currentAnalysis_` is acquire/release; superseded analyses go to `pendingDeletion_`.
4. **UI bridge:** `sendDisplayData` piggybacks `DisplayData` on the DataExchange handler (~30 Hz) with a Tier-3 `sharedDisplay_` polling fallback — don't add IMessage loops.

---

# Section 2 — Ordered work items

Build/test command template used throughout (substitute `<Target>`/`<TestName>`):

```bash
# innexus-local WIs:
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target innexus_tests
build/windows-x64-release/bin/Release/innexus_tests.exe "<TestName>*" 2>&1 | tail -5

# shared-DSP WIs (files under dsp/include/krate/dsp/processors/ or /systems/):
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_processors_tests   # or dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "<TestName>*" 2>&1 | tail -5
```

To confirm test-first: run the exe filtered to your new test name and see it FAIL before implementing.

---

## GROUP A — High-severity confirmed bugs (fix first)

### WI-1 — Long (4096) STFT is analyzed but never consumed (dead multi-resolution path)
- **Category/severity:** wrong-implementation / high. Subsumes confirmed #1, plausible P1, low candidate C5.
- **(a) File:line + excerpt:** `plugins/innexus/src/dsp/sample_analyzer.cpp`
  - L206–207 prepare `longSpectrum`; L287 `longStft.pushSamples(...)` every block; L295–297 `if (longStft.canAnalyze() && (shortHopCounter % longHopRatio == 0)) { longStft.analyze(longSpectrum); }`. **`longSpectrum` is never read.** Every downstream consumer uses `shortSpectrum`: L334 `subharmonicValidator.validate(f0, shortSpectrum)`, L338 `tracker.processFrame(shortSpectrum, kShortWindowConfig.fftSize, sampleRate)`. Pass-2 (L463–570) only builds `reShortStft`.
  - `dual_stft_config.h` L42–58 advertises the long window as "high frequency resolution for low-frequency partials (1-4)" citing FR-018..021. `partial_tracker.h:215` peak scan starts at `b=2` → short window (43.07 Hz/bin) has no detectable fundamental below ~86 Hz.
- **DECISION PROCEDURE (fix is genuinely ambiguous — pick before writing the test):**
  1. Ask/decide: *does Innexus need to resolve fundamentals below ~86 Hz (sub-bass/low-bass sources) in the offline sample path?* Check product intent: grep the spec archive `specs/_archive_/` and `plugins/innexus/CLAUDE.md` for FR-018/FR-020 wording and any "40 Hz floor" commitment. The DSP already ships a long-window config and the **live** high-precision path already consumes `longSpectrum_` (`live_analysis_pipeline.cpp:285`).
  2. **If YES (deliver the feature) → Option (a):** wire long-window peaks into low-index partial detection. Feed `longSpectrum` peaks for harmonics n≤4 (and any fundamental below the short-window resolution floor) and merge with short-window peaks above. This is a real SMS multi-resolution merge — additive, frequency-gated, and MUST NOT alter the mono sieve behavior above the floor.
  3. **If NO (short-only is the real design) → Option (b):** delete the long STFT allocation, `pushSamples`, and `analyze()` from the offline analyzer (`sample_analyzer.cpp`), reclaim the wasted 4096-pt FFT every 4 hops, and update the FR-018/020 dual-resolution claim in `dual_stft_config.h` comments + spec docs to state short-only. This resolves C5 automatically.
  - **Default if undecided:** Option (b). It is pure dead-code removal, is provably output-neutral for all sources the analyzer already handles, and carries the lowest risk. Only choose (a) if there is an explicit low-f0 requirement.
- **(b) Failing test FIRST:**
  - Option (b) test — `plugins/innexus/tests/integration/analysis_characterization_tests.cpp` (new). Analyze a synthetic mid-range harmonic WAV (e.g. 220 Hz + 5 harmonics, 1 s, generated in-test) through `SampleAnalyzer`, capture the resulting `frames[].partials[].frequency/amplitude`. Assert the analysis completes and partial-1 frequency ≈ 220 Hz within 1 bin. This is a **characterization** test: it must pass identically before and after the deletion (proving output-neutrality). Write it, confirm it PASSES on current code, then after the deletion confirm it STILL passes (regression guard). Name: `INNEXUS_AnalysisLongWindowRemoval_MidRangeUnchanged`.
  - Option (a) test — `dsp/tests/unit/processors/partial_tracker_tests.cpp` or a new `plugins/innexus/tests/integration/` test: synthesize an 80 Hz fundamental + harmonics, assert `frames[].partials[0].harmonicIndex == 1` and `partials[0].frequency ≈ 80 Hz ± 11 Hz` (long-window bin) — this FAILS today (fundamental below short-window floor is dropped). Name: `INNEXUS_LowFundamental80Hz_Resolved`.
- **(c) Change:** per chosen option above.
- **(d) Build+test:** `--target innexus_tests`; run `innexus_tests.exe "INNEXUS_AnalysisLongWindowRemoval*"` (or `"INNEXUS_LowFundamental80Hz*"`).
- **(e) Risk:** Option (a) touches the load-bearing sieve input — keep the merge additive and gated to n≤4/low-f0 so mono-sieve output above the floor is byte-identical; re-run all `partial_tracker_tests` + Innexus integration goldens. Option (b) must NOT remove the *short* STFT. Neither option may touch `bandwidth`.
- **Theory:** Serra & Smith SMS multi-resolution STFT; Gabor uncertainty (JOS SASP). f0=n·f0 comb / low-f0 resolution — Smith PASP F0 estimation.

### WI-2 — Envelope steady-state thresholds applied to raw (unnormalized) RMS contour
- **Category/severity:** bug / high.
- **(a) File:line + excerpt:** `plugins/innexus/src/dsp/envelope_detector.h`
  - L53 `contour[i] = frames[i].globalAmplitude;` (raw source RMS, `sqrt(rmsSum/N)` from `sample_analyzer.cpp:314`, never peak-normalized).
  - L57 `peakAmp` already computed (used for `sustainLevel` at ~L227 but NOT for the gate).
  - L180–181 `bool isSteady = (std::abs(slope) < kSlopeThreshold) && (variance < kVarianceThreshold);` with `kSlopeThreshold=5e-4` (L271), `kVarianceThreshold=2e-3` (L275). Slope scales ~linearly and variance ~quadratically with level → gate fires inconsistently across recording levels.
- **(b) Failing test FIRST:** `plugins/innexus/tests/unit/processor/test_envelope_detector.cpp` (existing). Build a synthetic ADSR contour (attack→decay→flat sustain→release), run `EnvelopeDetector::detect`, record detected `decayMs`/`sustainStartFrame`. Then scale the identical contour by 0.1× (quieter) and by 10× (louder) and `detect` again. Assert the detected `decayMs`, `sustainLevel` ratio, and `sustainStartFrame` are equal (within a tight tolerance) across all three scalings. This FAILS today (level-dependent). Name: `INNEXUS_EnvelopeDetector_ScaleInvariantSteadyState`.
- **(c) Change:** Before the slope/variance loop, normalize the contour by `peakAmp`: `contourNorm[i] = contour[i] / std::max(peakAmp, 1e-9f)` (or work in dB: `20*log10(max(amp, floor))`). Apply the gate to the normalized contour. Keep `kSlopeThreshold`/`kVarianceThreshold` (now relative on a `[0,1]` contour they are far more defensible). Do not change `sustainLevel` (already peak-relative).
- **(d) Build+test:** `--target innexus_tests`; `innexus_tests.exe "INNEXUS_EnvelopeDetector_ScaleInvariant*"`.
- **(e) Risk:** Analysis-thread-only; does not touch the sieve or bandwidth. Existing envelope tests use contours peaking ≈1.0, so normalization is ~no-op for them — verify they still pass.
- **Theory:** Absolute slope/variance thresholds are meaningful only on a peak-normalized/dB contour (JOS/PASP sinusoidal modeling: fit log-amplitude; ISO 3382 / Schroeder fit a relative dB region).

### WI-3 — `residualSynth.prepare()` heap-allocates on the audio thread on sample load
- **Category/severity:** bug / high. Merges confirmed #5 and #18 (same `processor.cpp:2012`).
- **(a) File:line + excerpt:** `plugins/innexus/src/processor/processor.cpp`
  - `checkForNewAnalysis()` runs on the audio thread (called from `process()` L352). L2010–2016:
    ```cpp
    for (auto& voice : voices_) {
        voice.residualSynth.prepare(
            result->analysisFFTSize, result->analysisHopSize,
            static_cast<float>(sampleRate_));
    }
    ```
  - `ResidualSynthesizer::prepare` (`residual_synthesizer.h:52–73`) calls `noiseBuffer_.resize`, `envelopeBuffer_.resize`, `outputBuffer_.resize`, **`fft_.prepare`**, `overlapAdd_.prepare`, `spectralBuffer_.prepare`. `FFT::prepare` (`fft.h:147–167`) allocates **unconditionally** (`pffft_new_setup` + 3 aligned buffers) even at unchanged size. Fires ×8 voices on every sample load while transport runs. The same file documents the invariant it violates at L377–378 ("No prepare() call here to avoid heap allocation on audio thread (FR-008)").
  - Fixed sizes: `kShortWindowConfig` is `{1024,512}` (`dual_stft_config.h:54`); analyzer hardcodes `analysisFFTSize/analysisHopSize` to those (`sample_analyzer.cpp:250–251`); `setActive()` already prepares each voice's residual synth to those exact sizes (`processor.cpp:140–156`). So the per-analysis re-prepare is redundant.
- **(b) Failing test FIRST:** `plugins/innexus/tests/integration/residual_integration_tests.cpp` (existing). Install a scoped allocation counter (override or a probe) OR — simpler and portable here — assert the re-prepare is skipped: add a test that (1) constructs the processor, calls `setActive(true)` (prepares residual synths), (2) publishes a completed `SampleAnalysis` carrying residual frames with `analysisFFTSize==1024, analysisHopSize==512`, (3) drives one `process()` block, and asserts `voice.residualSynth` was NOT re-prepared during `process()`. Expose a test hook: a monotonically-incrementing `prepareCallCount()` on `ResidualSynthesizer` (test-only accessor) or a `wasPreparedThisBlock` flag; assert it does not increment when the incoming sizes equal the already-prepared sizes. This FAILS today. Name: `INNEXUS_Residual_NoAudioThreadReprepareOnLoad`.
- **(c) Change:** In the `checkForNewAnalysis()` loop, guard the `prepare()` call so it is skipped when the incoming `analysisFFTSize/analysisHopSize` equal the sizes the voice's residual synth is already prepared to (track `preparedFftSize_/preparedHopSize_` in `ResidualSynthesizer` and expose an `isPreparedFor(fft,hop)` query, or compare against a processor-cached pair). Because the analyzer always emits `kShortWindowConfig`, this makes the audio-thread `prepare()` a no-op in practice. If a differing size ever appears, defer preparation off the audio thread (prepare on the background analysis thread before publication, or via a message), and only swap in the ready object.
- **(d) Build+test:** `--target innexus_tests`; `innexus_tests.exe "INNEXUS_Residual_NoAudioThreadReprepare*"`.
- **(e) Risk:** Residual/noise synth only — orthogonal to the mono sieve and to `bandwidth`. Do not remove the `setActive()` preparation. Ensure the size-equality guard is exact (both fft AND hop) so a genuine size change still re-prepares (off-thread).
- **Theory:** RT-safety: no heap alloc on the audio thread (project constitution / dsp-architecture skill).

### WI-4 — Controller `setComponentState` omits Sympathetic Resonance params → stream desync + broken instance-ID trailer
- **Category/severity:** bug / high.
- **(a) File:line + excerpt:** `plugins/innexus/src/controller/controller_state.cpp`
  - Body Resonance block ends L508–511 (`setParamNormalized(kBodyMixId, ...)`), then L513 jumps straight to the instance-ID trailer (`readInt32(marker) && marker == kInstanceIdMarker`) with **no `readFloat` for the two Sympathetic floats**.
  - Processor writes them (`processor_state.cpp:243–249`): `writeFloat(sympatheticAmount_); writeFloat(sympatheticDecay_); writeInt32(kInstanceIdMarker); writeInt64(instanceId_);`. Processor `setState` reads them correctly (`processor_state.cpp:703–706`). Consequences: (1) `kSympatheticAmountId`/`kSympatheticDecayId` revert to defaults in the UI on every reload; (2) controller stream is 8 bytes ahead → `readInt32(marker)` consumes `sympatheticAmount`'s bit pattern, marker check fails, `SharedDisplayBridge` instance-ID (Tier-3 fallback) never re-links.
- **(b) Failing test FIRST:** `plugins/innexus/tests/unit/vst/controller_state_tests.cpp` (create if absent, else add to existing controller state test). Set the processor's sympathetic params to non-default (e.g. amount=0.7, decay=0.4), call `Processor::getState(stream)`, rewind, call `Controller::setComponentState(stream)`. Assert `controller.getParamNormalized(kSympatheticAmountId) == 0.7f` and `kSympatheticDecayId == 0.4f` (within margin), AND assert the instance-ID trailer was consumed (expose a controller flag `instanceIdLinked()` or check that the display bridge id matches). Both FAIL today. Name: `INNEXUS_ControllerState_RestoresSympatheticAndInstanceId`.
- **(c) Change:** In `setComponentState`, immediately after the Body Resonance block and BEFORE the instance-ID trailer read, mirror the processor:
  ```cpp
  float v = 0.0f;
  if (streamer.readFloat(v)) setParamNormalized(kSympatheticAmountId, std::clamp(v, 0.0f, 1.0f));
  if (streamer.readFloat(v)) setParamNormalized(kSympatheticDecayId,  std::clamp(v, 0.0f, 1.0f));
  ```
- **(d) Build+test:** `--target innexus_tests`; `innexus_tests.exe "INNEXUS_ControllerState_RestoresSympathetic*"`.
- **(e) Risk:** Pure additive read in the controller restore path; mirrors the already-correct processor path. Does not touch DSP/sieve. Confirm the write order in `processor_state.cpp` is exactly Body → sympatheticAmount → sympatheticDecay → marker → id before mirroring (read it, don't assume).
- **Theory:** VST3 processor/controller state must use identical stream layout on both sides.

### WI-5 — MCF oscillator effective epsilon can exceed the `|eps|<2` stability bound after detune/modulator multiply → divergence to Inf/NaN
- **Category/severity:** bug / high. **Shared DSP — uses `dsp_processors_tests`.**
- **(a) File:line + excerpt:**
  - SIMD kernel `dsp/include/krate/dsp/processors/harmonic_oscillator_bank_simd.cpp:97–98`: `const auto vEpsEff = hn::Mul(vEps, vDetune); const auto vSinNew = hn::MulAdd(vEpsEff, vCos, vSin);` — no clamp on the product; also the SIMD scalar tail ~L118.
  - Scalar/bandwidth path `harmonic_oscillator_bank.h:598`: `float eps = epsilon_[i] * detuneMultiplier_[i];` (and the fade path ~L629) — no clamp on the product.
  - The ONLY clamp is on the *base* coefficient: `harmonic_oscillator_bank.h:886` `epsilon_[i] = std::clamp(eps, -kMaxEpsilon, kMaxEpsilon);` with `kMaxEpsilon=1.99` (also `recalculateSourceFrequencies` ~L870). `detuneMultiplier_` is built from `std::pow(2, offsetCents/1200)` (`offsetCents = detuneSpread*harmIdx*kDetuneMaxCents`, ~L968–970) and further `*= multipliers[i]` from modulators (~L548). Anti-alias gain uses the UN-detuned frequency (`recalculateAntiAliasing`), so a diverging partial is not amplitude-suppressed; output clamp `kOutputClamp` and `std::clamp` do NOT stop NaN.
  - Trigger: `kDetuneSpreadId=630` (user knob, range 0.0–1.0). At spread=1, harmonic index ~40+ gives detune ≈1.41×; a partial near 11 kHz has base epsilon ≈1.49 → `eps_eff ≈ 2.24 > 2` → eigenvalue >1 → state → Inf in ~2 ms.
- **(b) Failing test FIRST:** `dsp/tests/unit/processors/harmonic_oscillator_bank_tests.cpp` (existing). Load a frame with a high-harmonic partial (e.g. `harmonicIndex ~40–48`, frequency ~11–12 kHz at 44.1 kHz, nonzero amplitude), set `setDetuneSpread(1.0f)`, process ~2000 samples of `processStereo`, and assert every output sample AND the internal state is finite (`std::isfinite`). Add a second case at 48 kHz. FAILS today (Inf/NaN). Name: `HarmonicOscillatorBank_DetuneEpsilonStability_HighHarmonicFinite`. (Build non-finite-safe assertions per the repo's fast-math note — check `std::isfinite` via bit test if compiled under fast-math.)
- **(c) Change:** Clamp the **effective** coefficient at every synthesis site, not just the base. In both scalar loops: `float eps = std::clamp(epsilon_[i] * detuneMultiplier_[i], -1.99f, 1.99f);`. In the SIMD kernel: `hn::Clamp(vEpsEff, kNegLimit, kPosLimit)` (broadcast ±1.99). Preferred/robust: recompute `epsilon_` from the fully-detuned frequency and clamp once whenever detune/modulator multipliers change, and derive the anti-alias gain from the *detuned* frequency too — so a partial driven past Nyquist by detune is also amplitude-faded. Choose the per-site clamp as the minimal safe fix if the recompute is too invasive for one pass; either restores finiteness.
- **(d) Build+test:** `--target dsp_processors_tests`; `dsp_processors_tests.exe "HarmonicOscillatorBank_DetuneEpsilonStability*"`.
- **(e) Risk:** Both SIMD and scalar paths must be fixed identically (they run on different builds/ISAs — verify with the existing stereo/detune tests). Clamping only activates in the >2 regime, so existing finite-output detune tests (low harmonics) are unaffected — confirm they still pass. Does not touch the analysis sieve. Do NOT alter `bandwidth`.
- **Theory:** Gordon-Smith / coupled-form quadrature oscillator stability: `epsilon = 2·sin(π·f/fs)` must keep `|eps|<2` for unit-circle eigenvalues (Vicanek 2015 "A New Recursive Quadrature Oscillator"; JOS PASP magic-circle).

### WI-6 — Evolution/Blend rerun the full frame-load pipeline (incl. residual STFT) every SAMPLE → CPU overrun/dropouts
- **Category/severity:** wrong-implementation/high (dropouts) — **subsumes confirmed #6 (high), #12 (opt/high), #7 (opt/medium), #4 (opt/medium).** This single WI fixes the correctness (dropout) issue AND delivers the four optimization findings. See Section 3 for the measurement gate.
- **(a) File:line + excerpt:** `plugins/innexus/src/processor/processor.cpp`
  - Per-sample loop opens L1325. Normal sample-mode path gates reload to hop boundaries (L1332 `if (frameSampleCounter >= hopSizeInSamples)`). **Evolution (L1405–1445) and Blend (L1463–1513) do NOT** — they call `broadcastFrameToVoices(activePartialCount, /*loadResidual=*/true)` at L1444 (evolution) and L1512 (blend) **every sample**.
  - `evolutionEngine_.getInterpolatedFrame` (`evolution_engine.h:163–210`) returns true whenever `numWaypoints_ >= 2` (no hop gate), and calls `recallSnapshotToFrame` TWICE (L189, L194) + `lerpHarmonicFrame`/`lerpResidualFrame` over ≤96 partials.
  - `broadcastFrameToVoices` (L2118–2189) per active voice: full 96-partial frame copy, `oscillatorBank.loadFrame` (→ `recalculateFrequencies` ≤96 `sin`, `recalculateAntiAliasing` ≤96 `cos`, sumSq normalize), `modalResonator.updateModes` (per-mode trig/sqrt), and L2186 `residualSynth.loadFrame` (fftSize noise gen + forward FFT + envelope interp + overlap-add = full STFT resynth). Residual model is hop-rate (~86–94 Hz); driving it at 44.1/48 kHz is ~500× inflation. In-code comment L1417 "loadFrame is cheap" is false.
- **(b) Failing test FIRST (two assertions):** `plugins/innexus/tests/integration/evolution_blend_hoprate_tests.cpp` (new).
  1. Behavioral/correctness guard: with ≥2 evolution waypoints enabled, process N blocks and assert the audio output is *smooth and continuous* (no discontinuity spikes) AND — the real regression hook — expose a per-block counter of `residualSynth.loadFrame` calls; assert it equals roughly `numSamples / hopSizeInSamples` per active voice, NOT `numSamples`. This FAILS today (counter == numSamples). Name: `INNEXUS_Evolution_ResidualLoadFrameAtHopRate`.
  2. Equivalent for Blend: `INNEXUS_Blend_ResidualLoadFrameAtHopRate`.
  - Add a test-only accessor `residualLoadFrameCallCount()` (or a broadcast counter on the processor) to make the rate observable.
- **(c) Change:** Introduce a hop-boundary gate for the Evolution and Blend paths mirroring the normal sample-mode path. Per sample, advance the evolution/blend *phase* and the per-sample smoothers/oscillator amplitudes, but only rebuild+broadcast the frame (snapshot recall / lerp / physics / modulator amplitude / `broadcastFrameToVoices` incl. `residualSynth.loadFrame`) when `frameSampleCounter >= hopSizeInSamples` (reset counter, matching L1329–1401). Between rebuilds the MCF bank sustains itself and the amplitude one-pole smoothers cover sub-frame smoothing. For finding #4's double-recall specifically: additionally cache the two reconstructed waypoint frames keyed on `(slotA, slotB)` and only re-run `recallSnapshotToFrame` when the pair changes — but the hop-gate alone already removes the dominant cost.
- **(d) Build+test:** `--target innexus_tests`; `innexus_tests.exe "INNEXUS_Evolution_ResidualLoadFrameAtHopRate*"` then `"INNEXUS_Blend_ResidualLoadFrameAtHopRate*"`.
- **(e) Risk:** This path is synthesis-side, disjoint from the mono sieve — no sieve risk. Hop-gating changes bit-exact output only at the sub-frame smoothing granularity; if evolution/blend approval goldens exist they may need regeneration — treat any golden diff as EXPECTED-and-review, not a silent break. Confirm evolution phase still advances per sample (only the frame *rebuild* is gated). Do not gate the normal sample-mode path (already gated) or the oscillator `processStereo` (must stay per-sample).
- **Theory:** McAulay-Quatieri frame interpolation is at analysis-frame rate; between frames the oscillator bank interpolates parameters. Residual (SMS stochastic) is a hop-rate overlap-add process (Serra SMS; Allen-Rabiner/Griffin-Lim OLA).

### WI-7 — Waveguide resonance note-on broken: 2-option param denormalized as 3-way (`*2.0f`)
- **Category/severity:** bug / high.
- **(a) File:line + excerpt:** `plugins/innexus/src/processor/processor_midi.cpp:379–380`
  ```cpp
  const float resTypeNorm = resonanceType_.load(...);
  const int resType = std::clamp(static_cast<int>(std::round(resTypeNorm * 2.0f)), 0, 2);
  ```
  `kResonanceTypeId` is a 2-entry StringListParameter (Modal=0/Waveguide=1, `controller.cpp:789–794`), stored clamped `[0,1]`. At Waveguide (norm=1.0) this yields `resType=2`. `initVoiceForNoteOn` then sets out-of-range `activeResonanceType_=2` and the pluck gate `if (resonanceType == 1 && !isRetrigger) voice.waveguideString.noteOn(f0, velocity);` (L327/L332) is FALSE — the string is never plucked/retuned (stays at stale/default 440 Hz). Meanwhile `process()` uses `std::clamp(round(resTypeNorm),0,1)` = 1 (`processor.cpp:892`), causing a spurious modal→waveguide crossfade every note-on (`processor.cpp:1289`).
- **(b) Failing test FIRST:** `plugins/innexus/tests/integration/test_physical_model_output_levels.cpp` (existing waveguide tests live here). Set `kResonanceTypeId` normalized to 1.0 (Waveguide), send NoteOn at a known pitch (e.g. MIDI 60 → 261.6 Hz), and assert `voice.waveguideString` was plucked/retuned: expose/assert `waveguideString.frequency()` ≈ 261.6 Hz (not the 440 Hz default), and/or assert the voice produces nonzero, pitched output at the expected fundamental. Also assert `voice.activeResonanceType_ == 1` (in range). FAILS today (freq==default, activeResonanceType_==2). Name: `INNEXUS_WaveguideNoteOn_PlucksAndRetunes`.
- **(c) Change:** Match `process()`'s 2-option denormalization: `const int resType = std::clamp(static_cast<int>(std::round(resTypeNorm)), 0, 1);` (drop `*2.0f`).
- **(d) Build+test:** `--target innexus_tests`; `innexus_tests.exe "INNEXUS_WaveguideNoteOn_Plucks*"`.
- **(e) Risk:** None to the sieve/bandwidth. Confirm no OTHER caller relies on the buggy `*2.0f` for `resonanceType_` (grep `resonanceType_` / `activeResonanceType_`); only this note-on site is wrong (the 3-way `*2.0f` pattern is correct for the genuinely-3-entry `exciterType`/`voiceMode` lists — do not touch those).
- **Theory:** VST3 StringListParameter denormalization: `index = round(norm * (count-1))`; count=2 → `round(norm)`, not `round(norm*2)`.

---

## GROUP B — Medium-severity confirmed (correctness / wrong-implementation)

### WI-8 — Loris bandwidth noise not variance-normalized → noisy partials lose energy instead of becoming noise
- **Category/severity:** wrong-implementation / medium. **Shared DSP — `dsp_processors_tests`.**
- **(a) File:line + excerpt:** `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h:605`
  `ampMod = std::sqrt(1.0f - bw) + noise * std::sqrt(2.0f * bw);` where `noise = nextFilteredNoise(i)`. `nextFilteredNoise()` (L822–841) returns a uniform `[-1,1]` LCG (variance 1/3) through a 2-stage Chebyshev LP at 500 Hz with NO post-filter variance/gain normalization; filtered variance ≈ `(1/3)*(500/22050) ≈ 0.008` vs the model's required `E[noise²]=0.5`. Result `E[ampMod²] ≈ 1 − 0.98·bw` → bw→1 partials attenuate ~18 dB instead of becoming equal-energy noise.
- **(b) Failing test FIRST:** `dsp/tests/unit/processors/harmonic_oscillator_bank_tests.cpp` (existing has a bandwidth section ~L558). New case: load a single partial with `bandwidth=1.0`, amplitude 1.0, process a long block, measure output RMS/energy. Assert the total energy is within a tolerance band of the same partial at `bandwidth=0.0` (energy preservation, per the Loris model) — e.g. RMS ratio in `[0.7, 1.4]`. FAILS today (bw=1 RMS ≈ 0.14× of bw=0). Name: `HarmonicOscillatorBank_BandwidthEnergyPreserved`.
- **(c) Change:** Normalize the filtered noise to the model's target variance. Precompute the 2-stage LP cascade's noise-power gain (measure cascade RMS for a unit-variance white input at construction/prepare, once) and divide `nextFilteredNoise()` output by that RMS so `E[noise²] ≈ 0.5` — equivalently multiply the `sqrt(2*bw)` term by the compensating factor. Then `E[ampMod²] ≈ 1`. Keep `bw` clamped `[0,1]` (see WI-C8).
- **(d) Build+test:** `--target dsp_processors_tests`; `dsp_processors_tests.exe "HarmonicOscillatorBank_BandwidthEnergy*"`.
- **(e) Risk:** Only affects partials with `bw>1e-4`; clean harmonics (`bw≈0`, `ampMod≈1`) — the load-bearing mono path — are untouched. The existing LP-rolloff test asserts a relative near/far sideband ratio (invariant under a uniform noise gain) and the independence test asserts `rms>1e-4` — both preserved/strengthened; confirm they still pass. Do NOT inflate `bandwidth` itself (historical noise bug) — this changes the *noise gain*, not the per-partial bandwidth value.
- **Theory:** Fitz & Haken / Loris bandwidth-enhanced oscillator: `carrier √(1−bw) + noise √(2·bw)` is energy-preserving only when `E[noise²]=0.5` (Fitz-Haken 1995 ICMC; Fitz-Haken 2002 ICMC; association.pdf). Loris normalizes its modulator variance; this code does not.

### WI-9 — Raw processor atomic pointers shipped to controller via IMessage and dereferenced on UI thread
- **Category/severity:** wrong-implementation / medium (latent UAF + out-of-process crash).
- **(a) File:line + excerpt:** `plugins/innexus/src/processor/processor.cpp:480–488` sends `reinterpret_cast<intptr_t>(&adsrEnvelopeOutput_)` (and `&adsrStage_`, `&adsrActive_`) as IMessage int64 attributes. `controller.cpp:1389–1405` reinterpret_casts them back to `std::atomic<...>*` and the ADSR display `->load()`s them on the UI thread (`shared/src/ui/adsr_display.h:~1794`). Atomics updated on audio thread at `processor.cpp:1948–1950`. Violates process separation (VST3 permits out-of-process controller); latent use-after-free on teardown (no invalidation).
- **(b) Failing test FIRST:** This is a design/architecture correction; assert the pointer path is gone and the values still arrive as scalars. `plugins/innexus/tests/unit/vst/adsr_display_bridge_tests.cpp` (new). Drive the processor to produce a known ADSR envelope value/stage/active, run a `process()` + `sendDisplayData` cycle, and assert the controller-visible ADSR display values match the scalar payload — WITHOUT any `outputPtr`/`stagePtr`/`activePtr` IMessage attribute being sent (assert the IMessage the processor emits carries the scalar float/int values, not pointer ints). Write the assertion against the intended scalar channel; it FAILS today because the values travel as pointers. Name: `INNEXUS_AdsrDisplay_ScalarNotPointer`.
- **(c) Change:** Stop transmitting pointers. Add `adsrEnvelopeOutput`/`adsrStage`/`adsrActive` scalars to the existing per-block `DisplayData` block carried by `sendDisplayData` (the DataExchange/MetersBlock bridge the plugin already uses) — the controller copies them each block. Remove the `outputPtr/stagePtr/activePtr` IMessage send and the controller-side reinterpret_cast + `setPlaybackStatePointers`. Keep processor-owned atomics private.
- **(d) Build+test:** `--target innexus_tests`; `innexus_tests.exe "INNEXUS_AdsrDisplay_Scalar*"`.
- **(e) Risk:** UI-display path only; disjoint from DSP/sieve. Preserve the ~30 Hz throttle and the Tier-3 `sharedDisplay_` fallback (add the three scalars there too so the fallback still shows ADSR). Verify the editor-lifecycle harness still passes under ASan if available (this removes a real teardown UAF).
- **Theory:** VST3 processor/controller may live in separate processes; IMessage carries copied data, not shared-memory handles.

### WI-10 — Modulator Target selector (Amplitude/Frequency/Pan) has no effect — all three always applied
- **Category/severity:** wrong-implementation / medium.
- **(a) File:line + excerpt:** `plugins/innexus/src/dsp/harmonic_modulator.h` — `target_` is only written (`setTarget` L122–125, init L281) and never read. `applyAmplitudeModulation` (L154), `getFrequencyMultipliers` (L180), `getPanOffsets` (L209) each gate only on `rangeStart_ > rangeEnd_ || depth_ <= 0.0f`, never on `target_`. `processor.cpp` runs all three unconditionally when a modulator is enabled: `applyModulatorAmplitude → mod1_.applyAmplitudeModulation` (L2110), plus per-sample `getFrequencyMultipliers`+`applyExternalFrequencyMultipliers` and `getPanOffsets`+`applyPanOffsets` (L1552–1561). `mod1Target_`/`mod2Target_` are loaded/stored (L1230–1232) but never consulted.
- **(b) Failing test FIRST:** `plugins/innexus/tests/unit/processor/test_harmonic_modulator.cpp` (existing). Three sub-cases on one `HarmonicModulator` with depth>0:
  - `setTarget(Frequency)`: assert `applyAmplitudeModulation` leaves amplitudes UNCHANGED and `getPanOffsets` returns all-zero, while `getFrequencyMultipliers` is non-trivial.
  - `setTarget(Amplitude)`: assert `getFrequencyMultipliers` returns all-1.0 and `getPanOffsets` all-zero.
  - `setTarget(Pan)`: assert amplitude unchanged and freq multipliers all-1.0.
  All three FAIL today (every applicator always acts). Name: `INNEXUS_Modulator_TargetGatesEffect`.
- **(c) Change:** Gate each applicator on `target_`: return early / identity from `applyAmplitudeModulation` unless `target_==Amplitude`; from `getFrequencyMultipliers` (fill 1.0) unless `target_==Frequency`; from `getPanOffsets` (fill 0.0) unless `target_==Pan`. (Equivalently gate the three calls in the processor on `mod1Target_`/`mod2Target_` — but gating inside `HarmonicModulator` is the single point of truth and keeps existing unit-test call patterns valid.)
- **(d) Build+test:** `--target innexus_tests`; `innexus_tests.exe "INNEXUS_Modulator_TargetGates*"`.
- **(e) Risk:** Existing unit tests pair `setTarget(X)` with the matching applicator, so gating leaves them green — verify. `reset_trap_tests.cpp` asserts `setTarget` has no output effect in an Amplitude context — consistent with the fix. No sieve/bandwidth impact.

---

## GROUP C — Plausible findings (one skeptic already refuted each → VERIFY FIRST, then fix or drop)

> For every WI here: FIRST reproduce the defect against current code (read the cited lines, and where feasible write the failing test and confirm it fails as described). If it does NOT reproduce, or the effect is genuinely inconsequential, STOP and record "verified-not-actionable" — do not force a change. The refuting verdict's reasoning is summarized as the counter-argument to weigh.

### WI-11 — [verify first] `inputRms` on incompatible scales: time-domain (offline) vs raw spectral magnitude (live)
- **File:line:** `plugins/innexus/src/dsp/live_analysis_pipeline.cpp:303–316` (spectral-magnitude RMS, comment says "approximation") vs `sample_analyzer.cpp:311–315` (time-domain `sqrt(mean(audio²))`). Both feed the same `HarmonicModelBuilder.build()`.
- **Counter-argument (refuter):** `globalAmplitude` is never an absolute output multiplier (synthesis level comes from normalized per-partial amps); `noisiness` (the only inputRms²-scaled field) is consumed only in offline snapshot selection; the model-builder relative gate keys off peak *partial* amplitude, not inputRms. So the divergence may be inconsequential.
- **Verify step:** Grep every consumer of `frame.globalAmplitude` and `frame.noisiness` in the synthesis path (processor + `innexus_voice.h` + dsp). Confirm whether any *audible or host-visible* behavior depends on the live-path `globalAmplitude`/`noisiness` absolute scale (check `harmonic_blender.h:178` blend of live vs snapshot globalAmplitude, `processor.cpp:1104–1106` recovery ratio, meters/display).
- **If reproduces (audible/host-visible divergence found):**
  - **(b) Test:** `plugins/innexus/tests/integration/analysis_scale_parity_tests.cpp` (new). Feed identical audio through offline `SampleAnalyzer` and through `LiveAnalysisPipeline`; assert `globalAmplitude` (and `noisiness`) of matched frames agree within tolerance. FAILS today.
  - **(c) Change:** Make the live-path `inputRms` a time-domain hop RMS (small accumulator), OR scale the spectral RMS by `2/(N·CG)` + Parseval correction so both hand the model builder a consistent scale.
- **If not:** record verified-not-actionable.
- **(d) Build+test:** `--target innexus_tests`; `innexus_tests.exe "INNEXUS_AnalysisScaleParity*"`.
- **(e) Risk:** Live path only; do not perturb offline (correct) side. Watch for existing integration tests asserting on live `globalAmplitude`/`noisiness` — re-baseline only if intentionally changing them.
- **Theory:** amplitude recovery `A = 2|X|/(N·CG)` (JOS SASP); Parseval scaling by N and window energy.

### WI-12 — [verify first] Harmonic sieve tolerance has no absolute-resolution floor; low fundamentals can be rejected
- **File:line:** `dsp/include/krate/dsp/processors/partial_tracker.h:336–337` — `tolerance = 0.06f * sqrt(n) * f0` with no `max(relTol, ~1 bin)` term. Short window 43.07 Hz/bin; at f0=80 Hz the n=1 window is 4.8 Hz ≪ bin. **Shared DSP — `dsp_processors_tests`.**
- **Counter-argument:** Peak scan starts at b=2 so a 40 Hz fundamental is never a candidate anyway (resolution limit, not sieve rejection); once born, a partial persists via Hungarian matching; `subharmonicValidator` aligns YIN to the spectrum first. Magnitude of the parabolic vs YIN disagreement at low f0 is unproven.
- **Verify step:** Construct an 80 Hz harmonic signal, run the tracker+sieve on the short spectrum, and check whether partial-1 gets `harmonicIndex==0` (rejected/not-born) across frames. Compute the actual parabolic peak-frequency near-DC bias vs the 4.8 Hz window.
- **If reproduces (fundamental dropped):**
  - **(b) Test:** `dsp/tests/unit/processors/partial_tracker_tests.cpp`. Synthesize 80 Hz + harmonics, voiced F0=80, assert partial with `harmonicIndex==1` is born and retained. FAILS if dropped. Name: `PartialTracker_LowF0_FundamentalNotRejected`.
  - **(c) Change:** `tolerance = std::max(0.06f*sqrt((float)n)*f0, kBinFloor*binSpacing)` with `kBinFloor ≈ 1.0–1.5`, while keeping `tolerance < 0.45f*f0` (half-spacing invariant preserved).
- **(d) Build+test:** `--target dsp_processors_tests`; `dsp_processors_tests.exe "PartialTracker_LowF0*"`.
- **(e) Risk:** This is the mono-sieve path — the floor only *widens* the birth window at low f0 (never disables the sieve, never narrows above the floor), so currently-passing assignments cannot be dropped. Re-run ALL `partial_tracker_tests`. Preserve the half-spacing bound to avoid mis-assigning adjacent harmonics.
- **Theory:** Duifhuis/Goldstein harmonic sieve needs an absolute (≈1 bin) tolerance floor plus relative + order-dependent terms.

### WI-13 — [verify first] Sieve inharmonicity tolerance grows as √n, far under physical stiff-string ~n³
- **File:line:** `partial_tracker.h:317/337` — `tolerance = kBaseToleranceFactor * sqrt(n) * f0`; stiff-string deviation grows ~`(B/2)·f0·n³`. For B=1e-3, n=10 deviates ~0.49·f0 vs a 0.19·f0 window → stiff upper partial rejected. **Shared DSP — `dsp_processors_tests`.**
- **Counter-argument:** The √n window tolerates B up to ~4e-4 for n≤10 (real pianos in bass/mid sit below that); strongly inharmonic idiophones read YIN-unvoiced and bypass the sieve; widening risks mis-mapping near-harmonic sources; the proper fix (per-source B estimation) is a large rewrite of the load-bearing path.
- **Verify step:** Synthesize a stiff-string spectrum (B≈1e-3, voiced), check whether high-index partials (n≈8–12) get `harmonicIndex==0`/dropped. Confirm they aren't already routed to the sieve-OFF pass-2.
- **If reproduces (upper partials dropped for a realistic voiced stiff source):**
  - **(b) Test:** `dsp/tests/unit/processors/partial_tracker_tests.cpp`. Stiff-string peaks at `n·f0·sqrt(1+B·n²)`, voiced, assert n=10 partial is assigned/born. FAILS today. Name: `PartialTracker_StiffString_UpperPartialsRetained`.
  - **(c) Change (DECISION):** Prefer estimating B from the first few tracked partials and stretching the template `harmonicFreq = n*f0*sqrt(1+B*n²)`. If that is too invasive for one pass, add a super-linear widening term `tolerance = f0*(kRel + kStretch*n*n)` clamped below the half-spacing bound. Choose the template-stretch if B is reliably estimable; otherwise the clamped n² widening.
- **(d) Build+test:** `--target dsp_processors_tests`; `dsp_processors_tests.exe "PartialTracker_StiffString*"`.
- **(e) Risk:** HIGH sensitivity — this is the load-bearing mono sieve. Any widening MUST stay below half-harmonic-spacing to avoid mis-mapping partials to wrong indices for near-harmonic sources. Re-run all `partial_tracker_tests` and Innexus analysis goldens; treat any near-harmonic assignment change as a regression, not an acceptable diff. If the fix cannot preserve near-harmonic behavior, DROP it.
- **Theory:** Fletcher (1964) stiff-string `f_n = n·f0·√(1+B·n²)`, B≈1e-4..1e-3, deviation ~n³ (Fletcher & Rossing).

### WI-14 — [verify first] QIFFT parabolic peak interpolation on LINEAR magnitude instead of log/dB
- **File:line:** `partial_tracker.h:219–226` passes raw linear magnitudes to `parabolicInterpolation`; no zero-padding (`stft.h` windows fftSize into fftSize). **Shared DSP — `dsp_processors_tests`.**
- **Counter-argument (both skeptics lean refute/marginal):** With Blackman-Harris (near-Gaussian in log domain, ~8-bin main lobe) the linear-vs-log bias is sub-bin (~0.05 bin ≈ 1–6 cents), a static offset with no beating mechanism (partials at distinct frequencies), well inside sieve tolerance and below pitch JND. Fix mutates the shared tracker feeding the mono sieve and would shift every stored `relativeFrequency` → golden churn.
- **Verify step:** Measure the actual frequency bias for a Blackman-Harris windowed sinusoid at a non-integer bin, linear vs log parabola. Only proceed if bias exceeds a musically meaningful threshold (e.g. >5 cents) for in-range partials.
- **If reproduces (bias musically significant):**
  - **(b) Test:** `dsp/tests/unit/processors/partial_tracker_tests.cpp`. Known sinusoid at a non-integer bin; assert estimated frequency error < 0.1% of a bin. FAILS today (linear) if significant. Name: `PartialTracker_QIFFT_LogMagnitudeAccuracy`.
  - **(c) Change:** Convert the three magnitudes to dB (`20*log10(max(m, floor))`) before `parabolicInterpolation`, recover peak height in dB then back to linear (standard QIFFT). Optionally add a zero-pad factor ≥2.
- **(d) Build+test:** `--target dsp_processors_tests`; `dsp_processors_tests.exe "PartialTracker_QIFFT*"`.
- **(e) Risk:** Shifts every `relativeFrequency` in the mono-sieve output → regenerate frequency-estimation/golden tests intentionally. If the measured bias is sub-cent (likely with Blackman-Harris), DROP as inconsequential.
- **Theory:** Smith & Serra PARSHL / Abe & Smith CQIFFT — parabola fit on log-magnitude is near-unbiased; log-domain accuracy requirement (JOS SASP quadratic peak interp).

### WI-15 — [verify first] Offline YIN window (2048) cannot reach the stated 40 Hz F0 floor
- **File:line:** `plugins/innexus/src/dsp/sample_analyzer.cpp:182–185` — comment computes "need 2x = 2205" then sets `kYinWindowSize = 2048` (N/2=1024 → f0_min ≈43 Hz). `dual_stft_config.h:76–79` documents `kHighPrecisionYinWindowSize = 4096`; live path uses 4096.
- **Counter-argument (refuter):** Affected band is only ~40–43 Hz; octave errors are corrected downstream by `subharmonicValidator.validate` (independent of YIN window) and the poly re-analysis; raising to 4096 changes YIN temporal averaging and delays first voiced detection to ~93 ms, altering attack-frame output and existing goldens.
- **Verify step:** Analyze a synthetic 41 Hz fundamental sample end-to-end; check whether the resulting frames lock to ~41 Hz or octave-error to ~82 Hz AFTER `subharmonicValidator`. Only proceed if the end-to-end F0 is wrong.
- **If reproduces (end-to-end low-bass F0 wrong):**
  - **(b) Test:** `plugins/innexus/tests/integration/` — analyze a 41 Hz WAV, assert detected `f0 ≈ 41 Hz` in voiced frames. FAILS today. Name: `INNEXUS_OfflineYin_40HzFloor`.
  - **(c) Change:** Raise offline `kYinWindowSize` to ≥2206 (prefer reuse `kHighPrecisionYinWindowSize = 4096` for headroom, matching live). Fix the contradictory comment either way (this is at minimum a cosmetic cleanup).
- **(d) Build+test:** `--target innexus_tests`; `innexus_tests.exe "INNEXUS_OfflineYin_40Hz*"`.
- **(e) Risk:** Changes YIN track + first-voiced latency for ALL samples → attack-frame goldens may shift; regenerate intentionally. If the subharmonic validator already recovers 41 Hz end-to-end, downgrade to the comment-only cleanup (do the comment fix regardless).
- **Theory:** YIN searches lags up to N/2; f0_min = fs/(N/2). 40 Hz at 44.1 kHz needs N ≥ 2206 (de Cheveigné & Kawahara 2002).

### WI-16 — [verify first] `allocateMessage()`/`sendMessage()` on the audio thread
- **File:line:** `processor.cpp:475–491` (ADSR pointer send — one-shot, guarded by `adsrPlaybackPtrsSent_`) and `processor.cpp:2039–2058` (DetectedADSR — once per sample load) and ~L643 (RecalledADSR). `allocateMessage()` may heap-allocate host-side.
- **Counter-argument (refuter):** Both fire rarely (once ever / once per load), at non-time-critical moments already dominated by heavier load-time work; `sendMessage` from `process()` is a common host-supported VST3 pattern; the rework risks dropping the one-shot DetectedADSR the controller needs.
- **Verify step:** Confirm both sites are truly one-shot/rare (read the guards). **Note WI-9 already removes the ADSR pointer send entirely** — after WI-9, only the DetectedADSR/RecalledADSR sends remain.
- **Decision:** Given the refuting analysis (inconsequential, rare, and IMessage is the reliable one-shot channel), the DEFAULT is **verified-not-actionable for the DetectedADSR/RecalledADSR sends** — do NOT churn them. Only act if profiling shows a real xrun at sample-load. If acting: pre-allocate the IMessage object(s) once in `setActive()` and reuse, or publish the ADSR values on the DataExchange bridge (as WI-9 does for the playback scalars) and let the controller poll.
- **(b) Test (only if acting):** `plugins/innexus/tests/integration/` allocation-probe test asserting no host-message allocation inside `process()` during a sample-load block. Name: `INNEXUS_NoMessageAllocInProcess`.
- **(e) Risk:** Do not break one-shot delivery of DetectedADSR knob values. No sieve impact.

### WI-17 — [verify first] No NaN/Inf sanitization on the voice/master path
- **File:line:** `processor.cpp:~1870` (tanh cubic soft-limiter does not sanitize NaN; `std::clamp(NaN)` returns NaN), `~1890` output write, `1812` `oscillatorBank.reset()` only on note-off/steal/release-kill. No `isnan`/`isfinite`/FTZ/DAZ anywhere in `plugins/innexus/src` (grep returns nothing).
- **Counter-argument (refuter):** MCF epsilon clamp (WI-5) makes the oscillator amplitude-stable and no proven self-contained NaN source exists in steady state; the "Inf*0" trigger presupposes a pre-existing Inf.
- **Interaction with WI-5:** WI-5 removes the primary internal Inf/NaN source (detune divergence). **Do WI-5 first.** This WI is defense-in-depth against any residual non-finite (corrupt input WAVs, bandwidth>1 edge, pathological analysis).
- **Verify step:** After WI-5, attempt to still produce a non-finite output (e.g. feed a WAV containing NaN samples, or `bandwidth>1`). If a NaN can still reach `out[]` OR wedge a voice permanently, act.
- **If reproduces (NaN reaches output or wedges a voice):**
  - **(b) Test:** `plugins/innexus/tests/integration/` — inject a non-finite into the voice path (build the non-finite from bit patterns per the repo's fast-math note — `std::numeric_limits::quiet_NaN()` folds to garbage under `-ffast-math`), process, assert output is finite AND the voice recovers on the next block (not permanently dead). FAILS today. Name: `INNEXUS_OutputFiniteAndVoiceRecovers`.
  - **(c) Change:** Add a final per-sample finite guard on the summed master output (`if (!std::isfinite(x)) x = 0.0f;` on a `-fno-fast-math` TU or via a bit test), and on detecting non-finite voice output reset that voice's oscillator state so a transient can't wedge it. Enable FTZ/DAZ in `setupProcessing` (`_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON)` on x86).
- **(d) Build+test:** `--target innexus_tests`; `innexus_tests.exe "INNEXUS_OutputFinite*"`.
- **(e) Risk:** Output-path only; no sieve impact. No-op for finite signals (bit-exact for existing goldens) except FTZ/DAZ flushing denormal decay tails (negligible for spectral goldens — verify). Follow the repo's fast-math NaN-detection guidance (bit manipulation on a `-fno-fast-math` file).
- **Theory:** FTZ/DAZ denormal handling; `-ffast-math` breaks `std::isnan` (use bit tests) — project cross-platform notes.

### WI-18 — [verify first] `broadcastFrameToVoices` copies full 96-partial frames per voice + rebuilds mode arrays
- **File:line:** `processor.cpp:2142–2143` (`v.morphedFrame = voice_.morphedFrame; v.morphedResidualFrame = ...` — full ~3.8 KB copy before the `numPartials` clamp at 2146–2147), L2170–2178 rebuilds `modeFreqs`/`modeAmps`.
- **Counter-argument (refuter):** Largely subsumed by WI-6 (per-sample invocation is the real cost); the copy is a cheap contiguous memcpy dominated by `processStereo`; a partial-copy risks stale data in `[numPartials,96)`; a shared-DSP signature change risks `dsp_processors_tests`.
- **Decision:** **Do WI-6 first.** After WI-6 gates evolution/blend to hop rate, re-measure (Section 3). If `broadcastFrameToVoices` is still a measurable hot spot, apply a *safe* narrowing: copy/clamp only `[0, numPartials)` while ensuring every downstream consumer reads only `[0, numPartials)` (audit `oscillatorBank.loadFrame`, `modalResonator.updateModes`, `residualSynth.loadFrame` for reads past `numPartials`). Do NOT change shared-DSP signatures for a sub-percent gain.
- **(b) Test (only if acting):** add a broadcast-cost characterization to the WI-6 test file; assert output byte-identical before/after the narrowed copy. Name: `INNEXUS_Broadcast_ActivePartialCopyEquivalent`.
- **(e) Risk:** Stale-tail hazard — the narrowed copy must leave no consumer reading `[numPartials,96)`. If any does, DROP.

### WI-19 — [verify first] Least-squares slope uses unbounded absolute frame index → float cancellation for long samples
- **File:line:** `plugins/innexus/src/dsp/envelope_detector.h:107` (`x = i - peakIdx`, never re-centered), L171 `denom = nf*sum_x2 - sum_x*sum_x` (catastrophic cancellation at large x), L174 numerator same conditioning.
- **Counter-argument (refuter):** Error is exactly zero for x < ~340 (samples < ~4 s); only long (>15–23 s) samples + a slope within ~1–2% of the 5e-4 gate shift a sustain-loop boundary by a few frames — subtle, not audible; offline-only so the "perf win" is trivial.
- **Verify step:** Feed a synthetic long contour (>2000 frames ≈ 23 s) and check whether the detected `sustainStartFrame`/`decayMs` differs from the window-local-x computation.
- **If reproduces (long-sample detection differs):**
  - **(b) Test:** `test_envelope_detector.cpp` — long contour, assert detection matches an exact-window recompute reference. FAILS today. Name: `INNEXUS_EnvelopeDetector_LongContourConditioning`.
  - **(c) Change:** Use window-local x (`x = position within the current window, 0..kWindowSize-1`) so `sum_x`/`sum_x2` stay small and constant (`denom` becomes the exact constant 1716 for a 12-integer window), or precompute the constant `denom` and accumulate only the y-dependent numerator. Identical output, better conditioning, cheaper.
- **(d) Build+test:** `--target innexus_tests`; `innexus_tests.exe "INNEXUS_EnvelopeDetector_LongContour*"`.
- **(e) Risk:** Analysis-thread only; slope-translation-invariant so short-sample results are unchanged — verify existing envelope tests pass. This also composes cleanly with WI-2 (do WI-2 first; both touch the same detector). If short samples dominate the product's use, this may be verified-not-actionable — but the fix is cheap and safe, so acting is fine.
- **Theory:** Reverse/absolute-index accumulation loses significance (Welford/West; rolling-variance stability references).

---

## GROUP D — Optimizations (confirmed) — measure before AND after (see Section 3)

> WI-6 already delivered the Evolution/Blend per-sample optimizations (#4/#7/#12). The remaining confirmed optimizations are the spread/detune/pan recompute cluster and the tail scan.

### WI-20 — `setStereoSpread`/`setDetuneSpread` recompute pan (cos+sin) and detune (pow) every sample per voice with no change detection
- **Category/severity:** optimization / high. Subsumes confirmed #11 and #13. **Setters are in shared DSP (`harmonic_oscillator_bank.h`) → `dsp_processors_tests`; call site is `processor.cpp` → `innexus_tests`.** Split the change accordingly.
- **(a) File:line + excerpt:**
  - Call site `processor.cpp:1531–1539` (inside per-sample loop L1325): `float spread = stereoSpreadSmoother_.process(); float detune = detuneSpreadSmoother_.process(); for (vi...) { ... v.oscillatorBank.setStereoSpread(spread); v.oscillatorBank.setDetuneSpread(detune); }`.
  - `harmonic_oscillator_bank.h:490–493` `setStereoSpread` → `recalculatePanPositions()` (unconditional; L949–950 `std::cos`+`std::sin` over full `kMaxPartials`=96). L503–506 `setDetuneSpread` → `recalculateDetuneMultipliers()` (L970 `std::pow(2, offsetCents/1200)` per non-fundamental partial). The setter doxygen even says "called once per frame, not per sample."
- **(b) Failing/measurement test FIRST:** two tests.
  - Correctness guard: `dsp/tests/unit/processors/harmonic_oscillator_bank_tests.cpp` — call `setStereoSpread(x)` then `setStereoSpread(x)` again with the identical value; assert the second call does NOT recompute (expose a `panRecomputeCount()` test hook; assert it stays flat on unchanged value). Same for detune. FAILS today. Name: `HarmonicOscillatorBank_SpreadDetuneChangeGuard`.
  - Perf (Section 3): a `[.perf]`-tagged benchmark (below) measuring per-sample cost with settled smoothers.
- **(c) Change:** Add an epsilon change-guard in both setters: store `lastStereoSpread_`/`lastDetuneSpread_`; early-out (no recompute) when `|new - last| < 1e-6f`. This is behaviorally identical in steady state (skipped recomputes yield the same arrays), so goldens are unaffected. **CRITICAL detune caveat:** `detuneMultiplier_` is mutated in place by the modulator (`applyExternalFrequencyMultipliers` `*= multipliers[i]`, ~L548), so `recalculateDetuneMultipliers` doubles as the base-reset before the modulator multiply. A naive early-out on `setDetuneSpread` would let the modulator multiplier COMPOUND every sample → runaway detune. Therefore: keep a separate `baseDetuneMultiplier_` array recomputed only on spread change, and each sample copy it into `detuneMultiplier_` (cheap array copy) before applying modulator multipliers — OR gate the early-out on "no frequency modulation active this block." The pan early-out is safe (modulator pan reads the un-mutated base `panPosition_`). Optionally also bound the loops to `activePartials_`.
- **(d) Build+test:** setter change: `--target dsp_processors_tests`; `dsp_processors_tests.exe "HarmonicOscillatorBank_SpreadDetuneChangeGuard*"`. Call-site/base-detune change: `--target innexus_tests`; `innexus_tests.exe` (modulator detune tests).
- **(e) Risk:** The detune base-reset semantics MUST be preserved or mod1/mod2 frequency animation compounds (audible pitch runaway) and breaks modulator tests — implement the `baseDetuneMultiplier_` split, don't just early-out. Pan is safe. Synthesis-side only; no sieve/bandwidth impact.

### WI-21 — Harmonic modulators recompute per-partial pan cos+sin every sample via `applyPanOffsets`
- **Category/severity:** optimization / medium. Confirmed #14. **`applyPanOffsets` in shared DSP; call site in processor.**
- **(a) File:line + excerpt:** `processor.cpp:1553–1562` (mod1) / L1586 (mod2) call `v.oscillatorBank.applyPanOffsets(...)` per active voice per sample. `harmonic_oscillator_bank.h:522–534` loops full `kMaxPartials`=96 doing `std::cos(angle)+std::sin(angle)`. Together with WI-20's `setStereoSpread` the pan table is recomputed 2–3× per sample per voice.
- **(b) Test FIRST:** `dsp/tests/unit/processors/harmonic_oscillator_bank_tests.cpp` — bound-check: assert `applyPanOffsets` only touches `[0, activePartials_)` and produces identical pan for active partials (exact-output preserving). Plus a `[.perf]` benchmark. Name: `HarmonicOscillatorBank_ApplyPanOffsets_ActivePartialsOnly`.
- **(c) Change (exact-output-preserving only):** Bound the `applyPanOffsets` loop to `activePartials_` instead of 96 (inactive partials are silent — `processStereo` only sums active). Fold the `setStereoSpread` base + modulator offset into a single per-sample pan recompute (eliminate the double trig recompute). Do NOT substitute an approximate quadratic pan law (would perturb stereo goldens/SC-010).
- **(d) Build+test:** `--target dsp_processors_tests`; `dsp_processors_tests.exe "HarmonicOscillatorBank_ApplyPanOffsets*"`. Then call-site: `--target innexus_tests`.
- **(e) Risk:** Keep constant-power pan bit-exact for active partials (verify against existing stereo/pan tests). No sieve impact.

### WI-22 — `processStereo` scans the full 96-partial fade-out tail every sample per voice
- **Category/severity:** optimization / medium. Confirmed #15. **Shared DSP — `dsp_processors_tests`.**
- **(a) File:line + excerpt:** `harmonic_oscillator_bank.h:623` `for (size_t i = (size_t)n; i < kMaxPartials; ++i) { if (currentAmplitude_[i] > 1e-8f) {...} }` (duplicated in mono `process()` ~L730). `kMaxPartials=96` (`harmonic_types.h:21`) — the header's "48" comments are stale (also fix those comments while here).
- **(b) Test FIRST:** `harmonic_oscillator_bank_tests.cpp` — load a low-partial-count frame (e.g. 8 partials) after a high-count frame, process until tails decay, assert output byte-identical to the current implementation over a block (equivalence), AND assert the tail scan is bounded (expose a `tailScanUpperBound()` hook; assert it tracks the high-water mark, not always 96). Name: `HarmonicOscillatorBank_TailScanBounded`.
- **(c) Change:** Track a high-water mark `prevActivePartials_` of the previously-active partial count; scan the tail only `[n, prevActivePartials_)`, and reset the mark to `n` once all tail amplitudes reach ~0 (so a still-fading partial is never dropped). Fix the stale "48" doc comments to "96".
- **(d) Build+test:** `--target dsp_processors_tests`; `dsp_processors_tests.exe "HarmonicOscillatorBank_TailScan*"`.
- **(e) Risk:** The reset-on-decay must not drop a still-fading partial (that's the one correctness subtlety — test it explicitly). No sieve/bandwidth impact.

---

## GROUP E — Low-severity candidates (unverified) — QUICK SWEEP

> Batch these into ONE working session. For each: read the cited lines, confirm the defect reproduces, and either fix (with a minimal test where behavior is testable) or record "verified-not-actionable". Several are comment/documentation-only. Group by file to minimize rebuilds. **Verify the premise of each before touching** — at least C15's premise is likely stale.

Quick-sweep items (file:line — one-line fix — target):

- **QS-1 / C1** `live_analysis_pipeline.cpp:407` — mono↔poly crossfade blends amplitudes by array index, not frequency. Fix: match by `harmonicIndex`/nearest-frequency before interpolating (MQ birth/death for unmatched). Bounded to 4 frames — low. Target: `innexus_tests`. *Test:* assert crossfade preserves per-harmonic continuity when partial orderings differ.
- **QS-2 / C2** `harmonic_blender.h:190` — sets `rp.harmonicIndex = p+1` and reconstructs `frequency` from `relativeFrequency` alone, leaving `harmonicIndex`/`inharmonicDeviation` inconsistent for inharmonic sources. Fix: derive `harmonicIndex = max(1, round(relativeFrequency))` and recompute `inharmonicDeviation = relativeFrequency - harmonicIndex` (mirror `sample_analyzer.cpp:539–543`). Target: `innexus_tests`.
- **QS-3 / C3** `multi_pitch_detector.h:254/268–275` — comment claims Klapuri spectral-envelope interpolation; code subtracts flat `0.85*amp`. Fix: correct the comment (or implement interpolation). Doc/behavior mismatch — comment fix is the low-risk default. Target: `dsp_processors_tests` (only if code changes).
- **QS-4 / C4** `subharmonic_validator.h:15` — cites Hermes SHS but uses `1/h` over 4 fixed candidates. Fix: correct the citation/comment to "bounded octave-error corrector via weighted harmonic support." Comment-only. No build needed beyond compile.
- **QS-5 / C5** — folded into **WI-1** (long-window FFT waste). Skip here.
- **QS-6 / C6** `spectral_decay_envelope.h:54/142` — `partialGains_` filled but never read. Fix: remove it and its `fill()` (dead state). Target: `innexus_tests`.
- **QS-7 / C7** `spectral_decay_envelope.h:130` — `kBaseDecayTimeSec=0.6` is a 1/e time constant mislabeled "decay time". Fix: rename/comment as "time constant (1/e)" and note actual −60/−80 dB tail (~4.1 s / ~5.5 s). Comment-only, no math change.
- **QS-8 / C8** `harmonic_oscillator_bank.h:244` — `bandwidth_[i] = partial.bandwidth` with no clamp before `sqrt(1-bw)`. Fix: `bandwidth_[i] = std::clamp(partial.bandwidth, 0.0f, 1.0f);` in both `loadFrame` and `loadPolyphonicFrame`. **Do this alongside WI-8** (bandwidth energy) — it hardens the synthesis boundary against the historical inflated-bandwidth noise bug. Target: `dsp_processors_tests`. *Test:* load `bandwidth=2.0`, assert finite output.
- **QS-9 / C9** `harmonic_modulator.h:145/190` — per-sample `std::sin`/`std::pow`. Fix: Gordon-Smith magic-circle recurrence for the LFO (see MEMORY.md formula) and compute the single freq-multiplier/pan scalar once. Optimization — measure. Target: `innexus_tests`. *Interacts with WI-10/WI-20 — do after those.*
- **QS-10 / C10** `processor_params.cpp:34` — only last automation point applied, `sampleOffset` ignored (block-rate, not sample-accurate). Fix: route per-sample-consumed params (spread/detune/mod depths) through existing `OnePoleSmoother`s (some already are), OR document as intentional per-block. Default: document + ensure the per-sample-path params are smoothed. Target: `innexus_tests`.
- **QS-11 / C11** `envelope_detector.h:143/177/181` — reverse Welford variance can go negative → false steady-state. Fix: `variance = std::max(0.0f, w_M2/nf)` before the compare (or recompute over the 12-sample window each step — O(12), exact). **Compose with WI-2 and WI-19** (same detector). Target: `innexus_tests`. *Test:* an add/remove sequence that drives `w_M2` negative; assert no false sustain.
- **QS-12 / C12** `harmonic_modulator.h:135` / `evolution_engine.h:116` — float phase accumulator drifts at 0.01 Hz over long runs. Fix: store `phase_`/`inverseSampleRate_` as `double` for these free-running accumulators. Target: `innexus_tests`.
- **QS-13 / C13** `processor.cpp:1560` / `harmonic_oscillator_bank.h:546` — `applyExternalFrequencyMultipliers` loops full 96; per-sample `std::array<...,96>` re-zeroed. Fix: bound to `activePartials_`, reuse persistent scratch buffers. **Overlaps WI-20/WI-21** — do together. Target: `dsp_processors_tests` + `innexus_tests`.
- **QS-14 / C14** `processor.cpp:1785/502` — voice stranded active if `adsrAmount` automated to 0 mid-ADSR-release (block-level `adsrActive` gate flips false before a long release finishes). Fix: track per-voice release independently of the global `adsrActive` gate — always advance/finish voices in `inAdsrRelease`, or convert to the exponential `inRelease` path when `adsrAmount`→0. Target: `innexus_tests`. *Test:* note-off with adsrAmount>0, automate adsrAmount→0, assert the voice reaches Idle and is freed. This is a genuine stuck-voice bug — verify carefully; if it reproduces, consider promoting out of the sweep.
- **QS-15 / C15** `processor.h:358` — claims partial-count 64/80/96 are no-ops (assumes `kMaxPartials=48`). **PREMISE LIKELY STALE:** `harmonic_types.h:21` defines `kMaxPartials=96` (confirmed by WI-22). So 64/80 are NOT no-ops; only ">96" would be. **Verify first:** read `getActivePartialCount` (`processor.h:361–363`) and `kMaxPartials`. If the bank truly holds 96, the candidate is wrong — record verified-not-actionable (and note the doc/comment discrepancy is the real issue, fixed in WI-22). If the bank is actually capped below a listed option, remove the unreachable StringListParameter entries (never swap the param type — MEMORY.md). Target: `innexus_tests`.
- **QS-16 / C16** `processor.cpp:1896` — mono output bus sums two per-channel-limited samples → can reach ~2.0, defeating the safety limiter. Fix: for the mono path, sum before limiting (`out[0][s] = softLimit(sampleL + sampleR)`) or average (`(sampleL+sampleR)*0.5f`). Target: `innexus_tests`. *Test:* drive both channels near full scale, select mono output, assert `|out[0]| <= 1.0`. Genuine safety bug — verify and likely fix.

Quick-sweep build/test: after fixing a batch, run the relevant target(s) fully:
```bash
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target innexus_tests dsp_processors_tests
build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5
```
Quick-sweep risk: QS-1/QS-2 touch analysis frame metadata — do not alter the mono-sieve *analysis* (they operate on already-built frames). QS-8 hardens bandwidth (good). None may disable the sieve. Comment-only items (QS-3/4/7) just need a clean compile.

---

# Section 3 — Optimization measurement protocol (before/after)

Every optimization WI (WI-6, WI-18, WI-20, WI-21, WI-22, QS-9, QS-13) MUST be measured before and after. Use a `[.perf]`-tagged Catch2 benchmark (run with `dsp_processors_tests.exe "[.perf]"` or `innexus_tests.exe "[.perf]"` — perf tags are opt-in and excluded from the default run). Record the numbers in the commit message.

| WI | Expected win | Measurement step (before → after) |
|---|---|---|
| **WI-6** (evolution/blend hop-gate) | ~500× less residual-FFT + frame-rebuild work in evolution/blend modes; eliminates CPU-overrun dropouts. Residual `loadFrame` drops from `numSamples` → `numSamples/hopSize` (~512×) per voice. | Benchmark `process()` of N blocks with ≥2 evolution waypoints + residual level>0, 8 voices. Measure µs/block. Assert `residualLoadFrameCallCount()` per block ≈ `numSamples/hopSizeInSamples` after (was ≈`numSamples`). Expect large µs/block reduction. |
| **WI-20** (spread/detune change-guard) | Eliminates ~96 pow + ~192 trig per voice per sample in the settled-knob common case (order 10s of M transcendentals/sec at 8 voices). | `[.perf]` benchmark: settled smoothers, 8 voices, N samples of `processStereo` with per-sample `setStereoSpread`/`setDetuneSpread`. Measure µs; after the guard, expect near-elimination of the recompute cost. |
| **WI-21** (pan bound to activePartials + fold) | Removes redundant cos/sin over inactive partials and the double pan recompute when a modulator is active. | `[.perf]` benchmark with mod1 enabled, few active partials; measure µs/sample before/after. |
| **WI-22** (tail-scan high-water) | Removes up to `96 - activePartials_` load+compare per sample per voice in the hottest loop. | `[.perf]` benchmark with a low-partial-count frame (e.g. 8 partials), 8 voices; measure µs/block before/after. |
| **WI-18** (broadcast copy narrowing) | Sub-percent — only act if WI-6 leaves it measurable. | Compare µs/block of `broadcastFrameToVoices` before/after; only keep the change if the delta is real. |
| **QS-9** (magic-circle LFO) | Removes per-sample `std::sin`/`std::pow` per modulator. | `[.perf]` benchmark, 2 modulators enabled; measure µs/block. |
| **QS-13** (mult loop bound + scratch reuse) | Removes 96-wide loop + per-sample array re-zero. | Folded into WI-20/WI-21 benchmarks. |

Protocol: capture the "before" number on the pre-change build, implement, capture "after" on the post-change build, and only accept the WI if (a) all correctness tests pass, (b) output is equivalent where the WI claims exact-output preservation, and (c) the "after" number is ≤ "before". Paste both numbers into the commit body.

---

# Section 4 — Task: create the Innexus skill

**Task:** Write the file `.claude/skills/innexus/SKILL.md` (create the `.claude/skills/innexus/` directory if it does not exist) with **exactly** the following content, verbatim (including the YAML frontmatter delimited by `---`). Do not reformat, do not paraphrase, do not add or remove lines. This is a documentation deliverable, not code — no test, no build.

````markdown
---
name: innexus
description: Domain knowledge for the Innexus harmonic-resynthesis instrument — the analysis→resynthesis signal path, the two analysis front-ends, load-bearing invariants (mono harmonic-sieve, per-partial bandwidth), and where the deep docs live. Use when working on Innexus, harmonic/additive resynthesis, sample or live spectral analysis, or Innexus presets — especially outside plugins/innexus/ where the leaf CLAUDE.md won't auto-load.
allowed-tools: Read, Glob, Grep
---

# Innexus — Harmonic Analysis / Resynthesis Instrument

Innexus (AU `aumu`) analyzes audio into per-frame harmonic snapshots (`HarmonicFrame` + `ResidualFrame`)
then additively resynthesizes them with evolution/physics/modulation shaping. When editing under
`plugins/innexus/`, its leaf `CLAUDE.md` auto-loads with build/test/param facts — this skill carries the
domain lore that applies when reasoning about Innexus from elsewhere (DSP library, presets, tooling).

## Signal path (per voice)

Two input sources feed one frame contract, selected by `inputSource_`:
- **Sample mode** (≤0.5): drag-dropped WAV → `SampleAnalyzer` (background `std::thread`) → immutable
  `SampleAnalysis`, published to the audio thread by atomic pointer swap; voice advances `currentFrameIndex`.
- **Sidechain mode** (>0.5): aux bus 0 downmixed to mono → `LiveAnalysisPipeline` **on the audio thread**,
  emitting one frame per STFT hop.

Frame selection (`voice_.morphedFrame`): Recall│Capture│ManualFreeze+morph(lerp)│confidence-gated freeze,
then `applyModulatorAmplitude` → `applyHarmonicPhysics` → `broadcastFrameToVoices` (copy to each voice).
Per voice: `HarmonicOscillatorBank` (48 Gordon-Smith MCF sines; freq/amp/**phase** seeded from partials,
phase state NOT reset across frames = continuity) + exciter{Impact│Bow│Residual} → resonator{Modal│Waveguide,
equal-power xfade} → BodyResonance → `PhysicalModelMixer(residual↔physical)`. Σ voices × 1/√activeCount →
global `SympatheticResonance` → master gain → tanh soft-limiter → out.

## Analysis pipeline

Both paths share: `PreProcessingPipeline` (DC → 30 Hz HPF → transient suppress → noise gate) → YIN pitch →
dual Blackman-Harris STFT (**short 1024/512** for temporal tracking, **long 4096/2048** for low-freq
resolution) → `PartialTracker` (Hungarian match + harmonic sieve) → `HarmonicModelBuilder` (dual-timescale
smooth) → residual bands. Offline adds a **two-pass polyphony guard**: if F0 is unstable/noisy, pass 2
re-runs with a fixed F0 and `voiced=false` (sieve OFF) so all peaks survive — the conditional poly fix.

## Load-bearing constraints (violating these breaks presets, tests, or sound)

- **Never globally disable the mono harmonic sieve.** The sieve runs only when `f0.voiced`; poly/pass-2 code
  turns it off *conditionally* via a neutral `voiced=false` F0. The monophonic sieved path is load-bearing.
- **Per-partial `bandwidth` (0=sine..1=noise) is the historical noise bug.** Inflated bandwidth injects noise
  on *every* partial. Physics/modulators touch amplitudes only; pass-2 poly sets `bandwidth=0` (unreliable
  without the sieve). Treat any bandwidth write with suspicion.
- **No frees on the audio thread.** Superseded `SampleAnalysis` goes to `pendingDeletion_`, freed only in
  `setActive`/dtor. `currentAnalysis_` is an atomic acquire/release swap.
- **UI data bridge:** `sendDisplayData` piggybacks a `DisplayData` block on the DataExchange handler
  (~30 Hz throttle) with a Tier-3 `sharedDisplay_` polling fallback — don't add new IMessage loops.
- **Param IDs:** section bases — 200s = envelope, 400s = harmonic. `kPartialCountId`=202 is a
  **StringListParameter** ("48"/"64"/"80"/"96"), not a range — never swap a registered VST3 param type.
- **Preset categories are FIXED (7):** `Brass and Winds`, `Drums and Perc`, `Found Sound`, `Keys`,
  `Pads and Drones`, `Strings`, `Voice`. Never invent new ones.

## Deep tooling (read these for real work)

- **Plugin specifics:** [`plugins/innexus/CLAUDE.md`](../../../plugins/innexus/CLAUDE.md) — skeleton,
  param-ID scheme, `innexus_tests` target (unit `tests/unit/{processor,vst}/` + `tests/integration/`,
  run both), pluginval path, AU config.
- **Frame contract & shared DSP:** `dsp/include/krate/dsp/processors/harmonic_types.h`
  (`HarmonicFrame`, `Partial`, `kMaxPartials`), `residual_types.h`, `harmonic_oscillator_bank.h`.
- **Analysis internals:** `plugins/innexus/src/dsp/` (`sample_analyzer`, `live_analysis_pipeline`,
  `pre_processing_pipeline`, `dual_stft_config.h`) and shared `partial_tracker.h` / `harmonic_model_builder.h`.
````

---

# Section 5 — Final gates (run after ALL work items, before any release)

Run these in order. All must pass. Skip none. Capture slow-tool output to a log file on the FIRST run and inspect the log (do not re-run to re-grep).

1. **Zero compiler warnings.** Full clean-ish build of the plugin and DSP; scan the build log for any `C4xxx`/warning. Fix all (C4244 → `f` suffix; C4267 → explicit cast; C4100 → `[[maybe_unused]]`). No warnings tolerated.
   ```bash
   "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target innexus_tests dsp_processors_tests dsp_systems_tests 2>&1 | tee build_innexus.log
   ```
2. **Full innexus_tests + touched DSP layer tests.**
   ```bash
   build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5
   build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -5
   build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
   ```
   Each must end with `All tests passed (...)`. Do NOT grep individual cases — trust the summary line.
3. **Build the VST3 and run pluginval strictness 5.** (Any plugin source changed → required.)
   ```bash
   "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Innexus
   tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3" 2>&1 | tee pluginval_innexus.log
   ```
   (The post-build copy to `C:/Program Files/Common Files/VST3/` may fail on permissions — that is fine; the bundle at `build/windows-x64-release/VST3/Release/Innexus.vst3/` is valid.)
4. **clang-tidy on innexus (and dsp if DSP files changed).** Fix ALL warnings, not just new code.
   ```powershell
   ./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja *> clang_tidy_innexus.log
   ./tools/run-clang-tidy.ps1 -Target dsp     -BuildDir build/windows-ninja *> clang_tidy_dsp.log
   ```
   (Requires the Ninja compile_commands.json setup from a VS Developer PowerShell — see project CLAUDE.md. Redirect to a log on the first run; inspect the log.)
5. **Commit per logical group** (only when the user authorizes, per project rules — never push). End commit messages with the `Co-Authored-By: Claude ...` trailer. Record before/after perf numbers (Section 3) in optimization commits.

**Golden/approval note:** WI-6, WI-14, WI-15 (if acted), and any tracker change (WI-12/13/14) may legitimately shift analysis/synthesis goldens. Treat such diffs as EXPECTED-and-reviewed regenerations, never silent breaks — inspect each diff, confirm it matches the intended behavioral change, and regenerate the golden only then.

**Sieve safety re-check before final commit:** grep the diff for any change that makes the harmonic sieve run when `!f0.voiced`, or that widens tolerance below the half-harmonic-spacing bound, or that writes `bandwidth` without a `[0,1]` clamp. Any such change is a red flag against the load-bearing invariants — revert or re-scope.
