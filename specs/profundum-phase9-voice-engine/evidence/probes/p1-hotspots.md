# T028 — P-1 hot-spot list (optimisation group, ruling R-A, 2026-10-20)

Setting: SC-001's showcase chain (`VoiceEngine_CpuProbe`, `P1_VARIANT=static-morph`: morph held, Drop 24 st every
250 ms, CYCLE → Mass, WIDE with field Motion, Tube and Fracture endpoints, MONO then 4 voices). Three sources:

1. **Replica stage removal** (`p1-cpu-static-breakdown.txt`): the test-side replica rendered whole and with one stage
   removed; "cost" = the difference (MONO, % of one core; 4-voice per-voice figure in brackets).
2. **Lever table** (`run-log/close/p1-levers.log`): the engine with one setting changed (SOLID, excitation off, dynamics
   2×/off, no Motion on the field).
3. **Sampled profile** (`run-log/close/prof/p1-static3.etl`, exported to `prof/p1-static3-profile.txt`, aggregated in
   `prof/p1-static3-summary.txt`): `wpr -start CPU` over the whole probe run, against an exe linked with `/DEBUG`
   (`build/windows-x64-prof`, same `/O2 /Ob2` codegen); 51.8 M samples in the test process. Two earlier traces
   (`p1-static.etl`, `p1-static2.etl`) resolved only the C runtime because the Release exe carries no PDB; they agree on
   the CRT share (16.5 %).

Readings taken under the sampler were ≈ 40 % high (MONO 16.0 / POLY4 47.7 vs 11.5 / 32.0 unsampled); only the
distribution is used.

## 1. Where the time goes (profile, % of the test process's samples)

| Bucket | Functions (self time) | Share |
|---|---|---|
| Halfband oversampling (every 4× residual path: excitation, Fold, Shape, Character; Fold's 4× detector) | `Stage2x<8>::down` 7.99, `Stage2x<4>::up` 4.03, `Stage2x<4>::down` 3.82, `upsample` 3.31, `OversampledResidualPath::downsample` + lambda 3.48, `downsample` 0.34 | **23.0 %** |
| C runtime scalar math | `sinf` 4.47, `expf` 3.43, `logf` 2.97, `sin` 2.04, `log1pf` 1.27, `__remainder_piby2` 0.93, `powf` 0.69, `exp2f` 0.61, `log2f` 0.31, `pow` 0.30, `cosf` 0.26, others 0.5 | **17.8 %** |
| Core excitation (ADAA body + 2fs notch hook) | `exciteRun` 4.26, its lambda 2.86 | 7.1 % |
| Harmonic Fold stage | `processStage` 4.22, `processBlock` 1.69, `PeriodPeakDetector::truePeak` 1.21, `pushHeld` 0.42 | 7.5 % |
| Character | `FractureBranch::process` 3.08, `TubeStage::process` 2.19, `TubeBranch::process` 1.07, `processBlock` 1.08, `Waveshaper::applyShape` 0.60, `KeytrackedTilt::process` 0.54, `runSlot` lambda 0.50 | 9.1 % |
| `FastMath::fastTanh` (Tube's waveshaper, Fracture's bias tanh, ADAA epsilon branch) | | 2.33 % |
| Output limiter's IIR 4× upsampler (`Oversampler<4,1>` Economy = 2 × `BiquadCascade<4>` per channel) | `BiquadCascade<4>::processBlock` 4.62, `TruePeakLimiter` 0.12 | **4.7 %** |
| Bank per-sample continuity pass (`setContinuousFrequencyUpdates`, Phase 4 opt-in) | `reconcileContinuity` 3.66 | **3.7 %** |
| Bank render | `ProcessMcfBatchSIMDImpl` 1.81, `processStereo` 0.78, `processMcfBatchSIMD` 0.20 | 2.8 % |
| Shape | `processChannel` 1.40, `SVF::processMulti` 1.32, `processBlock` 0.75, `TanhADAA::process` 1.31 + `F1` 0.52 (shared with Fracture) | 5.3 % |
| Bank pan hand-off per interval (FR-012 step 2) | `_sse4_sinf4` 1.06 + `_sse4_sin2` 0.79 (the vectorised `applyPanOffsets` loop), `applyPanOffsets` 0.14, `setPartialPanOffsets` 0.19, `computePanVector` 0.29 | 2.5 % |
| Prepare-time work caught by the whole-run trace (not in the timed window) | `HarmonicFold::computeTables` 2.79, most of `sin` (double) 2.04, `computeExcitationTables` 0.20, branch `prepare`s 0.2 | ≈ 5.3 % |
| Mass dynamics (sidechain at 4×, gain computer) | `processOversampled` 0.36, `DynamicsProcessor::processSample` 0.35 + `constexprExp` 0.47, `detect` 0.17, `TransientShaper::processBlock` 0.32, `EnvelopeFollower` 0.12 | 1.8 % |
| Control path (per interval) | `SVF::setCutoff` 0.38, `ShapeMorph::computeControls` 0.25, `gridStep` 0.21, `loadFrame` 0.30, `refreshMultipliers` 0.27, recipe `evaluateMask`/`evaluateShape` 0.20, `KeytrackedTilt::updateGains` 0.09, re-key trig | ≈ 2.5 % |
| Anchor | `processBlock` 0.30, `waveformSample` 0.17, `shapeUnit` 0.05 | 0.5 % |

CRT attribution by call site (read from the shipped headers; the sampler has no stacks):

- `expf` + `log1pf` + `logf` (7.7 %): `TanhADAA::F1` = |x| − ln 2 + log1p(exp(−2|x|)), evaluated **twice per sample**
  (F1(x·drive) and F1(x₁·drive)) by ShapeMorph's ROUND regime and the Fracture branch at 4× per channel; ucrt's
  `log1pf` falls through to `logf` for its argument range, which is the `logf` bucket.
- `sinf` (4.5 %) + `__remainder_piby2`: `WavefoldMath::sineFold` → `std::sin(θ·s/V)` per 4× sample per channel in
  Fold's fold regime (|argument| ≤ `kFoldIndexMax` = 3 rad); one more per sample in `AnchorOscillator::waveformSample`.
- `_sse4_sinf4` / `_sse4_sin2` + `cosf`: `HarmonicOscillatorBank::applyPanOffsets`, cos + sin for all 96 partials at
  every interval in which the field's pan vector changed (with field Motion on: every interval).
- `sin` (double, 2.0 %): prepare-time tables (`HarmonicFold::computeTables`, Character branch tables, core calibration);
  `FractureBranch::setFundamentalHz` re-keys six double trig per interval (0.08 % self).
- `powf` / `pow` / `exp2f` / `log2f` / `tanf` (2.0 %): per-interval re-keys (`ShapeMorph::computeControls` + `keyChannel`
  on every f₀ change, `KeytrackedTilt::updateGains`, `BassStereoField::lawCurve` for the partials inside the law's
  transition band, `TransientShaper` duration map, `BassCompressor` corner), `transient_shaper.h` dB helpers per sample.

Stage view (replica removal, static morph, Tube; MONO % of one core; 4-voice per voice):

| Stage | MONO | POLY4 / voice | Profile reading |
|---|---|---|---|
| core + excitation | 4.31 | 3.76 | bank 2.8 % + excitation 7.1 % + its 4× path + continuity pass 3.7 % |
| field (stage + pan + WIDE) | 4.05 | 3.8 | the field itself is 0.5 %: removing it makes the voice L == R, so every stage after the core runs **linked** (one channel) — this row is the unlinked-stereo cost, not the field's |
| Character | 3.68 | 3.5 | 9.1 % + Fracture's 4× path + F1 |
| Mass dynamics | 2.82 | 1.2 (global) | 1.8 % after T029 lever 2 (the breakdown predates it) |
| Shape | 1.97 | 1.9 | 5.3 % + its 4× path (three decimations per channel) |
| DC + limiter | 1.87 | 0.65 (global) | 4.7 % — the limiter's IIR upsampler is the whole row |
| Fold | 1.19 | 1.8 | 7.5 % + sinf |
| a₁ + meter | 1.02 | 0.56 | recipe evaluation per interval + Goertzel meter |
| pitch (Drop + lock) | 0.85 | 0.43 | `DropEnvelope::run` 0.34, `runPitch` exp2 per sample 0.13 |
| morph + law + Motion + macros | 0.67 | 1.4 | control path |

## 2. Ranked hot spots, mechanism, candidate fix, predicted saving, gate

Savings are in % of the engine's samples (≈ % of the reading). "Bit-identical" = every existing caller renders the
same bits (proved by the before / after `VoiceEngine_FingerprintHarvest` digits and the owner's own TUs).

| # | Hot spot | Mechanism | Fix | Predicted | Where / gate |
|---|---|---|---|---|---|
| 1 | Halfband oversampling, 23 % | Four 4× residual paths per channel per voice, unlinked in SPREAD / WIDE; each is one `Stage2x<8>` + one `Stage2x<4>` up and down, first-order allpass sections in a serial dependency chain | (a) Shape decimates HOLLOW's tap `l` twice through identical filters with identical histories (`hollowDown` and the path's `downLin_`; the design note at shape_morph.h:35 says they are equal) → return the path's linear decimation instead, one 4× decimation fewer per channel. (b) Interleave the A / B branch chains per section in `Stage2x::up` / `down` (independent chains, same per-branch arithmetic). (c) The structural lever — process L and R in lock-step as 2-lane vectors inside each stage — is bit-identical but rewrites Phase 2 / 5 stage bodies; held until (a)–(b) and the rest are measured | (a) ≈ 2–2.5 %, (b) 0–3 % | (a) `shape_morph.h` + `oversampled_residual_path.h` accessor (append-only), Phase 5 TUs `[shape-morph]`, `[bass-character]`; (b) `polyphase_halfband_oversampler.h`, Phase 2 / 5 / 6 TUs + consumers |
| 2 | `TanhADAA::F1` twice per sample, 7.7 % | F1(x₁·drive) is F1(x·drive) of the previous sample whenever the drive is unchanged | Cache the previous F1 and invalidate on `setDrive` / `reset` / the non-ADAA branches | ≈ 3.5 % | `primitives/tanh_adaa.h` (bit-identical); `tanh_adaa_test.cpp`, Phase 5 TUs, Membrum `feedback_exciter.h` (membrum_tests) |
| 3 | Limiter IIR 4× upsampler, 4.7 % | `Oversampler<4,1>` Economy mode = 8 biquads per channel per 4× sample, 64 biquad evaluations per stereo sample, for a peak **detector** | Append-only opt-in: detect on the polyphase halfband 4× interpolation (`PolyphaseHalfbandOversampler::upsample`, the same primitive `PeriodPeakDetector::truePeak` uses), default unchanged for Seraphis / Vorago / Membrum; Profundum's engine opts in | ≈ 3.5 % | `processors/true_peak_limiter.h` opt-in; `true_peak_limiter_test.cpp` + a new case for the mode, Phase 9 limiter rows (sample-level ceiling stays exact, SC on true-peak re-measured) |
| 4 | `reconcileContinuity` per sample, 3.7 % | The Phase 4 opt-in walks all active partials every sample comparing coefficients, although a writer (`loadFrame`, `recalculateFrequencies`, `applyExternalFrequencyMultipliers`, `setDetuneSpread`) runs at most once per pitch interval | Dirty flag set by every coefficient writer; the pass returns at once when clean (a clean pass is a proven no-op: every entry already equals its target) | ≈ 3.5 % | `processors/harmonic_oscillator_bank.h` (bit-identical, internal); bank TUs, `[profundum]` core / note-behaviour TUs, Innexus / Seraphis / Vorago consumers unaffected (opt-in off) |
| 5 | Fold `sineFold` per 4× sample, ≈ 4.5 % | `std::sin` of a bounded argument (|θ·s/V| ≤ 3 rad) through the full-range CRT routine | Fold-local sine for |x| ≤ `kFoldIndexMax`: quadrant reduction + the minimax polynomial pair (≤ 2 ulp), `WavefoldMath::sineFold` untouched for its other callers | ≈ 2.5 % | `processors/harmonic_fold.h` (tolerance: Phase 2's own bars — aliasing ≤ −80 dBFS, THD monotone, descriptor continuity, DC — re-run; not bit-identical) |
| 6 | Bank pan hand-off trig, 2.5 % | 96 cos + sin per interval whenever the vector changed; with field Motion every interval | Change-only per partial (bit-identical, no gain under Motion); a polynomial pan law is not bit-identical and the bank is shared by Innexus / Seraphis / Vorago — held | 0 under Motion | — |
| 7 | Excitation body, 7.1 % | ADAA at 4× plus the 2fs notch hook (3 biquads per 2fs sample) | Inherent to FR-009 / Phase 2's aliasing bar; no bit-identical lever found | — | — |
| 8 | Control-path re-keys, ≈ 2.5 % | Shape / Character / Fold re-key on every f₀ change the voice hands them per sample (`fCore_`), i.e. per sample during a Drop | Hold f₀ per pitch interval for the re-keys (engine-internal); changes the re-key cadence the Phase 9 rows describe — not taken without a ruling | ≈ 1 % | `profundum_voice.h` |
| 9 | Mass dynamics gain computer | `DynamicsProcessor` dB helpers are the constexpr series (`constexprExp` 0.47 %) | Shared by every plugin's compressor; not touched | — | — |

Implementation order (T029): 2, 4, 1(a), 1(b) in one rebuild (all bit-identical, proved by the fingerprint digits), then
3 and 5 (tolerance-gated, each measured alone), then re-probe (T030). The unlinked-stereo cost (row 1(c)) is decided
on the T030 reading.
