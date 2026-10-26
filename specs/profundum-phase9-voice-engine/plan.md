# Implementation Plan: Profundum Phase 9 — Voice & Engine

**Spec:** `specs/profundum-phase9-voice-engine/spec.md` (Draft 2026-10-19, reviewed; Q1–Q8 ruled, OQ-2 / OQ-8 / OQ-9 resolved)
**Roadmap:** `specs/Profundum-roadmap.md` Part A, Phase 9 (lines 647–697); KDD 2–4 (lines 125–144); Cross-Cutting
Constraints (lines 969–992)
**Phase base:** commit `0ae33b87` on `synth/profundum`
**Status:** Planned 2026-10-19. S1 lists every refinement made inside the freedom the spec leaves to the plan, the points
where this plan amends a spec sentence (each marked **FOR RATIFICATION**), and four ⚠ RISK escalations (R-A … R-D), each
with a pre-registered probe that runs before any gated render is trusted, and a stop rule. No bar is relaxed anywhere.
The drafted values the spec hands to the plan (the FR-080 bars, SC-007 ε, the SC-014 segment sources, D-8) are adopted
or ruled in C-17 / C-4.

Five new Layer 3 headers, header-only, `Krate::DSP`, in `dsp/include/krate/dsp/systems/`:

| Header | Content |
|---|---|
| `profundum_motion.h` | `ProfundumMotion`: ENV / CYCLE / DRIFT, one active source, five fixed targets, a `ModulationSource` output (FR-040–FR-049) |
| `profundum_macro_matrix.h` | `ProfundumMacro`, `ProfundumMacroTarget`, `ProfundumMacroRow`, `ProfundumMacroValues`, `ProfundumMacroMatrix` (FR-050–FR-056) |
| `profundum_engine_observer.h` | `ProfundumEngineSeams`, `ProfundumCall`, `ProfundumCallRecord`, `ProfundumGridRecord`, `ProfundumVoiceTaps`, `ProfundumEngineTaps`, `ProfundumEngineObserver` (FR-086). Its own header because both the voice and the engine need these as **complete** types (S2) |
| `profundum_voice.h` | `ProfundumVoice` (FR-001–FR-016) |
| `profundum_engine.h` | `ProfundumEngine`, `ProfundumEngineConfig`, `ProfundumNonlinearPlacement` (FR-020–FR-032, FR-060–FR-066) |

One shared header changes, append-only and off by default: `systems/mass_dynamics.h` (the FR-082 opt-in, S7).
`processors/true_peak_limiter.h` is **not** changed (C-1). Everything else is test code under `dsp/tests/unit/systems/`.

---

## S0. Verification ledger (every fact below was read in this session, 2026-10-19)

Paths are relative to `dsp/include/krate/dsp/` unless stated.

### S0.1 Voice chain

| Fact | Where |
|---|---|
| `ProfundumCore`: `kDefaultPartials = 64`, `kControlInterval = 32`, `kPitchUpdateInterval = 16`, `kMinF0Hz = 8`, `kMin/kMaxSampleRate = 22050 / 192000` | `systems/profundum_core.h:92–103` |
| `prepare(double sampleRate, int numPartials = kDefaultPartials)` is NOT RT-safe (bank calibration) and enables `setContinuousFrequencyUpdates(true)` | `profundum_core.h:121–146` |
| `setControls`: no-op before prepare; a non-finite field keeps the pending one; clamps; a bitwise-unchanged set returns without dirtying | `profundum_core.h:160–184` |
| `noteOn(float)`: "Effective at offset 0 of the next block" | `profundum_core.h:197–203` |
| `setPartialPanOffsets`: an all-zero vector never reaches the bank, "so the Anchor path stays bit-identical L/R" | `profundum_core.h:204–223` |
| `processBlock(float* left, float* right, std::size_t n, const float* f0PerSample = nullptr)`: control update at grid phase 0, pitch update every 16 samples | `profundum_core.h:224–282` |
| `applyNoteOn`: the Reset branch runs when `policy_ == Reset \|\| !sounding_`; it calls `bank_.reset()`, sets `intervalPhase_ = 0` ("the grid restarts at the note"), and seeds phases at 0 so "the first sample is 0.0" | `profundum_core.h:524–571` |
| `sounding_` is false after `initialiseState` and set true by `applyNoteOn` | `profundum_core.h:445, 606, 853` |
| `setExcitation` sets `excitationDirty_ = true` on every call (not change-only) | `profundum_core.h:356–366` |
| `stateFinite()` | `profundum_core.h:308` |
| `reset()` is documented "Control thread; allocation-free and bounded (array fills + one evaluateShape) … The RT-safe way to clear a sounding core is a Reset-policy noteOn" | `profundum_core.h:158–161` |
| `prepare` clamps the rate, `sr = std::clamp(sampleRate, kMinSampleRate, kMaxSampleRate)`, and sets the private `capHz_ = SpectralShapeRecipe::capFrequency(sr)`; `sanitizeF0` clamps every f0 to `[kMinF0Hz, capHz_]` | `profundum_core.h:104–105, 131, 143, 797–803, 842` |
| `capFrequency(double sampleRate)` returns `kNyquistCapFraction * float(sampleRate) * 0.5f`, `kNyquistCapFraction = 0.8f` | `processors/spectral_shape_recipe.h:72, 93–95` |
| Span-out signatures: `static void evaluateShape(const Controls& c, std::span<float> out)`, `static void evaluateMask(float f0Hz, double sampleRate, std::span<float> out)`, `static void applyMask(std::span<const float> shape, std::span<const float> mask, std::span<float> out)`; `applyMask` writes `n = min(shape.size(), mask.size(), out.size(), kRecipeMaxPartials)` elements and renormalises those n to P0, so the span length is part of the result | `spectral_shape_recipe.h:100, 149, 190–200` |
| `AnchorOscillator::Controls` is a plain aggregate `{ waveform, mode, level, punchBoostDb, punchTimeMs, ghostTimeMs }` ("Designated initialisers only") | `systems/anchor_oscillator.h:99–106` |
| `MassDynamicsTest::unbiasedH1Amplitude(base, double f0, double fs)` evaluates into a `std::array<float, ProfundumCore::kDefaultPartials>` (64) and returns `g[0] * kCenterPanGain` | `dsp/tests/unit/systems/mass_dynamics_test_helpers.h:288–292` |
| `SpectralShapeRecipe::evaluate` is literally `evaluateShape → evaluateMask → applyMask`; all three are public static and RT-safe (stack mask); `kCenterPanGain = 0.7071067811865476f` | `processors/spectral_shape_recipe.h:52, 100, 149, 190, 211–220` |
| `ShapeMorph::prepare(double, size_t)`, `reset()`, `setShape(float)`, `processBlock(L, R, n, f0PerSample)`, `stateFinite()`; `shape_ = 0` is ROUND (BITE at 1) | `systems/shape_morph.h:126–200, 221, 291, 620` |
| `HarmonicFold::setAmount(float)` [0, 100], valid before prepare; `setFundamentalHz(float)`; `processBlock(L, R, n)` (no per-sample f0); `stateFinite()` | `processors/harmonic_fold.h:166–188, 247` |
| `BassCharacterStage`: `setAlgorithm` (switch machine, last request wins), `setAmount` / `setTone` call `retarget()` on every call, `setChannelOffsets`, `setFundamentalHz` (bit-compare no-op), `syncPhase(float coreCycles)` ("Call after prepare()", reset clears), `setInputHalfWaveSymmetric(bool)`, `processBlock(L, R, n, f0PerSample)` | `systems/bass_character_stage.h:241–334` |
| `perceivedPitchRatio()` reads `target_` and the centre targets: Rectify → 2 iff `halfWaveSymmetric_ && flippedRect_`; Fracture → 0.5 iff `flippedFrac_`; else 1 | `bass_character_stage.h:465–480` |
| `kMaxChannelOffset = 0.15f`; `stateFinite()` | `bass_character_stage.h:84, 483` |
| `AnchorOscillator`: `Waveform`, `Mode`, `RetriggerPhase` enums; `kMinFollowRatio = 0.25`, `kMaxFollowRatio = 4`; `kMaxCoreH1Amplitude = 0.3552343859f` | `systems/anchor_oscillator.h:57–98` |
| `setControls` skips a bitwise-unchanged set; `setFollowRatio` clamps; `setCoreH1Amplitude` clamps to [0, kMaxCoreH1Amplitude] and retargets a smoother; `noteOn(float, bool rearm = true)` | `anchor_oscillator.h:224–282` |
| `processBlock(L, R, n, f0PerSample, followRatioPerSample)` writes one value to both channels ("FR-050: one value, both channels"); `phase()`, `phaseIncrement()`, `isSounding()`, `stateFinite()`, `latencySamples() == 0` | `anchor_oscillator.h:285–385` |
| `DropEnvelope`: `process()`, `currentOffset()`, `plannedCurve()` | `processors/drop_envelope.h:124, 148, 151` |
| `DropPhaseLock::trigger(const DropCurve&, float fResHz, std::size_t gridPhase, float minF0Hz, float capHz, TriggerKind)`; `setBypass(true)` clears the lock state; `process(float)` returns its input unchanged while bypassed | `processors/drop_phase_lock.h:64–147` |
| `NoteMemory::resolve(Mode, Retrigger, Transition, bool audible)`: `phaseReset = osc && r == Reset`, `declick = phaseReset && audible`, `dropTrigger = env`, `dropSnap = !audible \|\| declick`, `anchorRearm = env`; the 4 × 5 cell matrix | `processors/note_memory.h:72–130` |
| `RetriggerDeclick`: `L_d = ceil(1.5 ms · fs)`; `arm()` sets `remaining_ = L_d − 1` and is a no-op while active; `samplesUntilSwitch()`; `process()` returns 0.5·(1 + cos), exactly 0 at the switch, then 1.0f | `primitives/retrigger_declick.h:31–96` |
| `ADSREnvelope`: `prepare(float)`, `reset()`, `gate(bool)` (Hard → `enterAttack()`), `setRetriggerMode`, `isActive()`, `getStage()`; `setAttackCurve(float)` etc. regenerate a table and set `useTableProcessing_ = true`; on the table path `enterAttack()` starts from `output_` | `primitives/adsr_envelope.h:68–108, 157–176, 209, 265–268, 451–456` |
| Defaults `attackCurveAmount_ = 0.7f // Default matches EnvCurve::Exponential`, `retriggerMode_ = Hard`; `kSustainSmoothTimeMs = 5` | `adsr_envelope.h:531, 541`; `primitives/envelope_utils.h:53, 64–67` |

### S0.2 Note handling

| Fact | Where |
|---|---|
| `MonoHandler`: `prepare(double)`; `noteOn(int, int)` / `noteOff(int)` return `MonoNoteEvent`; `processPortamento()`; `setMode`, `setLegato`, `setPortamentoMode`, `setGlideMode`, `hasActiveNote()`, `reset()`; `kMaxPortamentoTimeMs = 10000` | `processors/mono_handler.h:41–46, 112, 133, 148, 224, 293–392` |
| `VoiceAllocator`: `kMaxVoices = 32`; `noteOn` / `noteOff` return `std::span<const VoiceEvent>`; a same-note retrigger and a Hard steal each emit `Steal` + `NoteOn` on the same slot; `voiceFinished`, `setAllocationMode`, `setStealMode`, `setVoiceCount` (returns NoteOff events), `reset()` | `systems/voice_allocator.h:102–115, 193, 228–330, 456, 844–870, 923–968` |
| `findStealVictimSingle`: releasing first, then active; `findVictimOldest` takes the lowest note-on timestamp within that state | `voice_allocator.h:631–685` |
| `NoteProcessor`: `setPitchBend(float)` ignores NaN / Inf; `processPitchBend()` computes `currentBendSemitones_ = smoothedBend * pitchBendRange_; currentBendRatio_ = semitonesToRatio(currentBendSemitones_)`; `setPitchBendRange` clamps [0, 24] with **no** non-finite guard; no public ratio accessor besides `getFrequency(note)` | `processors/note_processor.h:51–229` |
| Phase 4 harness transition flow (the reference this voice replaces): classification, `resolve`, declick deferral to `pos + samplesUntilSwitch()`, last call wins during a dip, `applyTransition` (policies, Reset branch, Drop trigger kind, Hard gate), per-sample `fCore = fRes * std::exp2(dc / 12.0f)`, Steal + NoteOn → VoiceReuse | `dsp/tests/unit/systems/profundum_note_behaviour_test_helpers.h:318–345, 447–592, 645–760` |

### S0.3 Global chain and control layer

| Fact | Where |
|---|---|
| `BassMorphEngine`: `Resolved { MorphState state; float position, anchorPosition, anchorMass; }` (no mode field); `prepare(double)`, `reset()`, `setEndpoint`, `setPosition`, `setPositionRampMs`, `setAnchorMorphMode`, `anchorMorphMode()`, `selection(SelectionGroup)`, `advanceInterval()`, `resolved()`, `serializeRuntime(std::byte*, size_t)`, `restoreRuntime(const std::byte*, size_t)`; `AnchorMorphMode { Lock, Blend, Punch }` | `systems/bass_morph_engine.h:46–60, 88–227, 175, 178–183, 274` |
| Five selection groups `{Character, ImageMode, FieldMotion, Motion, Anchor}`; the four main ones select on p, Anchor on p_a (`updateSelections`); `curve(m, p)`: Lock 0, **Blend p**, Punch p³; `advanceInterval` sets p_a's target to `curve(mode_, p)` after p's step, both slews at the same rate; `ruleSelection` rule 3 hysteresis applies when both governing amounts are non-zero (Character `characterAmount`, ImageMode `field.spread`, FieldMotion `field.motionDepth`, Motion `motion.amount`, Anchor `anchor.level`) | `bass_morph_engine.h:184–196, 206–224, 368–373, 376–405, 414–423, 505–506` |
| `MassDynamics::compressor()` returns the `BassCompressor`; `BassCompressor::currentGainReductionDb()` | `systems/mass_dynamics.h:571, 272` |
| `semitonesToRatio(float)`; `midiNoteToFrequency`; `detail::isFinite(float / double)` | `core/pitch_utils.h:23`; `core/midi_utils.h:66–70`; `core/db_utils.h:118–125` |
| `MorphState`: `AmpEnvelopeFields` (7 floats, curves [−1, +1]) and `MotionConfig` (16 leaves; `cycleShape` is `::Krate::DSP::Waveform`); members `recipe` … `anchorFoldFeed` | `systems/morph_state.h:71–122` |
| `MassLaw`: seven rows (AnchorLevel: lo 0, hi 1, ±0.5, Linear); `evaluateRow` clamps a finite Mass to [0, 100]; `setMass` clamps; `setBase` clamps per row; `advanceInterval()` is one slew step; `currentTargets() = evaluate(smoothedMass(), base_)`; `base()` | `systems/mass_law.h:100–200` |
| `MassDynamics`: `prepare(double, size_t)`; setters latch on the absolute 32-sample grid; `processBlock` splits runs on the grid, shaper → compressor → trim; `stateFinite()` | `systems/mass_dynamics.h:469–590` |
| `BassCompressor::prepare` clamps fs to [22050, 192000]; `kMaxRuleIntervals = 1800`, "300 ms / 32 samples at 192 kHz" | `mass_dynamics.h:72–73, 110–142` |
| `PeriodPeakDetector::kPeakOversampling = 4` | `processors/period_peak_detector.h:39–41` |
| `PolyphaseHalfbandOversampler` (L1): heap-free, copyable, per-sample `upsample(float, float*)` / `downsample(const float*)`, `setFactor(2)`, `reset()`, `stateFinite()`; "Zero look-ahead … low-frequency group delay about 1.9 base-rate samples per direction" | `primitives/polyphase_halfband_oversampler.h:20–117` |
| `BassStereoField`: `setControls` latches until `advanceControl()`; `setTempoBpm` guards non-finite; `computePanVector(VoiceState&, float, std::array<float, kMaxPartials>&, int)` is const on the field; `wideOffsets(Algorithm)` is const; `processBlock` writes `outL = outR = mid` wherever `sp == 0` | `systems/bass_stereo_field.h:234–257, 274–285, 293–336, 349–393, 413–420, 437–481` |
| `sideIdle()` is the "SOLID settled" observer; it flips inside `processBlock` when the mode ramp reaches 0 with target 0 | `bass_stereo_field.h:500, 553–558` |
| The field clocks its LFO once per interval (`lfo_.prepare(controlRate)`) and its Drift by `processBlock(kControlInterval)` | `bass_stereo_field.h:181–187, 311–314` |
| `DCBlocker` has a defaulted copy ("trivially copyable for per-channel use"); `prepare(double, float cutoffHz = 10)`; `setState(x1, y1)` | `primitives/dc_blocker.h:114–117, 135, 165` |
| The Phase 6 chain fixture prepares its DC blockers at 5 Hz ("plan S1 C-10") | `dsp/tests/unit/systems/mass_dynamics_test_helpers.h:356` |
| `TruePeakLimiter`: per-channel state is `Oversampler<4, 1> osL_, osR_` plus `std::vector` scratch; the detector takes `tp = max(\|inL\|, \|inR\|, the 4 phases of both)`; ONE `currentGain_`; `left = clamp(inL·g)`, `right = clamp(inR·g)`; `setCeilingDb` has no finite guard; `prepare` assigns the scratch (`assign(maxBlockSize·4)`) | `processors/true_peak_limiter.h:60–76, 86–90, 122–181` |
| `Oversampler` copy is deleted, move is defaulted | `primitives/oversampler.h:271–276` |
| `LFO` (no base class): `prepare(double)`, `process()`, `setWaveform`, `setFrequency`, `setPhaseOffset`, `setTempoSync`, `setTempo`, `setNoteValue`, `retrigger()` (no-op unless enabled), `setRetriggerEnabled`, `setFadeInTime` | `primitives/lfo.h:63–298` |
| `BrownianDrift : ModulationSource`: `prepare`, `reset` (re-seeds), `setSeed`, `setSmoothness`, `setDepth`, `setMean`, `processBlock(n)`, `getSourceRange() = {−1, 1}` | `processors/brownian_drift.h:94–219` |
| `deriveStreamSeed(base, salt)`: lowbias32, never 0 | `core/random.h:96–110` |
| `applyModCurve(curve, x)` clamps x to [0, 1]; Linear x, Exponential x², SCurve x²(3 − 2x), Stepped floor(4x)/3; it returns 0 at x = 0 for the three non-Stepped curves | `core/modulation_curves.h:37–50` |
| Vorago contribution arithmetic `amount · applyModCurve(curve, \|g\|) · sign`, g = (m − 0.5)·2, with "DELIBERATELY NO if (neutral) return; FAST PATH" | `systems/vorago_macro_matrix.h:1162–1195` |
| Vorago voice AR-2 (no router, no virtual dispatch on the audio thread) and D3 / D4 (steal fade captured at serve time) | `systems/vorago_voice.h:34–64` |
| Vorago engine: per-voice seeds `deriveStreamSeed(seed_, kVoiceSaltBase + v)`; `allocator_.setAllocationMode(Oldest)`, `setStealMode(Hard)` at prepare | `systems/vorago_engine.h:368–378, 626–631` |

### S0.4 Test infrastructure

| Fact | Where |
|---|---|
| `TestHelpers::AllocationScope` (count read after the scope closes) | `tests/test_helpers/allocation_detector.h:111–131` |
| `render_fingerprint.h`: `kSampleTolerance = 5e-4`, `fingerprintRender`, `compareFingerprints` | `tests/test_helpers/render_fingerprint.h:55–122` |
| `goertzelPhase`, `aliasFftLength(fs, f0)`, `aliasedPower(x, fftLen, fs, f0)` | `dsp/tests/unit/systems/profundum_core_test_helpers.h:157, 257, 273` |
| `anchorShsF0(x, fs, f0Hint)`, the Phase 3 offline estimator over `kLowFrequencyFftSize` | `dsp/tests/unit/systems/anchor_oscillator_test_helpers.h:215–227` |
| Phase 8 readers `dCxDb`, `dNlDb` (character over the **linear** core), `dStDb`, `dStCore`, `dBrOct`, `lKDb`, `eSubDb`, `dTMs`; `MorphChainTaps` derives `FieldChainTaps` | `dsp/tests/unit/systems/bass_morph_test_helpers.h:679–940`; `bass_stereo_field_test_helpers.h:136–145` |
| `showcasePair()`: A = `kSineAnchor`, everything clean, **ampEnv attack 1 / decay 150 / sustain 0 / release 50**, SOLID, Anchor level 1; B = kHeavy with edge 1, excitation 1 / 1 / 0.5, Fold 80, SHAPE 0.5, Character Fold 0.5 / Tone 0.8, ampEnv 1 / 2000 / 0.8 / 800, WIDE spread 1, Anchor level 0.5 | `dsp/tests/unit/systems/bass_morph_state_helpers.h:165–215` |
| `kSubBandQBarDb = 1.0`, `kSubBandRhoBar = 0.99`, `bandReading(L, R, n, fs, fLo, fHi)` | `bass_stereo_field_test_helpers.h:95–97, 736` |
| CPU runner filter `[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]` | `tools/run-cpu-tests.js:56` |
| `mass_dynamics.h` consumers: only `dsp_systems_tests` TUs (Phases 6–8) and the header list; none in `plugins/` | `grep -rln mass_dynamics.h dsp/ plugins/ tests/` (16 hits) |
| Phase 7 / Phase 8 TU lists and the `-fno-fast-math` block | `dsp/tests/CMakeLists.txt:743–786, 1282–1286` |
| The `KRATE_DSP_SYSTEMS_HEADERS` list ends with the Phase 8 pair | `dsp/CMakeLists.txt:204–219` |

### S0.5 Recorded costs used for sizing (S13; different builds, not evidence)

| Figure | Where |
|---|---|
| Phase 2 chain (core + excitation + Fold) 1.708 % linked; **stereo-forced chain 2.462 %** (record) | `specs/profundum-phase2-edge-fold/compliance.md:7` |
| Phase 5 Shape + Character: max_linked 1.8463 % (Fracture, SHAPE 0); **max_unlinked 3.8755 % (Tube, SHAPE 0.25)**; char_alone Tube 0.9916 %, Fracture 0.8350 %; worst crossfade 2.6501 % | `specs/profundum-phase5-shape-character/compliance.md:100`; `…/evidence/sc015-cpu.txt` |
| MassDynamics 0.9908 %; DC ×2 + limiter 1.2346 % | `specs/profundum-phase6-mass-dynamics/evidence/sc017-cpu.txt:2–3` |
| Morph 0.0358686 % of a core per interval | `specs/profundum-phase8-bass-morph/evidence/phase-handoffs.txt:96–97` |

---

## S1. Spec conflicts, refinements and rulings

### C-1 (amendment, **FOR RATIFICATION**; FR-029 / Q7, FR-084, SC-030): the limiter needs no state copy

FR-029 asks that "every per-channel state of the limiter is copied L → R", with an append-only accessor "if
`TruePeakLimiter` exposes no state copy". It exposes none, and its per-channel state cannot be copied without a Layer 1
change: the two detection upsamplers are `Oversampler<4, 1>`, whose copy is deleted (oversampler.h:271–272) and whose
filter arrays are private. The copy is also **unnecessary for the ruling's purpose** (L == R bitwise from the crossfade
end, no settle):

- the limiter's only per-channel state feeds the detector `tp = max(|inL|, |inR|, the 4 phases of both)`
  (true_peak_limiter.h:142–147), and the result drives ONE shared `currentGain_` (:155–158);
- the outputs are `clamp(inL·g)` and `clamp(inR·g)` with the same g and the same ceiling (:163–164).

Whenever the limiter's inputs are L == R bitwise, its outputs are L == R bitwise **whatever the R upsampler holds**. The
residual changes only the shared gain, identically on both channels. The DC blockers are the only stage whose
per-channel state reaches the output separately, and they are copied (C-2). Ruling: no limiter accessor and no second
shared header; SC-030's list is `mass_dynamics.h` only. SC-015 gates the consequence bitwise.

### C-2 (refinement, FR-029): "the crossfade end" is the first grid point at which `sideIdle()` reads true

`BassStereoField::sideIdle()` is the shipped "SOLID settled" observer (bass_stereo_field.h:500). It flips inside
`processBlock` at the sample where the mode ramp reaches 0 with target 0 (:553–558), and from that sample on the stage
writes `outL = outR = mid` (:471–478). The engine reads it at each grid point **before** that interval's global chain
runs. At the first grid point where it is true, it executes `dcR_ = dcL_` (DCBlocker's defaulted copy,
dc_blocker.h:115) and latches `dcSynced_`; `dcSynced_` clears whenever `sideIdle()` reads false. Between the flip sample
and that grid point (≤ 31 samples) the DC outputs still differ. SC-015's "from the crossfade end" therefore means **from
that grid point**, which the test reads from the call log (`DcResync` record, S8). The copy runs once per SOLID entry.

### C-3 (refinement plus one **FOR RATIFICATION** clause; FR-082, FR-084, Q4): the `MassDynamics` opt-in runs the dynamics at 2·fs

Mechanism (S7): `MassDynamics` gains `setOversampling(bool)`. When it is active, each grid-aligned run is upsampled 2×
by a pair of `PolyphaseHalfbandOversampler` (L1, heap-free, zero look-ahead; polyphase_halfband_oversampler.h:20–117),
the **unchanged** `TransientShaper` and `BassCompressor` run at 2·fs, the trim is applied per 2× sample, and the run is
decimated. This is exactly "2× oversampled gain application plus 8× detectors": both gains are applied at 2·fs, and the
detectors are `PeriodPeakDetector`'s 4× true-peak (period_peak_detector.h:40) on a 2·fs signal, i.e. 8× the base rate.
No Layer 2 header changes. The off path is the existing code, untouched, so every existing caller is bit-identical.

**FOR RATIFICATION:** `BassCompressor::prepare` clamps its rate to 192 kHz (mass_dynamics.h:110–112) and its rule ring
is sized for 192 kHz (:72–73), so the opt-in is **active only for fs ≤ 96 kHz** (2·fs ≤ 192 kHz) and is a recorded no-op
at 192 kHz. SC-021 measures 44.1 / 48 / 96 kHz only, so no gate is affected. The 192 kHz no-op goes to the Phase 10
handoff.

### C-4 (ruling of D-8; FR-016, SC-005, FR-086): the steal "fade" is the Note Memory declick, with no second mechanism

A POLY Hard steal reaches the victim voice as `Steal` + `NoteOn` (voice_allocator.h:844–870, 923–968). Exactly as in the
Phase 4 harness (profundum_note_behaviour_test_helpers.h:323–345) it is classified as `Transition::VoiceReuse`, and
`NoteMemory::resolve` decides:

- under Retrigger = Reset on an audible voice, `declick = true` (note_memory.h:79–90). The old content is faded by the
  raised-cosine dip (3 ms in the voice, ruling R-B 2026-10-20; Phase 4's 1.5 ms default untouched), and the restart lands on the switch sample, where the dip is exactly 0
  (retrigger_declick.h:77–88);
- under Continue, or where a mode's VoiceReuse cell does not restart the oscillator, nothing restarts discontinuously:
  the pitch moves phase-continuously, the envelope Hard-retriggers from its current level (adsr_envelope.h:451–456), and
  the Drop is bridged.

The Vorago D3 / D4 capture exists because a Vorago steal cannot render a fade (vorago_voice.h:56–64). Here the dip
renders the fade, at the cost of ≤ L_d samples of onset latency (Phase 4 SC-011's accepted latency). Ruling:

- the steal-fade factor in FR-015's g_d is **identically 1.0f**, so g_d is the dip;
- the seam `disableStealFade` suppresses the dip for **steal-originated** transitions only, and `disableDeclick` for
  every other restart. SC-005's two positive controls each disable their own path and must fail (a) or (b);
- S_max for SC-005(b) / SC-006(b) is the dip's analytic peak slope π / (2·L_d) (S9.9).

### C-5 (refinement; FR-008, SC-033): the Rectify input rule uses a period-synchronous DFT with hysteresis inside the band

The voice measures h1 and h2 of the Character input (the mid of the Fold output) with an integrate-and-dump DFT over
each period of f_core (S9.6). The flag is set **true** when the last complete period reads h2/h1 ≤ **−22 dB**, set
**false** when it reads ≥ **−18 dB**, and held in between. Both thresholds lie inside the recorded uncertainty band
(−24 … −16 dB), so the spec's "true exactly when h2 < −20 dB, for states outside the band" holds, and the flag cannot
chatter. The flag is handed once per interval, change-only. The meter runs at the frequency the core actually plays,
f_core clamped to the core's own `[kMinF0Hz, capHz]` (S9.6), so its state stays bounded for every note and bend the
engine accepts.

### C-6 (refinement, FR-005): a₁ is computed in two cached stages, bitwise equal to `evaluate`

`evaluate` is literally `evaluateShape → evaluateMask → applyMask` (spectral_shape_recipe.h:211–220), and all three are
public. The voice caches the shape (recomputed only when one of the six recipe fields changes bitwise) and the mask
(recomputed only when f changes bitwise), and runs `applyMask` (which includes the P0 renormalisation) when either
changed. All three are span-out functions, and `applyMask` renormalises over the span length it is given
(spectral_shape_recipe.h:190–200), so every buffer is a `std::array<float, 64>` passed as a span of exactly
`kNumPartials` = 64, the length `unbiasedH1Amplitude` uses (mass_dynamics_test_helpers.h:289–290). The result is
bit-identical to calling `evaluate` every interval (same functions, same order, same length) at a fraction of the
cost. f is the
Anchor's pitch input at the **first sample of the interval** (f_res in LOCK / PUNCH / GHOST, f_core in FOLLOW), produced
by the voice's pitch stage before any setter (S5.4).

### C-7 (amendment, **FOR RATIFICATION**; FR-052, SC-027(a)): the compression row offsets the law's output

FR-052 drafts "the row offsets the Mass law's output, then clamps", and this plan adopts it: the handed compression
amount is `clamp(t.compressionAmount + c_TL, 0, 1)`, where c_TL is the Tight ↔ Loose compression contribution (the add is
skipped when c_TL is ±0). The alternative, offsetting the law's CompressionAmount base, was rejected: the base clamps at 0
(`setBase` clamps per row, mass_law.h:170–175, and the default base is 0), so the Loose half would be inert at every
Mass: a negative base clamps to 0 before the law adds its Mass term. After the law, the Loose half acts wherever the
law's compression is above 0 (Mass > 50); at Mass ≤ 50 the law already hands 0 and nothing can lower it. The Tight half
acts at every Mass in both forms. Consequence for SC-027(a), whose text says
the `MassDynamics` receivers equal `evaluate(...)` bitwise: **the compression receiver is compared with
`clamp(evaluate(...).compressionAmount + c_TL, 0, 1)`**, which is the bare law value whenever Tight ↔ Loose is at its
neutral. SC-027 runs with all macros neutral, plus one arm at Tight ↔ Loose = 0.2 that checks the formula.

### C-8 (refinement; FR-080, SC-007, SC-010): base "A" carries `ampEnv.sustain = 1`

Showcase A has decay 150 ms and sustain 0 (bass_morph_state_helpers.h:178–181). Every steady descriptor read on it would
read silence after 150 ms, and the release row AT-25 would release from 0. Phase 8 read its steady descriptors with no
envelope at all; the Phase 9 voice always has one (FR-009). So **every row whose base is A uses A with
`ampEnv.sustain = 1`** ("A_s"), and the CSV names "+ sustain 1 (C-8)" in each such row. B keeps its own 0.8. This is the
spec's own mechanism ("+" names any other field the row changes); no row is removed and no bar moves.

### C-9 (refinement; FR-063, FR-020): the event model

Every event (note-on / note-off, pitch bend, tempo, each FR-031 setter, an endpoint, the morph position) is **latched at
the engine's current sample position and applied at the first grid point at or after it**. The engine API therefore
carries no sample offsets: the caller (the test driver now, the Phase 10 processor later) splits the host block at each
event's offset and issues the event before rendering that sample. Because every event is applied on a grid point, the
render is independent of the host partition (FR-063, SC-012). Note events go into a fixed queue of
`kMaxQueuedEvents = 256` entries. A full queue drops the incoming note-on and counts it; a note-off is never dropped (it
is recorded in a fixed 128-entry pending-release flag array that the next grid point drains). SC-022 asserts a drop count
of 0 in its schedule.

### C-10 (refinement; FR-015, FR-004, SC-019): the voice-output order is FR-015's, literally

The voice always evaluates `(c·g_amp)·g_d + (a·g_amp)·g_d` per channel and sample, with g_d exactly 1.0f when idle (the
dip returns 1.0f when inactive, retrigger_declick.h:77–79, and a multiply by 1.0f is exact). SC-019(b) is therefore a
bitwise identity with no branch to reproduce. The fold feed `x + g·a` is **skipped**, not multiplied by 0, for every
interval whose ramp endpoints are both +0 and whenever `anchorDetached` is on: `x + 0·a` can turn a −0 input into +0,
which would break FR-004's bit identity at g = 0.

### C-11 (refinement, SC-020): the pitch arithmetic is fixed, so the gate is bitwise

- Bend ratio: `ratio = semitonesToRatio(b * range)`, with b = `NoteProcessor::processPitchBend()` and range the engine's
  sanitised copy. That is the float sequence `processPitchBend` itself executes (note_processor.h,
  `currentBendSemitones_` / `currentBendRatio_`).
- MONO: `f_res = monoHandler.processPortamento() * ratio`. POLY: `f_res = midiNoteToFrequency(note) * ratio`
  (`VoiceEvent::frequency` is not used; it carries the allocator's own tuning).
- `f_core = f_res * std::exp2(d_c / 12.0f)`, the Phase 4 harness expression (profundum_note_behaviour_test_helpers.h:663).

SC-020's reference executes the same expressions with its own components, so **SC-020 is gated bitwise**; the 1e-4 cent
reading is printed beside it.

### C-12 (refinement, FR-022): velocity is not used

No FR maps velocity in Phase 9. `VoiceEvent::velocity` and `MonoHandler`'s velocity argument are forwarded where a
component needs a valid value and are otherwise ignored. Recorded for Phase 11.

### C-13 (refinement; FR-040, FR-045, FR-046): the Motion sources run at the control rate

ENV is an `ADSREnvelope` prepared at fs / 32 with sustain 0, attack `envAttackMs`, decay `envDecayMs`, release
`envDecayMs`. It is gated **on** at each envelope-restarting note event and never gated off, so it is an AD contour that
rests at 0. CYCLE is an `LFO` prepared at fs / 32 and advanced by one `process()` per interval. DRIFT is a
`BrownianDrift` advanced by `processBlock(32)` per interval. This is the field's own precedent (bass_stereo_field.h:186,
312). All three advance every interval whichever is selected, so a source switch never restarts a clock.

### C-14 (refinement; FR-014, SC-004): a measurable post-sum variant exists, and its semantics are stated

SC-004 must measure the post-sum alternative, so `ProfundumEngineConfig::placement` ∈ {PerVoice (default), PostSum}
exists as a configuration-time choice. In PostSum, each voice renders its core (with excitation) and its Anchor,
multiplies **both** by g_amp·g_d, and adds the core into a core bus and the Anchor into an Anchor bus. One global
ShapeMorph → (+ the fold-feed bus) → HarmonicFold → BassCharacterStage runs on the core bus at f0 = the lowest sounding
f_core, and the Anchor bus is added after it. **The amp envelope therefore acts before the nonlinear stages in PostSum**,
which makes it a different instrument in POLY. FOLLOW per voice reads the global stage's ratio. If SC-004 shows that
PerVoice breaches SC-002 while PostSum meets it, the FR-014 ruling is written with these consequences: the per-voice
Character tap becomes the global one, SC-019(b) is read on `(core·g_amp)·g_d` per voice, and D_nl and SC-017 read the
global tap. MONO always uses PerVoice.

### C-15 (refinement; FR-025, FR-064, SC-024): the engine renders in control intervals, so no stage depends on the host block size

The engine processes every host block as a sequence of chunks that never cross a grid point (≤ 32 samples). Every
component is prepared with `maxBlockSize = 32` (`TruePeakLimiter::prepare(fs, 32)` sizes its scratch to 128 floats), so
a re-prepare at any host block size performs the same assigns on the same sizes. SC-024's "0 allocations at an unchanged
maxBlockSize" then also holds at a larger one; the config carries no `maxBlockSize`.

### C-16 (refinement; SC-001, Assumptions): the "worst Character algorithm" is two gated cells

Phase 5's record names Fracture as the linked argmax (1.8463 %) and Tube as the unlinked argmax (3.8755 % at SHAPE 0.25)
and the costliest algorithm alone (0.9916 %) (sc015-cpu.txt). SC-001 / SC-002 run WIDE, i.e. unlinked, so **Tube and
Fracture** are both gated cells in the same lane, and the verdict is the worse of the two.

### C-17 (adoption): drafted values adopted as written

The FR-080 bars (dB ≥ 3, centroid ≥ 0.25 oct, time ratio ≥ 2, Anchor ΔE_sub ≥ 2 dB, Drop rows by Phase 4 SC-021); the
SC-007 ε (0.25 dB, 0.02 oct, 2 %); the SC-014 sources (CYCLE 0.5 Hz on Depth / Body, DRIFT on Edge / Shift); D-3
(`ProfundumMotion` is its own class, the unit SC-009 / SC-025 / SC-026 test); FR-022 "no POLY glide"; the FR-023
mode-switch behaviour; the edge case "a seed change takes effect at the next prepare / reset".

### C-18 (refinement, FR-047): every `MotionConfig` field is realised; none is a deviation

| Field | Realised by |
|---|---|
| source, target, amount, depth, polarity | `ProfundumMotion::advanceInterval` (S9.1) |
| cycleRateHz, cycleShape, cycleSync, cycleNote, cycleModifier, cyclePhaseDeg, cycleRetrigger, cycleFadeInMs | `LFO::setFrequency / setWaveform / setTempoSync / setNoteValue / setPhaseOffset / setRetriggerEnabled` (+ `retrigger()` at a note event) `/ setFadeInTime`, each on change (FR-046) |
| driftSmoothness | `BrownianDrift::setSmoothness`, on change |
| envAttackMs, envDecayMs | `ADSREnvelope::setAttack`, `setDecay`, `setRelease(envDecayMs)`, on change |

All seven `AmpEnvelopeFields` are realised one-to-one by `ADSREnvelope` (FR-009). No append to `MorphState` and no
version bump.

### C-19 (refinement, FR-032): seeds and salts

`kDefaultSeed = 0xB455u`; salts `kMotionDriftSalt = 1`, `kFieldDriftSalt = 2`, `kVoiceSaltBase = 16`. The voice salt is
reserved: no per-voice random component exists today, so per-voice seeds are derived and stored but consumed by nothing.
`setSeed` stores the seed, and it takes effect at the next `prepare` / `reset`, so a running DRIFT never jumps.

### C-20 (refinement; FR-013, FR-054): every setter the voice and the engine issue is change-only

FR-013 forbids repeated calls only where a component re-arms. Several shipped setters do more than store, on every call:
`ProfundumCore::setExcitation` dirties unconditionally (profundum_core.h:365), `BassCharacterStage::setAmount / setTone`
call `retarget()` every time (bass_character_stage.h:258–268), `ADSREnvelope::set*Curve` rebuilds a table
(adsr_envelope.h:160–176), and `MassDynamics::setOutputTrimDb` evaluates `dbToGain`. So the voice and the engine keep a
"last handed" copy of every argument and call a setter **only on a bitwise change**; the first interval after prepare /
reset hands everything. This makes FR-054 / SC-008(b) a property of the code and removes per-interval cost. The call log
(S8) records every step with an `issued` flag, so SC-032's sequence is fixed whether or not a call was issued.

### C-21 (refinement, **FOR RATIFICATION**; SC-021(c), H-9): the 48 st Drop aliasing cell reads the core tap

SC-021(c) carries Phase 4's −96 dBFS bar and "Phase 4's rendered aliased-power method", which Phase 4 applied to the
core's rendered output (Phase 4 SC-015). The Drop acts on the core; the nonlinear stages after it are gated at −80 dBFS by
SC-021(a) / (b) on held notes. So (c) is gated on the **core tap** at −96 dBFS, and the Sum tap's reading over the same
Drop is recorded beside it.

### ⚠ R-A (RISK; SC-001, SC-002, FR-083): the sizing predicts a breach of both ceilings

Summing the recorded figures (S13) for the SC-001 setting, where WIDE makes every per-voice stage run unlinked, predicts
**≈ 10.3 % MONO** (bar 8 %) and **≈ 30.8 % at 4 voices** (bar 25 %). The spec's F-7 used the *linked* Phase 5 figure;
the unlinked one is 2.1× higher. These are cross-build figures that only size the work, but the gap is too large to
ignore. **Probe P-1** (S10.4) measures the real composition, alone and pinned, **before** any gated render (T009). The
levers, in order, none of which relaxes a bar or changes a sound:

1. L1: change-only setters (C-20), the a₁ cache (C-6), idle voices not rendered (S6.6), the global chain per interval
   (C-15). Built in from the start.
2. L2: PostSum placement in POLY (C-14), adopted only by FR-014's own criterion (SC-004).
3. L3: the H-16 `db_utils.h` run-time fast path. **This is a shared-header change that SC-030 as written turns red**, so
   it needs a user ruling (R-D).

**Stop rule:** if P-1 reads MONO > 8 % with L1 in place, or 4 voices > 25 % with L1 and L2 available, the build stops and
the main loop takes the per-stage breakdown to the user. The options it carries: approve L3; approve an append-only cost
fix in a Phase 2 / Phase 5 component (unlinked Shape + Character dominates); or another ruling. Relaxing a ceiling is not
offered.

### ⚠ R-B (RISK; SC-005(a), SC-006(a)): the 1.5 ms dip may out-slope a clean low reference

On a sine-like voice at E1 the steady per-sample slope is ≈ 2π·41.2 / 48000 ≈ 0.0054 of its peak, while the dip adds up
to π / (2·72) ≈ 0.0218 of the peak (S9.9). SC-005(a) / SC-006(a) compare the transition's max |Δy| with the reference's
(the old note continuing plus the new note starting on an idle voice) × 1.06. On base A_s at E1 the reference's maximum
comes mostly from the new note's 1 ms attack, so the margin is unknown. **Probe P-2** renders the worst cells (A_s, E1
and C3, Reset, held victim, SOLID, host block 1) before the grid runs. **Stop rule:** a fail of (a) on any cell where (b)
passes goes to the user with the readings. `RetriggerDeclick`'s 1.5 ms is Phase 4's shipped constant and is not changed
here. **Outcome (2026-10-20):** P-2 first failed (a) and (b) on every cell (a 0.28 step: the chain's memory of the old
note reappearing at full gain at the switch — fixed by the switch-sample chain reset, FR-011); after the fix four E1
cells failed (a) by 7–51 % with (b) passing everywhere; the user ruled a 3 ms dip through an append-only length parameter
on `RetriggerDeclick` (R-B).

### ⚠ R-C (RISK; SC-021(b), FR-082): −80 dBFS at the output with the opt-in on is unmeasured

Phase 6 recorded the Fold-100 cells at −45.8 … −55.0 dBFS steady (Phase 6 compliance, SC-010 row). The opt-in is the fix
Phase 6 proposed, but no one has measured that it reaches −80. **Probe P-3** (S10.4) measures the Fold-100 E2 cells at
44.1 and 48 kHz with the opt-in off and on, through `MassDynamics` alone, before the engine exists (T005). **Stop rule:**
if the opt-in does not reach −80 dBFS on those cells, the build stops and the readings go to the user. Q4 ruled the bar;
this plan does not move it.

### ⚠ R-D (RISK; FR-083 H-16 vs SC-030): the fast-path lever conflicts with SC-030's header list

H-16 ("`db_utils.h` run-time fast path as a CPU lever, used only if SC-001/002 need it") is a change to a Layer 0 header
that every plugin consumes; SC-030 lists only `mass_dynamics.h`. Using L3 therefore needs a ruling that adds
`core/db_utils.h` to SC-030 with its consumer gate (every suite). The plan does not use L3 without that ruling.

---

## S2. Architecture

```
ProfundumEngine (L3, profundum_engine.h)
 ├─ event queue (fixed) ─→ MONO: MonoHandler ─┐          POLY: VoiceAllocator (Oldest, Hard) ─┐
 │                                            └→ classify → NoteMemory (per voice) → transition ┘
 ├─ NoteProcessor (pitch bend, per sample)
 ├─ BassMorphEngine ─→ Resolved ─→ ProfundumMacroMatrix (offsets) ─→ ProfundumMotion (offsets)
 │                                         └──────────→ MassLaw (base, Mass) ─→ targets, Anchor level
 ├─ BassStereoField (law per voice + safety stage)
 ├─ ProfundumVoice × kMaxVoices (8)
 │    pitch:  f_res ─→ DropEnvelope ─→ DropPhaseLock ─→ f_core = f_res·2^(d_c/12)
 │    main:   ProfundumCore(f_core, pan) ─→ ShapeMorph ─→ (+ g·Anchor) ─→ HarmonicFold ─→ BassCharacterStage = c
 │    anchor: AnchorOscillator(f_res, or f_core in FOLLOW) = a     (L == R)
 │    out  += (c·g_amp)·g_d + (a·g_amp)·g_d                        (ADSREnvelope, RetriggerDeclick)
 └─ sum ─→ MassDynamics (2·fs opt-in) ─→ BassStereoField::processBlock ─→ DCBlocker L, R ─→ TruePeakLimiter ─→ out
```

| Header | Layer | Includes (all downward, or Layer 3 peers) |
|---|---|---|
| `profundum_motion.h` | 3 | `core/db_utils.h`, `core/modulation_source.h`, `core/note_value.h`, `primitives/adsr_envelope.h`, `primitives/lfo.h`, `processors/brownian_drift.h`, `systems/morph_state.h` |
| `profundum_macro_matrix.h` | 3 | `core/db_utils.h`, `core/modulation_curves.h`, `core/modulation_types.h`, `primitives/envelope_utils.h`, `systems/morph_state.h` |
| `profundum_engine_observer.h` | 3 | `systems/morph_state.h`, `systems/bass_morph_engine.h` (`Resolved`), `systems/mass_law.h` (`Targets`); `<cstdint>`, `<cstddef>`, `<span>` |
| `profundum_voice.h` | 3 | `core/db_utils.h`, `core/midi_utils.h`, `primitives/adsr_envelope.h`, `primitives/retrigger_declick.h`, `processors/drop_envelope.h`, `processors/drop_phase_lock.h`, `processors/note_memory.h`, `processors/spectral_shape_recipe.h`, `processors/harmonic_fold.h`, `systems/profundum_core.h`, `systems/shape_morph.h`, `systems/bass_character_stage.h`, `systems/anchor_oscillator.h`, `systems/bass_stereo_field.h`, `systems/morph_state.h`, `systems/profundum_engine_observer.h` |
| `profundum_engine.h` | 3 | `systems/profundum_engine_observer.h`, `systems/profundum_voice.h`, `systems/profundum_motion.h`, `systems/profundum_macro_matrix.h`, `systems/bass_morph_engine.h`, `systems/mass_law.h`, `systems/mass_dynamics.h`, `systems/bass_stereo_field.h`, `systems/voice_allocator.h`, `processors/mono_handler.h`, `processors/note_processor.h`, `primitives/dc_blocker.h`, `processors/true_peak_limiter.h`, `core/random.h`, `core/pitch_utils.h` (`semitonesToRatio`), `core/midi_utils.h` (`midiNoteToFrequency`), `core/db_utils.h` (`detail::isFinite`) |

The include graph is acyclic: `profundum_engine_observer.h` includes neither the voice nor the engine; the voice
includes the observer header (it reads seam fields and calls the observer's virtual callbacks, S5.4–S5.6, which need
complete types); the engine includes both. Every symbol a header uses is included directly, never through a
transitive path (e.g. `pitch_utils.h` is not relied on through `note_processor.h:18`).

There is no `effects/` include (FR-001); `node tools/lint-layers.js` checks it. All five headers are added to
`KRATE_DSP_SYSTEMS_HEADERS` (`dsp/CMakeLists.txt:204–219`) under a "Profundum Phase 9" comment.

**ODR sweep** (2026-10-19, `grep -rnw "<Name>" dsp/ plugins/ tests/`; the header name `profundum_engine_observer` and
the observer type names re-swept 2026-10-20: 0 hits): 0 definitions for `ProfundumVoice` (one comment
mention), and 0 hits for `ProfundumEngine`, `ProfundumEngineConfig`, `ProfundumEngineSeams`, `ProfundumEngineObserver`,
`ProfundumMotion`, `ProfundumMacro*`, `ProfundumVoiceTaps`, `ProfundumEngineTaps`, `ProfundumCall`,
`ProfundumCallRecord`, `ProfundumGridRecord`, `ProfundumNonlinearPlacement`. Test helpers live in
`namespace Krate::DSP::ProfundumTest` with a `VoiceEngine` / `voiceEngine` prefix (0 hits). Nested types
(`ProfundumVoice::HarmonicBalanceMeter`, `ProfundumEngine::VoiceMode`) need no sweep. The hazards stay untouched:
`CharacterProcessor`, `SubOscillator`, `StereoField`, `MorphEngine`, `TransientDetector`, `MacroMapper`, and the two
`VoiceState`s (finding F-6: the engine always writes `Krate::DSP::VoiceState` and `BassStereoField::VoiceState` in full).
`node tools/lint-odr.js` runs at close.

---

## S3. `ProfundumMotion` (FR-040–FR-049) — `systems/profundum_motion.h`

### S3.1 Public API

```cpp
namespace Krate::DSP {
class ProfundumMotion {
public:
    using Config = MorphState::MotionConfig;
    static constexpr std::size_t kControlInterval = 32;                 // static_assert == ProfundumCore / MassLaw
    static constexpr float kMassHalfSpan = 50.0f;                       // FR-042 (Q2): |offset| 1 = half the range
    static constexpr float kUnitHalfSpan = 0.5f;                        //   Depth / Body / Edge
    static constexpr float kShiftHalfSpan = 1.0f;                       //   Shift
    static constexpr std::uint32_t kDefaultSeed = BrownianDrift::kDefaultDriftSeed;

    struct Offsets {                       // one interval's target offsets; exactly one is non-zero at most
        float mass = 0.0f;                 // Mass units
        float depth = 0.0f, body = 0.0f, edge = 0.0f;   // recipe units
        float shift = 0.0f;
    };

    class Output final : public ModulationSource {       // FR-048: the BassStereoField::MotionOutput pattern
    public:
        [[nodiscard]] float getCurrentValue() const noexcept override { return value_; }
        [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override { return {-1.0f, 1.0f}; }
    private:
        friend class ProfundumMotion;
        float value_ = 0.0f;
    };

    ProfundumMotion() noexcept = default;
    ProfundumMotion(const ProfundumMotion&) = delete;    // owns an LFO (lfo.h has no copy contract we rely on)
    ProfundumMotion& operator=(const ProfundumMotion&) = delete;

    void prepare(double sampleRate) noexcept;            // control-thread; clocks at fs / 32; then reset()
    void reset() noexcept;                               // env idle, lfo / drift reset (drift re-seeds), offsets 0
    void setSeed(std::uint32_t seed) noexcept;           // stored; applied at the next prepare / reset
    void setConfig(const Config& c) noexcept;            // per interval; each component setter on a bitwise change
    void setTempoBpm(float bpm) noexcept;                // non-finite / <= 0 keeps the previous; LFO::setTempo on change
    void noteRestart() noexcept;                         // an envelope-restarting note event: env.gate(true); lfo.retrigger()
    const Offsets& advanceInterval() noexcept;           // exactly once per grid point (FR-026 step 4)

    [[nodiscard]] const Offsets& offsets() const noexcept;
    [[nodiscard]] float sourceValue() const noexcept;    // v after polarity, before amount / depth (FR-086 tap)
    [[nodiscard]] float outputValue() const noexcept;    // m = v * amount * depth, in [-1, 1]
    [[nodiscard]] const ModulationSource& output() const noexcept;
};
}
```

### S3.2 State

`ADSREnvelope env_` (control rate), `LFO lfo_` (control rate), `BrownianDrift drift_`, `Config applied_` plus a
`bool configured_` flag (forces every setter on the first `setConfig` after prepare / reset), `Offsets offsets_`,
`float v_`, `float m_`, `Output output_`, `std::uint32_t seed_`, `float tempoBpm_ = 120`. All fixed size; no heap.

### S3.3 Contract

- `prepare(fs)`: `env_.prepare(float(fs / 32))`, `env_.setSustain(0)`, `env_.setRetriggerMode(Hard)`;
  `lfo_.prepare(fs / 32)`; `drift_.prepare(fs)`, `setDepth(1)`, `setMean(0)`; then `reset()`.
- `reset()`: `env_.reset()`, `lfo_.reset()`, `drift_.setSeed(seed_)`, `drift_.reset()`, offsets and values to +0,
  `configured_ = false`.
- `setConfig(c)`: for each field, compare bitwise with `applied_` (or `!configured_`) and call only the changed
  component setter (C-18 table). `source`, `target`, `amount`, `depth`, `polarity` are stored only. Undeclared enums and
  non-finite floats keep the previous value; floats clamp to the `MorphState` field-table ranges.
- `advanceInterval()` (S9.1): advance all three sources, select, map, scale, publish.
- `noteRestart()`: `env_.gate(true)`; `lfo_.retrigger()` (a no-op unless `cycleRetrigger`, lfo.h:272–275).

### S3.4 RT notes

No virtual call on the audio path (the `Output` virtuals are read by tests and by Phase 12, never by the engine). The
engine reads `offsets()` through the concrete type, which satisfies Vorago AR-2.

---

## S4. `ProfundumMacroMatrix` (FR-050–FR-056) — `systems/profundum_macro_matrix.h`

### S4.1 Types

```cpp
enum class ProfundumMacro : std::uint8_t { Weight = 0, DarkBright, TightLoose, CleanViolent, Count };
enum class ProfundumMacroTarget : std::uint8_t {
    RecipeDepth = 0, RecipeBody, AnchorLevel,                         // Weight
    RecipeEdge, RecipeShift, Shape, CharacterTone,                    // Dark <-> Bright
    AmpAttackOct, AmpDecayOct, AmpReleaseOct, TransientAttackDb, CompressionAmount,   // Tight <-> Loose
    ExcitationMix, ExcitationDrive, FoldAmount, CharacterAmount,      // Clean <-> Violent
    Count };
struct ProfundumMacroRow { ProfundumMacro macro; ProfundumMacroTarget target; float amount; ModCurve curve; };
struct ProfundumMacroValues { float weight = 0.5f; float darkBright = 0.5f; float tightLoose = 0.5f; float cleanViolent = 0.5f; };
```

### S4.2 Row table (FR-052; directions normative, amounts and curves harvested per Vorago AR-6)

`amount` is the contribution at the macro's end (u = ±1), in the target's units. The seed amounts below are the starting
point of the harvest (S10.5, T018); the harvest may change magnitudes and curves, never signs.

| Row | Macro | Target | Seed amount | Curve | Unit |
|---|---|---|---|---|---|
| 0 | Weight | RecipeDepth | +0.25 | Linear | recipe [0, 1] |
| 1 | Weight | RecipeBody | +0.25 | Linear | recipe [0, 1] |
| 2 | Weight | AnchorLevel | +0.25 | Linear | level [0, 1] |
| 3 | DarkBright | RecipeEdge | +0.5 | Linear | recipe [0, 1] |
| 4 | DarkBright | RecipeShift | +0.5 | Linear | recipe [−1, 1] |
| 5 | DarkBright | Shape | +0.5 | Linear | SHAPE [0, 1], toward BITE (1) |
| 6 | DarkBright | CharacterTone | +0.3 | Linear | Tone [0, 1] |
| 7 | TightLoose | AmpAttackOct | +2.0 | Linear | octaves of time (×4 at Loose, ÷4 at Tight) |
| 8 | TightLoose | AmpDecayOct | +2.0 | Linear | octaves |
| 9 | TightLoose | AmpReleaseOct | +2.0 | Linear | octaves |
| 10 | TightLoose | TransientAttackDb | −6.0 | Linear | dB, [−12, 12] |
| 11 | TightLoose | CompressionAmount | −0.5 | Linear | amount [0, 1], post-law (C-7) |
| 12 | CleanViolent | ExcitationMix | +0.5 | Linear | [0, 1] |
| 13 | CleanViolent | ExcitationDrive | +0.5 | Linear | [0, 1] |
| 14 | CleanViolent | FoldAmount | +50 | Linear | [0, 100] |
| 15 | CleanViolent | CharacterAmount | +0.5 | Linear | [0, 1] |

`static_assert`ed invariants (FR-050), each a constexpr loop over `kRows`: every `ProfundumMacroTarget` below `Count` is
claimed **exactly once**; no row uses `ModCurve::Stepped`; every row's macro is the documented owner of its target; the
signs match FR-052 (rows 10 and 11 negative, every other row positive).

### S4.3 Class

```cpp
class ProfundumMacroMatrix {
public:
    static constexpr float kNeutral = 0.5f;                           // FR-051 / D-7, all four
    static constexpr std::size_t kNumTargets = static_cast<std::size_t>(ProfundumMacroTarget::Count);
    static constexpr std::array<ProfundumMacroRow, 16> kRows{ /* S4.2 */ };

    void setMacro(ProfundumMacro m, float value) noexcept;            // non-finite keeps; clamp [0, 1]
    void setMacros(const ProfundumMacroValues& v) noexcept;           // field-wise, same rules
    [[nodiscard]] ProfundumMacroValues macros() const noexcept;       // FR-055
    [[nodiscard]] std::array<float, kNumTargets> contributions() const noexcept;   // pure (S9.3)
    void apply(MorphState& s) const noexcept;                         // FR-053: offsets + clamp, rows 0-10, 12-15
    [[nodiscard]] float compressionContribution() const noexcept;     // row 11 (C-7)
};
```

`apply` adds each **non-zero** contribution to its field and clamps the result to the field's range (S9.3). A
contribution of ±0 is skipped, so at the neutral the state is untouched bit for bit. The arithmetic itself already gives
exactly 0 at the neutral (`applyModCurve(c, 0) == 0`, modulation_curves.h:37–50); the skip only removes the −0 + x
question. With the `macroBypass` seam the engine does not call `apply` at all (SC-008(a)). The matrix stores four floats
and has no other state, so FR-054 idempotence holds by construction.

---

## S5. `ProfundumVoice` (FR-001–FR-016) — `systems/profundum_voice.h`

### S5.1 Public API (called by `ProfundumEngine` only; tests drive it through the engine)

```cpp
class ProfundumVoice {
public:
    static constexpr std::size_t kControlInterval = 32;      // static_assert == ProfundumCore / AnchorOscillator /
                                                             // MassLaw / BassStereoField / BassMorphEngine
    static constexpr std::size_t kMaxChunk = kControlInterval;
    static constexpr int kNumPartials = ProfundumCore::kDefaultPartials;     // 64
    static constexpr float kHalfWaveOnDb = -22.0f;           // C-5 (inside the -24 ... -16 band)
    static constexpr float kHalfWaveOffDb = -18.0f;
    static constexpr float kMeterFloor = 1.0e-6f;            // h1 below this: keep the flag

    struct IntervalInputs {                                  // handed by the engine at each grid point (S6.5 step 7)
        const MorphState* state;                             // after macros and Motion, before the Mass biases
        SpectralShapeRecipe::Controls coreControls;          // recipe with depth / body := law targets
        float foldAmount;                                    // law target
        float anchorLevel;                                   // FR-027 Mass-biased level
        const BassStereoField* field;                        // const: computePanVector / wideOffsets
        bool forceLinearCore;                                // seam (FR-086)
    };

    void prepare(double sampleRate, const MorphState& initial) noexcept;     // control thread
    void reset(const MorphState& initial) noexcept;     // control thread; allocation-free, bounded (the core's contract)
    void setNoteMemory(NoteMemory::Mode m, NoteMemory::Retrigger r) noexcept;

    // ---- note events, at a grid point (S5.5) ----
    void noteEvent(NoteMemory::Transition t, float fNote, bool stealOriginated) noexcept;
    void release() noexcept;                                 // amp-envelope gate off
    [[nodiscard]] bool audible() const noexcept;             // amp envelope not idle (Phase 4 spec line 849)
    [[nodiscard]] bool held() const noexcept;                // gate on
    [[nodiscard]] bool active() const noexcept;              // audible || pending transition || dip running

    // ---- per interval (S5.4) ----
    void beginInterval(const IntervalInputs& in, const float* fRes, std::size_t n) noexcept;  // FR-012, FR-013
    // ---- render (S5.6): adds into sum buffers; n <= kMaxChunk and never crosses a grid point ----
    void render(const float* fRes, float* sumL, float* sumR, std::size_t n) noexcept;

    [[nodiscard]] float currentCoreF0() const noexcept;      // f_core of the last rendered sample (detector key)
    [[nodiscard]] bool stateFinite() const noexcept;         // the components that declare one (SC-013)
    void setObserverSlot(ProfundumEngineObserver* o, std::uint8_t index, const ProfundumEngineSeams* s) noexcept;
};
```

`ProfundumEngineObserver`, `ProfundumEngineSeams` and the record types are complete here through
`profundum_engine_observer.h` (S2, S8).

`IntervalInputs` is assembled by the engine; the voice never sees the morph engine, the law or the macros directly.

### S5.2 State layout (fixed size, no heap)

| Member | Type | Note |
|---|---|---|
| core_, shape_, fold_, character_, anchor_ | the five components | FR-001 |
| drop_, lock_, memory_, declick_, ampEnv_ | `DropEnvelope`, `DropPhaseLock`, `NoteMemory`, `RetriggerDeclick`, `ADSREnvelope` | FR-001, FR-009 |
| fieldState_ | `BassStereoField::VoiceState` | FR-012, F-6 |
| capHz_ | `float` | the core's f0 ceiling, computed by the voice because the core's is private: `SpectralShapeRecipe::capFrequency(std::clamp(fs, ProfundumCore::kMinSampleRate, ProfundumCore::kMaxSampleRate))`, the core's own expression (profundum_core.h:131, 143). Used by the lock trigger and the meter clamp |
| pan_ | `std::array<float, kMaxPartials>` | handed to `setPartialPanOffsets` |
| scratch | `std::array<float, 32>` × 15: coreL/R, mainL/R (in-place Shape → Fold → Character), anchorL/R, d, dc, fCore, fAnchor, feed, gAmp, gD, followRatio (unused unless per-sample), anchorPhase | ≈ 1.9 KB |
| a₁ cache | `std::array<float, 64>` shape, `std::array<float, 64>` mask, `Controls` key, `float` f key, `float a1_` | C-6 |
| meter_ | `HarmonicBalanceMeter` (S9.6) | C-5 |
| handed | a "last handed" copy per setter argument (C-20) | `MorphState` slices + scalars |
| transition | `NoteMemory::Actions pending_`, `bool dipPending_`, `std::size_t switchAt_` (sample offset in the interval stream), flags `coreNoteOnPending_`, `anchorNoteOnPending_`, `anchorRearm_`, `lockTriggerPending_`, `lockKind_`, `coreSounding_`, `syncPhasePending_`, `stealOriginated_` | mirrors the Phase 4 harness |
| pitch | `float fNote_` (f_res source in POLY), `std::uint32_t coreGridCount_` (samples since the core's grid restart), `float lastFCore_`, `float feedFrom_, feedTo_` | |

The voice owns about 100 KB (the core's bank dominates). The engine owns `kMaxVoices` = 8 of them by value inside itself;
tests construct the engine with `std::make_unique` (the Phase 4 harness precedent).

### S5.3 prepare / reset

`prepare(fs, initial)` (control thread, not RT-safe because of the core's bank calibration):

1. Configure the components that snap at prepare from `initial` **before** their `prepare` (the Phase 8 fixture
   rule): `shape_.setShape(initial.shape)`, `character_.setAlgorithm / setAmount / setTone`,
   `fold_.setAmount(initial.foldAmount)`.
2. `capHz_ = SpectralShapeRecipe::capFrequency(std::clamp(fs, ProfundumCore::kMinSampleRate,
   ProfundumCore::kMaxSampleRate))`;
   `core_.prepare(fs, kNumPartials)`; `shape_.prepare(fs, 32)`; `fold_.prepare(fs, 32)` (its signature as shipped);
   `character_.prepare(fs, 32)`; `anchor_.prepare(fs)`; `drop_.prepare(fs)`; `lock_.prepare(fs)`;
   `declick_.prepare(fs)`; `ampEnv_.prepare(float(fs))`, `ampEnv_.setRetriggerMode(Hard)` (Phase 4 D-10).
3. Components whose setters are no-ops before prepare get `initial` now: `core_.setControls / setExcitation`,
   `anchor_.setControls`, the four Drop setters, the amp times and sustain, and **all three curve setters**
   (`setAttackCurve(initial.ampEnv.attackCurve)` etc.), which puts the envelope on its table path for every value, 0.7
   included (FR-009, Q6).
4. `reset(initial)`.

`reset(initial)` (**control thread; allocation-free and bounded**, the contract of the `core_.reset()` it is built on,
profundum_core.h:158–161; on the audio thread a sounding voice is cleared by a Reset-policy note event, never by
`reset`): reset every component (`core_.reset()`, `shape_.reset()`, `fold_.reset()`,
`character_.reset()` through their shipped reset; `anchor_`, `drop_`, `lock_`, `declick_`, `ampEnv_` resets), clear the
transition state, `coreSounding_ = false`, `fieldState_.reset()`, meter reset, a₁ cache invalidated, every "last handed"
value invalidated so the first interval hands everything.

### S5.4 `beginInterval` — the per-interval contract (FR-012, FR-013, in this order)

0. If a non-declicked transition is pending for sample 0 of this interval, apply it (S5.5); then run the **pitch stage**
   for the first chunk (up to the dip's switch sample, or the whole interval): `d[i] = drop_.process()`,
   `dc[i] = lock_.process(d[i])`, `fCore[i] = fRes[i] * std::exp2(dc[i] / 12.0f)`, `fAnchor[i] = (mode == Follow || lockReadsCore) ? fCore[i] : fRes[i]`
   (S9.4). A pending lock trigger is issued first:
   `lock_.trigger(drop_.plannedCurve(), fRes[0], gridPhase(), ProfundumCore::kMinF0Hz, capHz_, lockKind_)` with
   `gridPhase() = (16 − coreGridCount_ % 16) % 16` (the harness form).
1. **FR-012** (after the engine's `advanceControl`): if the core takes its Reset branch at sample 0,
   `fieldState_.reset()`. Then `field->computePanVector(fieldState_, fCore[0], pan_, kNumPartials)` →
   `core_.setPartialPanOffsets(pan_)` (the core does its own change test, profundum_core.h:204–223), and
   `field->wideOffsets(state.characterAlgorithm)` → `character_.setChannelOffsets(amount, tone)` on change.
2. **Core:** `core_.setControls(in.coreControls)`; excitation = `state.excitation`, with `mix = 0` if
   `forceLinearCore`; `setExcitation` on change.
3. **Fold / SHAPE:** `fold_.setAmount(in.foldAmount)`, `shape_.setShape(state.shape)`, on change;
   `fold_.setFundamentalHz(fCore[0])` on change (FR-006).
4. **Character:** `setAlgorithm(state.characterAlgorithm)` only when it differs from the last request (FR-013),
   then `setAmount`, `setTone`, on change.
5. **Rectify rule (C-5):** `character_.setInputHalfWaveSymmetric(meter_.halfWaveSymmetric())` on change.
6. **FOLLOW ratio (FR-007):** `r = character_.perceivedPitchRatio()` (read after steps 4–5, so it reflects this
   interval's targets) → `anchor_.setFollowRatio(r)` on change.
7. **Anchor (FR-013):** copy, then assign (a plain aggregate, anchor_oscillator.h:99–106):
   `AnchorOscillator::Controls c = state.anchor; c.level = in.anchorLevel; anchor_.setControls(c);` (the component
   skips an unchanged set itself); `DropPhaseLock::setBypass(mode == Follow && !followNoBypass)` on change.
8. **a₁ (FR-005, C-6):** shape cache keyed on `state.recipe` (the pre-Mass recipe after macro and Motion offsets, D-4),
   mask cache keyed on `fAnchor[0]`; `anchor_.setCoreH1Amplitude(a1_)` on change.
9. **Drop setters** (latched by the component): Amount, Time, Shape, Rebound on change.
10. **Amp envelope:** attack, decay, sustain, release on change (immediate receivers, FR-009); each curve setter only on
    a bitwise change of its curve.
11. **Feed ramp:** `feedFrom_ = feedTo_; feedTo_ = state.anchorFoldFeed` (FR-003, S9.5).

Every step emits one call-log record when an observer is attached (S8), with `issued` true only where a component setter
ran.

### S5.5 Note events and transitions (FR-010, FR-011, FR-016)

`noteEvent(t, fNote, stealOriginated)` (at a grid point, engine step 1):

1. `audible = ampEnv_.isActive()`; `a = memory_.resolve(t, audible)`.
2. If `a.declick` and the matching seam is off (`disableStealFade` for steal-originated events, `disableDeclick`
   otherwise, C-4): `declick_.arm()` (a no-op while a dip runs, so the switch sample stays), store
   `pending_ = a`, `dipPending_ = true`, `switchAt_ = now + declick_.samplesUntilSwitch()`; return.
3. If a dip is already pending, the new transition replaces the pending one (last call wins, Phase 4 FR-043); return.
4. Otherwise the transition applies at sample 0 of this interval.

**Applying a transition** (at sample 0, or at the switch sample inside `render`), the harness's `applyTransition`
verbatim (profundum_note_behaviour_test_helpers.h:541–592):

- `core_.setRetriggerPhase(static_cast<ProfundumCore::RetriggerPhase>(a.phase))`, the same for the Anchor, under
  `static_assert`s that `NoteMemory::PhasePolicy`, `ProfundumCore::RetriggerPhase` and `AnchorOscillator::RetriggerPhase`
  agree on `Reset = 0`, `FreeRunning = 1` (FR-010);
- `resetBranch = a.phase == Reset || !coreSounding_`. If true: `coreGridCount_ = 0`, `fieldState_.reset()` followed by a
  fresh `computePanVector` → `setPartialPanOffsets` at the switch sample (FR-012 step 1 runs first),
  `syncPhasePending_ = true`;
- if `a.dropTrigger`: `drop_.trigger(a.dropSnap)`, `lockTriggerPending_ = true`, `lockKind_ = !a.dropSnap ? Bridged :
  (resetBranch ? SnapReset : SnapFreeRunning)`;
- `coreNoteOnPending_ = anchorNoteOnPending_ = true`, `anchorRearm_ = a.anchorRearm`;
- if `a.envelopeRestart`: `ampEnv_.gate(true)` (Hard, from the current level on the table path) and the engine is told
  to call `motion.noteRestart()` (FR-045, D-5);
- `coreSounding_ = true`.

`release()`: `ampEnv_.gate(false)`.

### S5.6 `render(fRes, sumL, sumR, n)` (PerVoice placement)

Within one interval, the chunk `[0, n)` is split at the switch sample when a dip ends inside it. For each sub-chunk:

1. If this is the switch sample: apply the pending transition, rerun the pitch stage from here (S5.4 step 0), and
   re-run FR-012's pan step if the core takes its Reset branch.
2. Pending note-ons, at offset 0 of the sub-chunk: `core_.noteOn(fCore[0])`; `anchor_.noteOn(fAnchor[0], anchorRearm_)`;
   if `syncPhasePending_`, `character_.syncPhase(0.0f)` (FR-008: the core's h1 phase is 0 by construction here).
3. `core_.processBlock(coreL, coreR, m, fCore)` → **core tap**. `coreGridCount_ += m`.
4. Copy into mainL/R; `shape_.processBlock(mainL, mainR, m, fCore)`.
5. `anchor_.processBlock(anchorL, anchorR, m, fAnchor)` (one call per sample when the `anchorPhaseTap` seam is on, to
   read `phase()` per sample; SC-036 proves the two forms bit-identical) → **Anchor tap** (L == R).
6. Fold feed (FR-003, C-10): unless skipped, `mainX[i] += feed[i] * anchorL[i]` on both channels with
   `feed[i] = feedFrom_ + (feedTo_ − feedFrom_) · (k + 1) / 32` at interval position k (S9.5).
7. `fold_.processBlock(mainL, mainR, m)`; meter update on `0.5f * (mainL[i] + mainR[i])` at
   `std::clamp(fCore[i], ProfundumCore::kMinF0Hz, capHz_)` (S9.6).
8. `character_.processBlock(mainL, mainR, m, fCore)` → **Character tap** c.
9. Gains: `gAmp[i] = ampEnv_.process()`; `gD[i] = declick_.isActive() ? declick_.process() : 1.0f`.
10. Output (FR-015, C-10), per channel: `sum[i] += (c[i] * gAmp[i]) * gD[i] + (a[i] * gAmp[i]) * gD[i]`; with
    `anchorDetached`, only the first product is added.
11. `lastFCore_ = fCore[m − 1]`.

**Idle:** `active()` is false when the amp envelope is idle, no transition is pending and no dip runs. The engine skips
`beginInterval` and `render` for inactive voices (S6.6). Skipping freezes every component of the voice together, so the
core's h1 phase and the Character's sync clock stay aligned.

### S5.7 RT safety

Every method except `prepare` is `noexcept`, allocation-free, lock-free and I/O-free. The voice has no virtual call
except the observer callbacks, which run only when an observer is attached (a configuration-time pointer; one predictable
branch per tap point when null).

---

## S6. `ProfundumEngine` (FR-020–FR-032, FR-060–FR-066) — `systems/profundum_engine.h`

### S6.1 Configuration types

```cpp
enum class ProfundumNonlinearPlacement : std::uint8_t { PerVoice, PostSum };   // C-14
// ProfundumEngineSeams is declared in profundum_engine_observer.h (S8); shown here for reference.
struct ProfundumEngineSeams {                    // FR-086: configuration-time, all off by default
    bool macroBypass = false;
    bool anchorDetached = false;
    bool forceLinearCore = false;
    bool disableStealFade = false;
    bool disableDeclick = false;
    bool followNoBypass = false;
    bool lockReadsCore = false;
    bool anchorPhaseTap = false;                 // per-sample Anchor render for phase() (S5.6 step 5)
};
struct ProfundumEngineConfig {
    ProfundumNonlinearPlacement placement = ProfundumNonlinearPlacement::PerVoice;
    bool dynamicsOversampling = true;            // FR-082: the engine turns the MassDynamics opt-in on
    ProfundumEngineSeams seams{};
};
```

### S6.2 Public API

```cpp
class ProfundumEngine {
public:
    enum class VoiceMode : std::uint8_t { Mono = 0, Poly = 1 };
    static constexpr std::size_t kMaxVoices = 8;                 // FR-022, Q8
    static constexpr std::size_t kDefaultPolyphony = 4;
    static constexpr std::size_t kControlInterval = 32;
    static constexpr std::size_t kMaxQueuedEvents = 256;         // C-9
    static constexpr float kDcCutoffHz = 5.0f;                   // the Phase 6 chain value (C-10 there)
    static constexpr float kMinCeilingDb = -12.0f, kMaxCeilingDb = 0.0f;
    static constexpr float kDefaultCeilingDb = TruePeakLimiter::kDefaultCeilingDb;   // -1 dBTP
    static constexpr float kDefaultTempoBpm = BassStereoField::kDefaultTempoBpm;     // 120
    static constexpr float kDefaultBendRangeSt = 2.0f;           // NoteProcessor's own default
    static constexpr std::uint32_t kDefaultSeed = 0xB455u;
    static constexpr std::size_t kMotionDriftSalt = 1, kFieldDriftSalt = 2, kVoiceSaltBase = 16;
    static_assert(kMaxVoices <= VoiceAllocator::kMaxVoices);

    // lifecycle (control thread)
    void prepare(double sampleRate, const ProfundumEngineConfig& cfg = {}) noexcept;   // FR-020, FR-064
    void reset() noexcept;            // control thread; allocation-free, bounded (S6.4)
    void setObserver(ProfundumEngineObserver* o) noexcept;                            // configuration-time

    // events (audio thread; latched, applied at the next grid point, C-9)
    void noteOn(std::uint8_t note, std::uint8_t velocity) noexcept;
    void noteOff(std::uint8_t note) noexcept;
    void setPitchBend(float bipolar) noexcept;                   // [-1, 1]
    void setTempoBpm(float bpm) noexcept;

    // morph surface (FR-030)
    bool setEndpoint(BassMorphEngine::Endpoint e, const MorphState& s) noexcept;
    void setMorphPosition(float p) noexcept;
    void setMorphRampMs(float ms) noexcept;
    void setAnchorMorphMode(BassMorphEngine::AnchorMorphMode m) noexcept;
    [[nodiscard]] std::size_t serializeMorphRuntime(std::byte* dst, std::size_t cap) const noexcept;
    [[nodiscard]] bool restoreMorphRuntime(const std::byte* src, std::size_t size) noexcept;

    // non-morphed controls (FR-031)
    void setVoiceMode(VoiceMode m) noexcept;                     // FR-023
    void setPolyphony(std::size_t n) noexcept;                   // [1, kMaxVoices]
    void setMonoPriority(MonoMode m) noexcept;
    void setLegato(bool on) noexcept;
    void setPortamentoTimeMs(float ms) noexcept;
    void setPortamentoMode(PortaMode m) noexcept;
    void setGlideMode(GlideMode m) noexcept;
    void setNoteMemoryMode(NoteMemory::Mode m) noexcept;
    void setNoteRetrigger(NoteMemory::Retrigger r) noexcept;
    void setPitchBendRange(float semitones) noexcept;            // [0, 24]
    void setOutputCeilingDb(float db) noexcept;                  // [kMinCeilingDb, kMaxCeilingDb]
    void setMacro(ProfundumMacro m, float v) noexcept;
    void setMacros(const ProfundumMacroValues& v) noexcept;
    void setSeed(std::uint32_t seed) noexcept;                   // next prepare / reset (C-19)

    // FR-066 read-back: one getter per setter above (pending values), plus getEndpoint(e), morphPosition()

    void processBlock(float* left, float* right, std::size_t numSamples) noexcept;   // overwrites
    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }   // FR-025
    [[nodiscard]] std::uint64_t droppedEvents() const noexcept;  // C-9 diagnostic
};
```

Each setter sanitises: a non-finite float keeps the previous value; an undeclared enum is ignored; ranges clamp
(FR-061). Guards use `detail::isFinite` (never `std::isnan`). The values that reach a component without its own guard
(`TruePeakLimiter::setCeilingDb`, `NoteProcessor::setPitchBendRange`) are guarded here.

### S6.3 State

`std::array<ProfundumVoice, kMaxVoices> voices_`; `MonoHandler mono_`; `VoiceAllocator allocator_`;
`NoteProcessor bend_`; `BassMorphEngine morph_`; `ProfundumMacroMatrix macros_`; `ProfundumMotion motion_`;
`MassLaw law_`; `MassDynamics dynamics_`; `BassStereoField field_`; `DCBlocker dcL_, dcR_`; `TruePeakLimiter limiter_`;
`MorphState state_` (the working copy per interval); the fixed event queue and pending-release flags (C-9); pending and
applied copies of every FR-031 setter; per-interval scratch `std::array<float, 32>` × 4 (sumL/R and the MONO f_res /
bend-ratio trajectories); for PostSum, one extra `ShapeMorph`, `HarmonicFold`, `BassCharacterStage` and bus scratch;
`std::uint64_t pos_` (absolute sample), `std::size_t gridPhase_`; `bool dcSynced_`; seeds; `observer_`, `seams_`.

### S6.4 prepare / reset

`prepare(fs, cfg)` (control thread):

1. Store `cfg`; clamp fs to [22050, 192000].
2. `morph_.prepare(fs)` (it resets); `r0 = morph_.resolved()`; `state_ = r0.state`, with macros applied (S6.5 step 3).
3. Every voice: `voices_[v].prepare(fs, state_)`.
4. `mono_.prepare(fs)`; `bend_.prepare(fs)`; `allocator_.reset()`, `setAllocationMode(AllocationMode::Oldest)`,
   `setStealMode(StealMode::Hard)`, `(void)setVoiceCount(polyphony_)` (nothing to release at prepare, the Vorago
   precedent, vorago_engine.h:372–378).
5. `motion_.setSeed(deriveStreamSeed(seed_, kMotionDriftSalt))`, `motion_.prepare(fs)`;
   `field_.setDriftSeed(deriveStreamSeed(seed_, kFieldDriftSalt))`, `field_.setControls(state_.field)`,
   `field_.prepare(fs, 32)`.
6. `law_.setBase(base(state_))`, `law_.setMass(state_.mass)`, `law_.prepare(fs)` (it resets).
7. `dynamics_.setOversampling(cfg.dynamicsOversampling)`, its setters from the r0 targets, `dynamics_.prepare(fs, 32)`.
8. `dcL_.prepare(fs, kDcCutoffHz)`, `dcR_.prepare(fs, kDcCutoffHz)`; `limiter_.setCeilingDb(ceiling_)`,
   `limiter_.prepare(fs, 32)` (C-15).
9. `reset()`.

`reset()` (**control thread; allocation-free and bounded**: it resets 8 voices, each through a `core_.reset()` with the
core's control-thread contract, profundum_core.h:158–161, and `field_.reset()` re-runs `crossover_.prepare`,
bass_stereo_field.h:198–208; the Phase 10 processor calls it from `setActive` / `setupProcessing`, never from
`process`): reset every component (`morph_.reset()`, voices, `mono_.reset()`, `allocator_.reset()` and its
mode / steal / count re-applied, `bend_.reset()`, `motion_.reset()`, `field_.reset()`, `law_.reset()`,
`dynamics_.reset()`, `dcL_.reset()`, `dcR_.reset()`, `limiter_.reset()`), clear the event queue, `pos_ = 0`,
`gridPhase_ = 0`, `dcSynced_ = false`. The pending setter values and macros are kept (edge case "sample-rate change").

### S6.5 `processBlock` and the grid step (FR-026)

```
processBlock(L, R, n):
  i = 0
  while i < n:
    if gridPhase_ == 0: gridStep()
    m = min(n - i, 32 - gridPhase_)
    renderChunk(L + i, R + i, m)            // never crosses a grid point
    i += m; pos_ += m; gridPhase_ = (gridPhase_ + m) % 32
```

`gridStep()`, in FR-026's normative order:

1. **Events.** POLY housekeeping first: for every slot whose allocator state is Releasing and whose voice is not
   `active()`, `allocator_.voiceFinished(v)`. Then the latched setters are applied (mode switch FR-023 first, then
   polyphony, MONO handler settings, Note Memory settings forwarded to every voice, bend target, bend range, tempo to
   `field_.setTempoBpm` and `motion_.setTempoBpm`, ceiling), then the queued notes in arrival order (S6.7, S6.8).
2. `const Resolved& r = morph_.advanceInterval();` **exactly once** (Phase 8 handoff 9.1).
3. `state_ = r.state;` `if (!seams.macroBypass) macros_.apply(state_);` `cTL = macros_.compressionContribution()`.
4. `motion_.setConfig(state_.motion);` `const Offsets& o = motion_.advanceInterval();` then add each non-zero spectral
   offset to `state_.recipe` and clamp it (FR-043); `massIn = state_.mass` (+ `o.mass` if non-zero).
5. `law_.setBase({ .depth = state_.recipe.depth, .body = state_.recipe.body, .foldAmount = state_.foldAmount,
   .anchorLevel = state_.anchor.level })` (the Mass-driven fields at their `Targets{}` defaults, FR-027);
   `law_.setMass(massIn)`; `(void)law_.advanceInterval()`; `t = law_.currentTargets()`.
   `m_a = (morph_.anchorMorphMode() == BassMorphEngine::AnchorMorphMode::Lock) ? r.anchorMass
   : r.anchorMass + o.mass` (the add only when `o.mass` is non-zero) (FR-027, Q5). `Resolved` carries no mode
   (bass_morph_engine.h:55–60), so the mode is read from the getter (bass_morph_engine.h:175);
   `anchorLevel = MassLaw::evaluateRow(MassLaw::kRows[AnchorLevel], m_a, law_.base().anchorLevel)`.
6. `field_.setControls(state_.field)` (on change), `field_.advanceControl()`; then the SOLID re-sync check (C-2).
7. Compute the interval's pitch trajectories, always (so the bend smoother and the glide advance exactly once per
   sample even with no voice sounding): 32 `bend_.processPitchBend()` ratios, and in MONO 32 `processPortamento()` values
   (S6.7 / S6.8, C-11). Then, for each active voice v in index order,
   `voices_[v].beginInterval({&state_, coreControlsFor(state_.recipe, t), t.foldAmount, anchorLevel, …}, fRes, 32)`.
8. `MassDynamics`, each on change: `setTransientDurationMs(t.transientDurationMs)`,
   `setCompressionAmount(clamp(t.compressionAmount + cTL, 0, 1))` (C-7; the add skipped when cTL is ±0),
   `setOutputTrimDb(t.outputTrimDb)`, `setTransientAttackDb(state_.transientAttackDb)`,
   `setTransientSustainDb(state_.transientSustainDb)`, `setDetectorKeyHz(lowestSoundingF0)` (0 when no voice is audible,
   which the compressor maps to 80 Hz, mass_dynamics.h:194–196).

Steps 2–8 run in this order with no reordering. `coreControlsFor` is copy-then-assign:
`SpectralShapeRecipe::Controls c = state_.recipe; c.depth = t.depth; c.body = t.body; return c;` (the Phase 6 helper's
mapping, mass_dynamics_test_helpers.h:275–281; no designated-initialiser spread, no brace-init narrowing).
`state_.transientAttackDb` already includes the Tight ↔ Loose row (S4.2 row 10).

`renderChunk(L, R, m)`:

1. Zero `sumL/R[0..m)`.
2. Slice the interval trajectories computed once at the grid step (step 7): `ratio[k]` from 32 calls of
   `bend_.processPitchBend()` (C-11), MONO `fRes[k] = mono_.processPortamento() * ratio[k]` (32 calls), POLY
   `fRes_v[k] = fNote_v * ratio[k]` per voice. Each component is advanced exactly once per sample whatever the host
   partition, because events and the bend target only change at grid points (C-9).
3. Each active voice renders and adds into the sum (PerVoice) or into the buses (PostSum, C-14).
4. **Sum tap.** `dynamics_.processBlock(sumL, sumR, m)` → dynamics tap; `field_.processBlock(sumL, sumR, m)` → field tap;
   `dcL_.processBlock(sumL, m)`, `dcR_.processBlock(sumR, m)` → DC tap; `limiter_.processBlock(sumL, sumR, int(m))` →
   output tap; copy to `L`, `R`.

### S6.6 Voices that are not rendered

A voice that is not `active()` is skipped in steps 7 and renderChunk (C-20 lever L1). The MONO voice is voice 0.

### S6.7 MONO (FR-021)

`noteOn(note, vel)` applied at the grid point, mirroring the harness (profundum_note_behaviour_test_helpers.h:463–539):
`wasHeld = mono_.hasActiveNote()`, `audible = voices_[0].audible()`, `mev = mono_.noteOn(note, vel)`; if
`!mev.isNoteOn` nothing happens; else `t = !audible ? FromSilence : (wasHeld ? Overlap : Tail)` and
`voices_[0].noteEvent(t, /*fNote unused in MONO*/ 0, false)`. `noteOff(note)`: `mev = mono_.noteOff(note)`; when the
last held key is released (`!mev.isNoteOn`), `voices_[0].release()`; when the active note is released and another is
held, `t = ReturnToHeld`. A note-off of a non-active held key changes nothing. Glide is `MonoHandler`'s
(`processPortamento` per sample, S6.5 renderChunk step 2).

### S6.8 POLY (FR-022)

`events = allocator_.noteOn(note, vel)`; walk the span: `Steal` marks `stolen[v]`; `NoteOn` on v calls
`voices_[v].noteEvent(stolen[v] ? VoiceReuse : (voices_[v].audible() ? Tail : FromSilence),
midiNoteToFrequency(note), stolen[v])`; `NoteOff` calls `voices_[v].release()`. `noteOff(note)`: the NoteOff events
release their voices. `setPolyphony(n)` (at the grid point): `setVoiceCount(n)` and release every voice its NoteOff
events name; a voice above n keeps rendering until its release ends. No POLY glide (C-17).

### S6.9 MONO ↔ POLY switch (FR-023)

At the grid point where a pending mode change applies: every voice `release()`s (its amp envelope enters Release, so no
note hangs and none is cut); `mono_.reset()`; `allocator_.reset()` with mode, steal mode and count re-applied. Note-offs
that arrive later for notes held before the switch find nothing and change nothing. A releasing voice that receives a new
note is classified Tail (audible, not held), so a Reset policy declicks it.

### S6.10 SOLID re-synchronisation (FR-029, C-1, C-2)

At step 6 of every grid step, after `advanceControl()`: `if (field_.sideIdle()) { if (!dcSynced_) { dcR_ = dcL_;
dcSynced_ = true; log DcResync; } } else { dcSynced_ = false; }`.

### S6.11 RT safety (FR-060)

Everything except `prepare` and `setObserver` is `noexcept`, allocation-free and lock-free. `reset()` is
allocation-free and bounded but is a **control-thread** method (S6.4) and is not in the audio-thread list.
`VoiceAllocator` uses relaxed atomics internally (voice_allocator.h); they are lock-free on every supported platform
and are the shipped component's own choice. `static_assert(noexcept(...))` covers every audio-thread method (SC-022).

---

## S7. `MassDynamics` opt-in (FR-082, FR-084, C-3) — append-only edit of `systems/mass_dynamics.h`

> **Amended by T029 (2026-10-20).** The shipped mechanism oversamples the sidechain only (HP + held-peak detector at
> f·fs on the upsampled input); the gain computer, the shaper, the trim and the audio path stay at the base rate, so
> there is no halfband delay (the ~3.8-sample group delay below no longer exists) and the factor runs at every sample
> rate. See the spec's FR-082 and the Clarifications entry "T029 lever 2".

Appended to `MassDynamics` (public section, after the taps):

```cpp
    // ---- Profundum Phase 9 (FR-082, H-15, ruled Q4): opt-in 2x gain application + 8x detection ----
    static constexpr std::size_t kOversamplingFactor = 2;
    /// Configuration-time; takes effect at the next prepare(). Off by default (every existing caller unchanged).
    void setOversampling(bool enabled) noexcept { oversamplingRequested_ = enabled; }
    [[nodiscard]] bool oversamplingRequested() const noexcept { return oversamplingRequested_; }
    /// True when prepare() engaged the path: requested and 2 * fs <= BassCompressor::kMaxSampleRate (C-3).
    [[nodiscard]] bool oversamplingActive() const noexcept { return osActive_; }
    /// The halfband pair's state (polled by Phase 9's soak; stateFinite() itself is left untouched).
    [[nodiscard]] bool oversamplerStateFinite() const noexcept { return osL_.stateFinite() && osR_.stateFinite(); }
```

Private members appended: `PolyphaseHalfbandOversampler osL_, osR_;`, `std::array<float, 2 * kControlInterval> upL_{},
upR_{};`, `bool oversamplingRequested_ = false;`, `bool osActive_ = false;`. One include is appended:
`primitives/polyphase_halfband_oversampler.h` (Layer 1, downward). Behaviour changes inside two existing functions,
written so that the **off path executes exactly the existing statements** (as drafted; **T026, 2026-10-21:** the
shipped T029 shape also reads the sidechain rate in `BassCompressor::configureDetector` and moves `processBlock`'s
loop body into a helper whose detection step is T032's `processDetectionOnly`; the off path is bit-identical by the
Phase 6 suites, which SC-030 names as the gate under R-A, and `sc030-diff.js` records the hunks):

- `prepare`: the first statement becomes the computation of `osActive_` and `inner = osActive_ ? 2·fs : fs`; the shaper,
  the compressor and the trim smoother are prepared at `inner`; when active, `osL_.setFactor(2)`, `osR_.setFactor(2)`.
  With the opt-in off, `inner == fs`, so the calls are the existing calls with the same arguments.
- `reset`: appends `osL_.reset(); osR_.reset();` (no effect on the off path's output).
- `processBlock`: one branch at the top. Off → the existing loop, unmodified. On → the same run split; per run of `len`
  base samples, sanitise each input sample, `osL_.upsample(x, &upL_[2k])` (R likewise; only L in mono), latch the
  duration when the base `intervalPhase_ == 0`, `shaper_.processBlock(upL_, upR_, 2·len)`,
  `comp_.processBlock(upL_, upR_, 2·len)`, the trim per 2× sample, then `L[k] = osL_.downsample(&upL_[2k])`.

The base grid and the inner grid are aligned: a setter latched at a base grid point lands on an inner grid point (base
sample G ↔ inner sample 2G, a multiple of 32), so the latching semantics are unchanged. `latencySamples()` stays 0 (no
look-ahead; the halfband pair adds ≈ 3.8 base samples of group delay at low frequencies, recorded by T005 and in the
Phase 10 handoff). The diff is checked by `git diff` to contain only additions plus the two documented branch points; the
reviewer reads the off path line by line against `0ae33b87`.

Gate (SC-030, FR-084): the Phase 6 TUs (`[mass-dynamics]`) and the other consumers of `mass_dynamics.h` (the Phase 7 and
Phase 8 TUs that include it, S0.4), per-push and `[long]`, all green, plus the new `MassDynamics_Oversampling*` cases
(S10.3).

---

## S8. Test taps, seams and the observer (FR-086)

Everything in this section is declared in `systems/profundum_engine_observer.h` (S2), which `profundum_voice.h` and
`profundum_engine.h` both include, so the voice reads seam fields and calls the observer's virtuals on complete types
without an include cycle.

```cpp
enum class ProfundumCall : std::uint8_t {
    Events, MorphAdvance, Macros, Motion, LawSetBase, LawSetMass, LawAdvance, FieldSetControls, FieldAdvance, DcResync,
    VoicePan, VoiceWide, CoreControls, CoreExcitation, FoldAmount, FoldF0, Shape, CharAlgorithm, CharAmount, CharTone,
    HalfWave, FollowRatio, AnchorControls, LockBypass, A1, DropSetters, AmpEnv, AmpCurve, SyncPhase, NoteOnCore,
    NoteOnAnchor, DynDuration, DynCompression, DynTrim, DynAttack, DynSustain, DynKey };
// AmpEnv = the time / sustain step of S5.4 step 10; AmpCurve = its curve setters (issued only on a bitwise change).
struct ProfundumCallRecord { std::uint64_t sample; std::uint8_t voice; ProfundumCall call; bool issued; float value; };

struct ProfundumGridRecord {                     // per interval
    std::uint64_t sample;
    const BassMorphEngine::Resolved* resolved;
    const MorphState* afterMacros;               // a copy kept by the engine only while an observer is attached
    const MorphState* afterMotion;
    float motionSource, motionOutput, massIn, smoothedMass, anchorMassUsed, anchorLevel, compressionHanded;
    float compressorGrDb;                        // dynamics_.compressor().currentGainReductionDb() (mass_dynamics.h:571, 272)
    MassLaw::Targets targets;
};
struct ProfundumVoiceTaps {                      // per voice, per rendered chunk; spans into the voice's scratch
    std::uint8_t voice; std::uint64_t sample; std::size_t n;
    std::span<const float> coreL, coreR, charL, charR, anchorL, anchorR, gAmp, gD, fRes, fCore, fAnchor, anchorPhase,
                           feed;                 // the per-sample fold-feed g actually multiplied (S5.6 step 6; +0 when skipped)
    float followRatio, a1, anchorLevel, feedFrom, feedTo, meterTheta, meterFreqHz; bool halfWave;
};
struct ProfundumEngineTaps {                     // per chunk
    std::uint64_t sample; std::size_t n;
    std::span<const float> sumL, sumR, dynL, dynR, fieldL, fieldR, dcL, dcR, outL, outR;
};
class ProfundumEngineObserver {
public:
    virtual ~ProfundumEngineObserver() = default;
    virtual void onGrid(const ProfundumGridRecord&) noexcept {}
    virtual void onCall(const ProfundumCallRecord&) noexcept {}
    virtual void onVoiceChunk(const ProfundumVoiceTaps&) noexcept {}
    virtual void onEngineChunk(const ProfundumEngineTaps&) noexcept {}
};
```

- **Exposure:** `setObserver(ptr)` and `ProfundumEngineConfig::seams` are configuration-time. No compile-time macro is
  used (a header-only class compiled twice in one binary would be an ODR violation).
- **Cost when off:** one `if (observer_)` per tap point per chunk. The engine keeps the two extra `MorphState` copies of
  `ProfundumGridRecord` only while an observer is attached. The engine and the voice never allocate, copy or lock for a
  tap; the observer copies on the test side.
- **Transparency:** an observer with every seam off leaves the output bit-identical (SC-036). The `anchorPhaseTap` seam
  switches the Anchor to per-sample calls; SC-036 runs it as a third arm and requires bit identity too.
- **Twin rendering:** D_nl's denominator is the core tap of a second engine with `forceLinearCore`, same seed, settings
  and events (FR-086).

---

## S9. Algorithm notes (enough that nothing is guessed)

### S9.1 Motion per interval (FR-040–FR-044, Q2)

1. Advance every source: `e = env_.process()` (∈ [0, 1]); `l = lfo_.process()` (∈ [−1, 1]);
   `drift_.processBlock(32)`, `d = clamp(drift_.getCurrentValue(), −1, 1)` (a non-finite value reads 0, the field's rule,
   bass_stereo_field.h:315–316).
2. Select: Off → raw = 0 (and the polarity map is skipped, so v = +0); Env → e; Cycle → l; Drift → d.
3. Polarity (FR-042): Bipolar → Env: v = 2e − 1; Cycle / Drift: v = raw. Unipolar → Env: v = e; Cycle / Drift:
   v = (raw + 1) / 2. Evaluated in float, in that operation order. Consequence of Q2, recorded: Bipolar ENV rests at
   v = −1 between notes.
4. `m = v * amount * depth` (float, left to right). Output value = clamp(m, −1, 1).
5. Offsets: all five +0, then `offset[target] = m * span[target]`, span = {50, 0.5, 0.5, 0.5, 1} for {Mass, Depth, Body,
   Edge, Shift}.

Depth 0 (SC-026): m = v·amount·0 = ±0, so the offset is ±0, and the engine adds only non-zero offsets (S6.5 step 4), so
the render is bit-identical to Motion Off. The sources still advance under depth 0 and Off; they are not audible.

### S9.2 Applying offsets (FR-043)

Spectral: `x' = clamp(x + o, lo, hi)` for o ≠ 0, with the recipe ranges (depth / body / edge [0, 1], shift [−1, 1]).
Mass: `massIn = state.mass + o_mass` for o_mass ≠ 0; `MassLaw::setMass` clamps to [0, 100] (mass_law.h:163–167) and
the 40 ms slew follows (CYCLE above the slew's reach is attenuated and recorded, SC-009). The core then applies its own
per-interval step ceiling to the spectral controls (profundum_core.h:142–147).

### S9.3 Macro contribution and application (FR-050–FR-053)

`u = (m − 0.5)·2`, `c = amount · applyModCurve(curve, |u|) · (u < 0 ? −1 : 1)` (the Vorago form,
vorago_macro_matrix.h:1170–1173, applied to all four macros because all four are bipolar about 0.5, D-7). Per target the
contributions are summed (one row per target here). `apply`:

| Target | Field update (only when c ≠ 0) |
|---|---|
| RecipeDepth / Body / Edge | `clamp(x + c, 0, 1)` |
| RecipeShift | `clamp(x + c, −1, 1)` |
| AnchorLevel | `anchor.level = clamp(x + c, 0, 1)` (then the law's AnchorLevel base, FR-027) |
| Shape, CharacterTone, CharacterAmount, ExcitationMix, ExcitationDrive | `clamp(x + c, 0, 1)` |
| FoldAmount | `clamp(x + c, 0, 100)` (then the law's FoldAmount base) |
| AmpAttackOct / DecayOct / ReleaseOct | `t' = clamp(t · std::exp2(c), kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs)` (log domain, envelope_utils.h:50–51) |
| TransientAttackDb | `clamp(x + c, −12, 12)` |
| CompressionAmount | not in `MorphState`; handed post-law by the engine (C-7) |

### S9.4 Pitch (FR-006, FR-024, C-11)

Per sample: `ratio = semitonesToRatio(b · range)`; MONO `f_res = processPortamento() · ratio`; POLY
`f_res = midiNoteToFrequency(note) · ratio`; `d = drop.process()`; `d_c = lock.process(d)` (equal to d while bypassed);
`f_core = f_res · std::exp2(d_c / 12.0f)`; Anchor input `f_a = (mode == Follow || lockReadsCore) ? f_core : f_res`.
The bypass call: `lock.setBypass(mode == Follow && !followNoBypass)`.

### S9.5 Fold-feed ramp (FR-003, Phase 8 handoff 9.5)

Per interval `g_from = previous g_to`, `g_to = state.anchorFoldFeed`; at interval position k ∈ [0, 31]:
`g[k] = g_from + (g_to − g_from) · float(k + 1) / 32.0f` (it lands exactly on g_to at k = 31). If `g_from == +0 &&
g_to == +0` (bit test) the add is skipped for the whole interval (C-10).

### S9.6 Rectify input meter (C-5)

`HarmonicBalanceMeter`, per voice, in double:

- the meter frequency is **f = `std::clamp(f_core, ProfundumCore::kMinF0Hz, capHz_)`**, the f0 the core actually plays
  (the core's `sanitizeF0`, profundum_core.h:797–803; `capHz_` S5.2). f_res is unclamped (note 127 plus a +24 st bend
  is ≈ 50 kHz, above fs at 44.1 / 48 kHz), so the clamp is what bounds the meter; `capHz = 0.8 · fs/2`
  (spectral_shape_recipe.h:72, 93–95), so f/fs ≤ 0.4 always. As a defensive rule the meter **holds** (no accumulation,
  no reading, flag unchanged) for any sample whose f/fs ≥ 0.5;
- per sample: `θ += f / fs`; the rotor `z = e^{−j2πθ}` advances by the step `w = e^{−j2π f/fs}`, recomputed (one
  `std::sin` / `std::cos` pair) at each 16-sample pitch update and at each note-on; `Z1 += x·z`, `Z2 += x·z²`;
- when θ ≥ 1 (one period done): if `|Z1| > kMeterFloor · N` the reading is `r = 20·log10(|Z2| / |Z1|)`; the flag goes
  true if r ≤ −22 dB, false if r ≥ −18 dB, else stays; then `θ −= std::floor(θ)` (never a single `−= 1`, so θ stays in
  [0, 1) whatever the step), Z1 = Z2 = 0, z renormalised to unit modulus;
- reset: θ = 0, sums 0, flag false.

The f_core handed to `HarmonicFold::setFundamentalHz` and to the pan / detector keys is the same value the core
sanitises internally, so those receivers are not affected; only the meter integrates f per sample and needs the clamp.

Each period is a whole period to within one sample, so every harmonic other than the one measured falls on a near-null
of the rectangular window; the residual leakage is ≲ 1/P of h1 (−50 dB at E1, −33 dB at 1 kHz), below the −24 dB band
edge. Cost ≈ 12 flops per sample per voice.

### S9.7 a₁ (FR-005, C-6)

The real signatures are span-out (spectral_shape_recipe.h:100, 149, 190):

```cpp
static void evaluateShape(const Controls& c, std::span<float> out) noexcept;
static void evaluateMask(float f0Hz, double sampleRate, std::span<float> out) noexcept;
static void applyMask(std::span<const float> shape, std::span<const float> mask, std::span<float> out) noexcept;
```

The cache holds `std::array<float, 64> shape_, mask_, out_`. When the recipe key changes,
`evaluateShape(recipe, std::span<float>(shape_.data(), kNumPartials))`; when the f key changes,
`evaluateMask(f_a[0], fs, std::span<float>(mask_.data(), kNumPartials))`; when either changed,
`applyMask(shape_span, mask_span, out_span)`, all spans of exactly `kNumPartials` = 64, because `applyMask` renormalises
to P0 over `min(shape.size(), mask.size(), out.size(), kRecipeMaxPartials)` elements (:190–200) and the reference uses a
64-float array (mass_dynamics_test_helpers.h:289–290). `a₁ = out_[0] · kCenterPanGain`. It is bitwise equal to
`MassDynamicsTest::unbiasedH1Amplitude(recipe, f, fs)` (:288–292), which `VoiceEngine_A1MatchesUnbiasedH1` asserts
(S10.3).

### S9.8 FR-027 values

`t = MassLaw::evaluate(smoothedMass, base)`; Anchor level `= evaluateRow(kRows[AnchorLevel], m_a, base.anchorLevel)`,
with m_a from S6.5 step 5. `evaluateRow` clamps m_a to [0, 100] (mass_law.h:132–140).

### S9.9 Click bounds (SC-005(b), SC-006(b))

Phase 3 SC-006(c) form: every |y[n] − y[n−1]| in W ≤ 1.1 · (G_max·D_max + S_max·P_max), where D_max and P_max are the
steady-state max |Δw| and peak |w| of the voice's unit-gain output (Character + Anchor, g_amp = g_d = 1) for each note
involved, measured on separate steady renders of the same settings, and never read from the transition trace;
G_max ≤ 1 (the amp-envelope peak); S_max = the dip's analytic peak slope `π / (2·L_d)` (L_d = 72 at 48 kHz →
0.02182) for declicked restarts; for a Drop, `+ |A|·max|ḋ|·ln2/12 · P_max` with the Drop's analytic peak slope (Phase 4
SC-010(b)); for a glide, Phase 4 SC-010(c)'s one-semitone frequency-step S_max.

### S9.10 Coherent-sum prediction (SC-017(c))

The gate is the spec's closed form (SC-017(c), Phase 3 SC-009(i)), not a tap-sum identity. On a steady whole-period
window, complex h1 of the Character tap C and the Anchor tap A (Goertzel at f0, the same window for both) in the
stressed render S and in the reference R, plus the Sum tap's complex h1 Σ in the same windows.

- **Prediction (G11-3, 2026-10-20):** ρ_S = |C_S| / |A_S|, θ_S = arg C_S − arg A_S, and ρ_R, θ_R the same in R:
  **ΔE_pred = 20·log10(|1 + ρ_S·e^{jθ_S}| / |1 + ρ_R·e^{jθ_R}|)**. The drafted form 20·log10(|1 + ρ·e^{jθ}| / 2) assumed
  R coherent; `ShapeMorph` rotates h1 by −9.9° on every chain (Phase 5 FR-040), which moves the prediction by 0.008 dB.
- **Measurement:** ΔE_meas = 20·log10|Σ_S| − 20·log10|Σ_R| (Sum tap, one voice, steady window where g_amp is constant
  and g_d = 1).
- **Gate (c):** |ΔE_meas − ΔE_pred| ≤ 0.1 dB.
- **Premise assertions** (what the closed form assumes; each is a REQUIRE in the same cell, because a mis-locked,
  mis-scaled or out-of-phase Anchor in R is exactly the defect (c) exists to catch):
  - ~~R is coherent: |20·log10(|C_R| / |A_R|)| ≤ 0.05 dB and |arg C_R − arg A_R| ≤ 2° (wrapped)~~ — **recorded, not
    asserted (G11-3):** the two-chain form carries ρ_R / θ_R, so no coherence of R is assumed;
  - the Anchor is the same in S and R: |20·log10(|A_S| / |A_R|)| ≤ 0.01 dB (it renders before the join with the same
    pitch and a₁, so it is expected to be bitwise equal; (a)'s 1 dB gate stays as the spec writes it).
- **Sanity line, recorded only:** |Σ − (A + C)| / |Σ| per window (the C-10 tap-sum identity, a restatement of
  SC-019(b)), printed beside the gate. It never substitutes for the prediction.

ρ, θ, ρ_R, θ_R, ΔE_pred, ΔE_meas per cell go to `evidence/anchor-chain.csv`.

### S9.11 Frame-wise readers (SC-009, AT-33 … AT-40)

Frame = 8 whole periods of f0 (rectangular, so each harmonic sits on a bin), hop 960 samples. E_sub = Goertzel power
at f0. D_nl per frame = 10·log10(P_{≥9}(char) / P_{≥9}(twin core)) with **P_{≥9} = P_total − P_DC − Σ_{n=1..8} P_n**
(Parseval over whole periods: 9 Goertzels per frame instead of one per harmonic). L_K per frame = K-weighted RMS of the
frame (`TestUtils::kWeightedLevelDb`). The boxcar's analytic attenuation at rate f_m, |sinc(π·f_m·T_frame)|, is printed
beside each peak-to-peak.

---

## S10. Test plan (file · TEST_CASE · assertion strategy)

### S10.1 Files, tags, targets

All in `dsp/tests/unit/systems/`, in `dsp_systems_tests`, tagged `[systems][profundum][voice-engine]` plus the SC tag
(`[sc-0NN]`). Render-heavy grids add `[long]`; CPU cases add `[performance][.perf]`; probes and harvests are hidden
(`[.probe]`, `[.harvest]`).

| TU | Content |
|---|---|
| `profundum_voice_engine_test_helpers.h` | the driver, the observer that records taps, the twin runner, readers (S10.2) |
| `profundum_voice_engine_helpers_test.cpp` | helper self-tests (reader on known signals, driver partitioning, adapter mapping) |
| `profundum_motion_test.cpp` | FR-040–FR-049 unit cases; SC-026 at the unit level |
| `profundum_macro_matrix_test.cpp` | FR-050–FR-056 unit cases; SC-008 at the unit level |
| `profundum_voice_test.cpp` | SC-019, SC-020, SC-033, SC-034, FR-010 / FR-015 identities |
| `profundum_engine_test.cpp` | SC-008, SC-011, SC-012, SC-022, SC-024, SC-027, SC-028, SC-029, SC-031, SC-032, SC-036 |
| `profundum_engine_nonfinite_test.cpp` | SC-023, in the `-fno-fast-math` block |
| `profundum_engine_click_test.cpp` | SC-005, SC-006, SC-025 (`[long]`, sharded per image mode / matrix slice) |
| `profundum_engine_audibility_test.cpp` | SC-010 (sharded by row group), SC-007, SC-009 (`[long]`) |
| `profundum_engine_chain_test.cpp` | SC-015 / SC-016 (`[long]`), SC-017, SC-018 |
| `profundum_engine_aliasing_test.cpp` | SC-021 (`[long]`, one shard per rate × algorithm) |
| `profundum_engine_soak_test.cpp` | SC-013 / SC-014 (`[long]`; two TEST_CASEs: the MONO half and the POLY half, 15 min each) |
| `profundum_engine_probe_test.cpp` | P-1 … P-4, the H-13 Character records, the macro harvest, the fingerprint harvest (hidden) |
| `profundum_engine_perf_test.cpp` | SC-001, SC-002, SC-003, SC-004 (`[performance][.perf]`) |
| `mass_dynamics_oversampling_test.cpp` | the FR-082 opt-in unit cases; tagged `[mass-dynamics][voice-engine]` |

### S10.2 Shared conventions

- **Driver:** `VoiceEngineRender render(const VoiceEngineScript&)`: a script is a sample-stamped event list plus settings
  (seed, config, endpoints, macros, polyphony, mode). The driver splits host blocks at event stamps (C-9) and renders
  with a configurable host block (default 64; partition tests sweep it). The engine is built with `std::make_unique`.
- **Taps:** an observer that appends every callback's spans into preallocated `std::vector`s (allocated before any
  `AllocationScope`); per-voice taps are stored per voice. `VoiceEngineTaps` derives from
  `BassMorphTest::MorphChainTaps`, so the Phase 8 readers apply unchanged; `asFieldTaps(taps, Tap)` copies the chosen tap
  (Sum, Dynamics, Field, Output) into `outL / outR` for `dStDb` / `dBrOct` / `lKDb`.
- **D_nl:** always the Character tap over the twin's core tap (`forceLinearCore` engine, same script), through
  `BassMorphTest::dNlDb`.
- **Notes:** E0 / E1 / E2 / C3 = MIDI 16 / 28 / 40 / 48; A1 = 33, G1 = 31, B1 = 35, E3 = 52, A2 = 45.
- **FTZ / DAZ:** every TU enables it (`enable_ftz_daz.h`); the engine relies on it (spec edge case "Denormals").
- **No bit-exact float goldens:** in-process comparisons only; the single cross-build pin is a `render_fingerprint.h`
  fingerprint harvested inside `dsp_systems_tests` (SC-011).

### S10.3 Unit cases (fast lane)

| TEST_CASE | Assertion |
|---|---|
| `ProfundumMotion_Sources` | each source selected alone moves `sourceValue()` within its range; Off reads +0; the three sources advance whichever is selected (the LFO phase after N intervals equals a standalone LFO's) |
| `ProfundumMotion_PolarityAndScale` | for v ∈ {−1, −0.5, 0, 0.5, 1} and each source, polarity and target: offset == v_pol · amount · depth · span bitwise (S9.1); exactly one target non-zero |
| `ProfundumMotion_DepthZeroIsZero` | depth 0 → every offset is ±0 for every source × target (SC-026 unit arm) |
| `ProfundumMotion_ConfigChangeOnly` | a repeated identical `setConfig` changes nothing bitwise over 1000 intervals vs a single call |
| `ProfundumMotion_ModulationSource` | `output()` is a `ModulationSource`; range {−1, 1}; value == clamp(m) |
| `ProfundumMacro_TableInvariants` | the constexpr checks of S4.2 also run at test time (every target once, no Stepped, signs) |
| `ProfundumMacro_NeutralIsIdentity` | for 1000 seeded random valid `MorphState`s, `apply` at all-0.5 leaves the state bitwise unchanged (SC-008(a) unit arm) |
| `ProfundumMacro_Directions` | each macro 0 → 1 moves each of its fields in its FR-052 direction (non-strict where a clamp binds) and no other field |
| `ProfundumMacro_ValuesRoundTrip` | `setMacros(v); macros() == v`; non-finite keeps, clamps (FR-055, FR-061) |
| `MassDynamics_OversamplingOffUnchanged` | opt-in off: a render bit-identical to a second instance never told about the opt-in (and the full Phase 6 suite is the real gate, SC-030) |
| `MassDynamics_OversamplingActiveRange` | active at 22.05 / 44.1 / 48 / 96 kHz, inactive at 192 kHz (C-3) |
| `MassDynamics_OversamplingNoAllocation` | `AllocationScope` over prepare (second call), reset and 10 s of processing: 0 |
| `MassDynamics_OversamplingPartition` | host blocks {1, 7, 32, 64, 511} bit-identical |
| `MassDynamics_OversamplingTransfer` | steady sines at 40 / 200 / 1000 / 5000 Hz with amount 0, trim 0: gain within ±0.05 dB of the off path; group delay at 40 Hz recorded |
| `VoiceEngine_A1MatchesUnbiasedH1` (engine TU) | FR-005: MONO and 4 voices, a morph ramp plus CYCLE → Depth so the recipe and f move; per voice and interval the `a1` tap is bitwise equal to `MassDynamicsTest::unbiasedH1Amplitude(afterMotion->recipe, fAnchor[0], fs)`, in LOCK and in FOLLOW (f = f_core) |
| `VoiceEngine_DropLatchedAtTrigger` (voice TU) | FR-006: a held note with a 24 st Drop; mid-Drop, the morph moves Drop Amount / Time / Shape / Rebound and a Motion offset is active: the `fCore` tap of that note is bitwise equal to a run without the change; the next note's Drop follows the new values (its trajectory differs from the old one) |
| `VoiceEngine_MonoPriorityAndPortaMode` (engine TU) | FR-021: overlapping keys under `setMonoPriority` Last / LowNote / HighNote select the sounding note per `MonoHandler`'s rule (f_res tap vs a standalone `MonoHandler` fed the same events, bitwise); `PortaMode::LegatoOnly` glides on f_res only for overlapped notes, `Always` for both |
| `VoiceEngine_HostTempo` (engine TU) | FR-028: CYCLE `cycleSync` on and a synced field Motion: the CYCLE period (grid-record `motionSource` zero crossings) and the synced field Motion's period (the period of the side-channel level of the Field tap, WIDE, one held E1) track 90 / 120 / 150 BPM within one interval; with no `setTempoBpm` call, both equal the 120 BPM period |
| `VoiceEngine_MorphRuntimeRoundTrip` (engine TU) | FR-030: mid-ramp, `serializeMorphRuntime` equals a standalone `BassMorphEngine` driven by the same calls byte for byte; restoring into a second engine with the same endpoints gives a bitwise-equal output thereafter; a corrupt blob returns false and changes nothing |
| `VoiceEngine_PolyEnvRetrigger` (engine TU) | FR-045: POLY, Motion ENV: a second note on another voice while the first is held restarts the source (grid-record `motionSource` returns to the attack start at that grid point); a non-restarting event (Continue cell) does not |
| `VoiceEngine_GetterReadBack` (engine TU) | FR-066: every FR-031 / FR-030 setter with an in-range value, then its getter returns that value bitwise (enums equal); clamped values read back clamped |
| `VoiceEngine_MeterBoundedAtTopNote` (voice TU) | note 127, bend range 24, bend +1, 2 s at 44.1 and 48 kHz: per chunk `meterTheta` ∈ [0, 1), `meterFreqHz` == the core's clamp (`capHz`), output finite, no `HalfWave` flip driven by a single-sample period |

### S10.4 Probes (hidden; run before the gated renders, results to `evidence/probes/`)

| Probe | What it settles | Stop rule |
|---|---|---|
| **P-1** `VoiceEngine_CpuProbe` (`[.probe][.perf]`, run alone, pinned) | SC-001 / SC-002 setting with the real composition; per-stage breakdown (each stage timed by an observer-free build of the same loop with stages switched off one at a time by probe-only seams in the test, never in the header) | R-A |
| **P-2** `VoiceEngine_DeclickReferenceProbe` | SC-005(a) / SC-006(a) ratios on the worst cells (A_s, E1 and C3, Reset, held victim, SOLID, block 1) | R-B |
| **P-3** `MassDynamics_OversamplingAliasProbe` | Phase 6's Fold-100 E2 cells at 44.1 / 48 kHz through `MassDynamics` alone, opt-in off / on, with Phase 6's residual method; plus the engine-output estimator of SC-021 on the same input | R-C |
| **P-4** `VoiceEngine_FollowFlipProbe` | the Amounts at which `perceivedPitchRatio()` flips for Rectify (half-wave-symmetric input) and Fracture at E1 / C3, so SC-018 / AT-13 use a setting that flips | if neither flips, FOLLOW is flagged inert with this evidence (roadmap 688–689) |
| **H-13** `VoiceEngine_CharacterRecords` (`[.probe]`) | FR-070 H-13 "Record": in the composed voice at E0 / E1 / E2 / C3, per algorithm at Amount 1: the h1 transfer (complex Goertzel h1 of the Character tap over the Character input, i.e. Fold 0 so the input is the Shape output; gain dB and phase); the group delay at f0 from that phase across f0 ± 1 % notes; the growl level swing (L_K of the Character tap with Amount 0 across SHAPE 0 … 1, the Phase 5 C-6 measurand). Written to `evidence/h13-character-records.txt` beside Phase 5's figures. The crossfade CPU cell is recorded by SC-003 (S10.6) | none (Record) |

### S10.5 Harvests (hidden)

- `VoiceEngine_MacroHarvest`: per macro, the amounts (and curve) are raised from the S4.2 seeds until the AT-47 … AT-50
  row passes at E1 and C3 and the SC-007 sweep is monotone; signs are fixed. The harvested table is pasted between
  `// HARVEST-BEGIN macro-rows` / `// HARVEST-END macro-rows` and written to `evidence/macro-rows.txt`.
- `VoiceEngine_FingerprintHarvest`: the SC-011 schedule's fingerprint, pasted into the test between harvest markers
  (harvested inside `dsp_systems_tests`, never from a standalone dump).

### S10.6 Per criterion

| SC | TEST_CASE (TU) | Assertion strategy |
|---|---|---|
| SC-001 | `VoiceEngine_CpuMono` (perf) | The spec setting (excitation 1 / 1, SHAPE 0.5, Fold 100, Character **Tube** and **Fracture** at Amount 1 (C-16), Anchor FOLLOW, feed 1, Drop 24 st every 250 ms, CYCLE → Mass depth 1, WIDE with field Motion, Mass 100, morph ramping 0 ↔ 1 on the showcase pair, block 64), 10 s timed after 1 s warm-up, median of 3, % = wall / audio time. Gate: max(Tube, Fracture) ≤ 8 %. Per-stage breakdown printed |
| SC-002 | `VoiceEngine_CpuPoly4` (perf) | Same, four held notes E1 / A1 / E2 / C3; gate ≤ 25 %. 8-voice cell (E1 / C3 / A1 / E2 / G1 / B1 / E3 / A2) printed as RECORD |
| SC-003 | `VoiceEngine_WorstInterval` (perf; the spec's name) | 4 voices, block 64, ramp 5 ms (the minimum), position ramping 0 ↔ 1 repeatedly. The endpoint pair differs in **all five** selection groups (bass_morph_engine.h:505–506): **Character** (algorithm Tube ↔ Fracture), **ImageMode** (SOLID ↔ WIDE), **FieldMotion** (field Motion source), **Motion** (Motion source ENV ↔ CYCLE), **Anchor** (waveform Sine ↔ Triangle); every group's governing amount is non-zero on both sides (Character amount, field spread, field motionDepth, Motion amount, Anchor level), so every group switches by rule 3 at the same q. Anchor morph mode **Blend** (c(p) = p, bass_morph_engine.h:184–196), so p_a's target is p and the Anchor group switches at the same grid point as the four main groups. The pair also differs in **all three amp-envelope curves** (A −1, B +1 on attack / decay / release), so every interval of the ramp rebuilds the 256-entry tables on all 4 voices (R-I). Each `processBlock` timed with `steady_clock`; **gate** max(block wall / 1.333 ms) ≤ 1.0; worst 32-sample interval recorded against 81.6 %. **Coincidence and coverage asserts**, from a second, untimed run of the same script with an observer attached (bit-identical by SC-036): at every switch, `morph.selection(g)` of all five groups changes in the same `MorphAdvance` interval and `Resolved::anchorPosition == position` bitwise there; and the grid points inside the worst timed block carry `AmpCurve` records with `issued == true` for all 4 voices. **H-13 record:** the per-block cost of the blocks inside the Character crossfade window, as % of a core per voice, printed against Phase 5's 2.65 % crossfade cell |
| SC-004 | `VoiceEngine_PlacementCost` (perf) | PerVoice vs PostSum (C-14) at 4 voices under SC-002's protocol, and FOLLOW per voice vs off; written to `evidence/placement.txt`; the FR-014 ruling is written from it |
| SC-005 | `VoiceEngine_StealClickless_{Solid,Spread,Wide}` (click, `[long]`) | 5 notes into 4 voices; victim {held, releasing} × the four Note Memory modes × Retrigger {Reset, Continue} × {E1, C3} × blocks {1, 37, 512}; bases A_s and B. W per spec. (a) max\|Δy\| over W on the Sum tap ≤ 10^(0.5/20) × the polyphony-5 reference's; (b) S9.9 bound; (c) ClickDetector differential count recorded. Positive controls: `disableStealFade` steal and `disableDeclick` restart each fail (a) or (b). CSV `evidence/steal-clicks.csv` |
| SC-006 | `VoiceEngine_MonoReplaceClickless_*` (click, `[long]`) | Phase 4's legato × Retrigger × Note Memory × GlideMode matrix on the full chain, intervals {1, 12, 24} st both ways; reference = POLY-2; (a) gated except where portamento > 0 (recorded there), (b) gated everywhere with the glide S_max where it applies. **FR-009 record:** an extra arm per matrix slice where the morph (5 ms ramp, at the replacement) moves attack, decay, sustain and release; column `ampStep` = max \|g_amp[n] − g_amp[n−1]\| (gain tap) over the 64 samples after each grid point where a time / sustain receiver changed, beside the same reading of the no-morph run. CSV `evidence/mono-replace-clicks.csv` (row per cell: cell id, (a) ratio, (b) margin, ampStep, ampStep_ref); recorded, not gated |
| SC-007 | `VoiceEngine_MacroMonotone` (audibility, `[long]`) | 21 steps, others neutral, E1 and C3, base A_s (B_s for Dark ↔ Bright, build ruling B10-1): Weight → E_sub (Sum); Dark ↔ Bright → D_br (Sum); Tight ↔ Loose → D_t (Sum, gate render); Clean ↔ Violent → D_nl. Non-decreasing within ε (0.25 dB / 0.02 oct / 2 %) |
| SC-008 | `VoiceEngine_MacroNeutral` (engine) | (a) all four at 0.5 vs `macroBypass`: output bitwise equal over the SC-011 schedule; (b) non-neutral macros set every interval vs once: bitwise equal |
| SC-009 | `VoiceEngine_MotionMassSwell` (audibility, `[long]`) | CYCLE → Mass, amount 1, depth 1, Mass 50, E1, rates {0.1, 0.5, 2} Hz; 1 s settle + max(3 cycles, 3 s); S9.11 frames; gate: D_nl peak-to-peak at 0.1 Hz ≥ 3 dB; E_sub, L_K, the boxcar response and the ENV / DRIFT readings recorded |
| SC-010 | `VoiceEngine_AudibilityTable_{Core,Character,Anchor,Pitch,Envelope,Mass,Field,Motion,Morph,NoteMemory,Macros}` (audibility, `[long]`) | every AT row at E1 and C3 on its base (C-8), contrast, descriptor and tap; bar per family; a row that is inert at both endpoints gets a per-row setting written into the spec table and the CSV with the reason; CSV columns row, contrast, base, note, descriptor, tap, value, bar, pass, the output-tap reading, and **ampStep**; `REQUIRE(pass)` per cell. **FR-009 record (Envelope shard):** for each envelope row, a morph-driven arm (5 ms ramp moving the row's time / sustain field during a held note) writes `ampStep` = max \|Δg_amp\| (gain tap) over the 64 samples after each grid point where an immediate receiver changed; rows without such an arm write `n/a`; recorded, not gated. **H-17 record (Mass shard):** 4 voices detuned (E1 ± 7 cents, ± 13 cents) at Mass 100 on base B; the steady sub-band GR (`compressorGrDb` grid-record field, median over 1 s after 500 ms) written to `evidence/h17-poly-gr.txt` beside Phase 6's −10.1 dB |
| SC-011 | `VoiceEngine_DeterminismHarness` (engine) | a seeded 6-event schedule (the Vorago `kDeterminismSchedule` shape) in MONO and POLY: two instances and two runs bitwise equal; seed A vs B with DRIFT on: residual > −60 dB, with DRIFT off: bitwise equal; the fingerprint compared at `kSampleTolerance`; the gated comparisons are same-path only (H-29). **H-29 record:** a second TEST_CASE in the same TU, `VoiceEngine_FoldPathRecord`, renders one held E1 note reaching Fold 100 by two paths (held at 100 from the note-on vs a morph ramp 0 → 100) and writes the steady level and h1 differences in dB (Character tap) to `evidence/h29-fold-path.txt` beside Phase 8's ≈ 0.13 dB; recorded, not gated |
| SC-012 | `VoiceEngine_Partition` (engine) | blocks {1, 7, 32, 64, 511, 4096}, same stamped script: bitwise equal |
| SC-013 | `VoiceEngine_Soak30Min_{Mono,Poly}` (soak, `[long]`) | 15 min each; every sample finite (bit test); true peak (test-side 4× halfband) ≤ ceiling + 0.1 dB; DC per 10 s ≤ −80 dBFS at the DC-safety tap (G11-4; the output's windows recorded), at the output last-5-min mean ≤ first-5-min mean + 1 dB; per interval the `stateFinite()` components plus `oversamplerStateFinite()` and the FR-086 taps of the rest checked with `detail::isFinite`; H-2 recorded |
| SC-014 | inside SC-013 | four lock segments per half (Depth / Body by CYCLE 0.5 Hz, Edge / Shift by DRIFT), each ≥ 122 s of a held LOCK Sine note, MONO E1 / POLY E1, A1, E2, C3; per voice, `goertzelPhase` of the Anchor tap vs the h1 of that voice's core tap; windows at s₀ + 1 s and s₀ + 61 s; \|Δθ(t₀ + 60) − Δθ(t₀)\| ≤ 0.5° |
| SC-015 | `VoiceEngine_MonoSum` (chain, `[long]`) | SOLID / SPREAD / WIDE × MONO / 4 voices × A_s / B × E1 / C3: Q_sub ≤ 1.0 dB, ρ ≥ 0.99 (`bandReading`); SOLID: output L == R bitwise from the `DcResync` grid point (C-2); H-20 residue recorded |
| SC-016 | inside SC-015 | the Anchor tap L == R bitwise in every chunk |
| SC-017 | `VoiceEngine_AnchorThroughChain` (chain) | E0, E1, E2 × six algorithms at Amount 1 × Fold 100 × excitation 1, Anchor level 1, LOCK; R = Fold 0, Amount 0, excitation 0. Gated waveforms Sine, Triangle, RoundedSine, AsymmetricSine: (a) Anchor-tap w₁ within 1 dB S vs R; (b) \|E_sub(S) − E_sub(R)\| ≤ 4.1 dB for Tube, Fold and Bite; recorded for Rectify, Crush and Fracture under the floor E_sub(S) ≥ E_sub(A_S) − 2 dB (G11-3); (c) the Sum tap's h1 change S vs R within 0.1 dB of the two-chain form 20·log₁₀(\|1 + ρ_S·e^{jθ_S}\| / \|1 + ρ_R·e^{jθ_R}\|) (S9.10, G11-3), with the Anchor-equality premise asserted and ρ_R / θ_R recorded; the tap-sum identity printed as a sanity line only. OctaveDownSine recorded. ρ, θ, ρ_R, θ_R to `evidence/anchor-chain.csv` |
| SC-018 | `VoiceEngine_FollowTracking` (chain) | Rectify (half-wave-symmetric input, from P-4) and Fracture at E1 / C3, sweep and step cases with ≥ 6 s holds; event = the interval where the follow-ratio tap changes; Anchor frequency per sample from `phase()` differences (`anchorPhaseTap` seam); reference `anchorShsF0` on frames inside the holds; settling exists and the error after it ≤ ±5 cents; latency recorded; H-12 recorded |
| SC-019 | `VoiceEngine_AnchorJoin` (voice) | (a) `anchorDetached` vs attached at g = 0: core and Character taps bitwise equal; (b) per sample the voice's contribution (one voice sounding, Sum tap) bitwise equals `(c·g_amp)·g_d + (a·g_amp)·g_d` from the taps, including a Reset retrigger and a steal (g_d ≠ 1 intervals tagged), **run at g ∈ {0, 0.5, 1} and across an interval where g steps 0 → 1** (so a dropped direct path at g > 0 fails, FR-003 "always present"); (c) at g = 1 the Anchor tap is bitwise equal to the g = 0 render's; (d) **feed ramp:** across a g step from feedFrom to feedTo, the `feed` tap equals `feedFrom + (feedTo − feedFrom) · float(k + 1) / 32.0f` bitwise at every interval position k, landing on feedTo at k = 31, and the interval with both endpoints +0 records `feed` = +0 (skipped, C-10) |
| SC-020 | `VoiceEngine_DropRouting` (voice) | 24 st Drop, MONO with a 200 ms glide and a ±2 st bend during the Drop, and POLY at 4 voices; the test-side reference (MonoHandler + NoteProcessor + DropEnvelope + DropPhaseLock, C-11 expressions); **bitwise** equality of the Anchor and core pitch taps per mode; positive controls `followNoBypass`, `lockReadsCore` fail |
| SC-021 | `VoiceEngine_Aliasing_{44k1,48k,96k}_{algo}` (aliasing, `[long]`) | the spec setting and estimator (`aliasedPower`, `aliasFftLength`), worst channel; (a) Sum ≤ −80 dBFS; (b) output ≤ −80 dBFS with the opt-in on, the off arm recorded with the dynamics-added term; (c) a 48 st Drop at the steepest tail on the **core tap** ≤ −96 dBFS (the Phase 4 bar and method), the Sum reading recorded |
| SC-022 | `VoiceEngine_RealTimeSafety` (engine) | `static_assert(noexcept(...))` for every audio-thread method (`reset()` is a control-thread method, S6.4, and is not in that list); 10 s of notes, steals, mode switches, morph / macro / Motion automation inside `AllocationScope`: 0; a `reset()` call inside the same scope: 0 (the allocation-free half of its contract); `droppedEvents() == 0` |
| SC-023 | `VoiceEngine_NonFinite` (nonfinite) | NaN / ±Inf / a denormal through every setter, bend and tempo: output finite (bit test), getters return the previous values |
| SC-024 | `VoiceEngine_SampleRates` (engine) | 22.05 / 44.1 / 48 / 96 / 192 kHz: finite, RMS ≥ −40 dBFS over 1 s after 100 ms (held E1, A_s); a re-prepare at a new rate inside `AllocationScope`: 0 (C-15) |
| SC-025 | `VoiceEngine_MotionSelectorsClickFree` (click, `[long]`) | every ordered pair of source, target, cycleShape, polarity, cycleSync, cycleRetrigger and the representative cycleNote × cycleModifier subset, through the morph, amount 1, depth 1; max\|Δy\| ≤ 10^(0.5/20) × the direct-setter reference; differential click count ≤ the reference's; both recorded |
| SC-026 | `VoiceEngine_MotionDepthZero` (engine) | every source × target at depth 0 vs Motion Off: output bitwise equal |
| SC-027 | `VoiceEngine_MassSplit` (engine) | (a) per interval the handed main-voice and dynamics values equal `MassLaw::evaluate(smoothedMass(), base())` bitwise (compression per C-7), macros neutral; the Tight ↔ Loose 0.2 arm checks C-7; (b)–(d) as specified, from the handed Anchor-level tap |
| SC-028 | `VoiceEngine_MorphGlobal` (engine) | 4 voices: every voice's handed state per interval comes from the one `Resolved`; `MorphAdvance` records == ⌈n / 32⌉, all on grid multiples |
| SC-029 | `VoiceEngine_ModeSwitch` (engine) | MONO ↔ POLY with held notes: every voice idle after its release; no stuck voice; clicks by the SC-005 gate against the no-switch reference with notes released at the switch point |
| SC-030 | comply | `git diff --name-only 0ae33b87` lists `dsp/include/krate/dsp/systems/mass_dynamics.h` as the only modified shared header; a `node` check confirms the diff of that file has no removed lines outside the two documented branch points (S7); the consumer TUs' results are quoted |
| SC-031 | `VoiceEngine_StealOrder` (engine) | 4 voices, a seeded 1000-event schedule; at each steal the victim equals a test-side model (oldest Releasing by note-on time, else oldest Active) |
| SC-032 | `VoiceEngine_CallOrder` (engine) | one interval's call-log steps equal the S6.5 / S5.4 sequence exactly, MONO and 4 voices |
| SC-033 | `VoiceEngine_RectifySymmetry` (voice) | states whose Character-input h2/h1 (offline Goertzel on the Character input, i.e. the mid of the Fold output) reads −30, **−25**, **−15** and −10 dB (each within ±0.5 dB, found by a bisection on Fold amount / excitation in the helper): the flag is true at −30 and −25, false at −15 and −10, from either previous flag value, so a threshold anywhere in (−30, −24) or (−16, −10) fails. **Hysteresis:** a state at −20 dB (inside −22 … −18) reached from the −25 state keeps true, and reached from the −15 state keeps false, over ≥ 20 periods each. `SyncPhase` issued records exactly at Reset-branch effective samples and the first note after prepare / reset; at each, the core tap's `goertzelPhase` over 4 periods equals a fresh prepared core's within 1°, under both policies |
| SC-034 | `VoiceEngine_PitchBend` (voice) | full bend at ranges 2 and 12: f_res moves by the range within 0.1 cent, MONO and POLY |
| SC-035 | comply | `evidence/listening-sheet.md`: one entry per Listen row (H-1, H-2, H-8, H-10, H-14, H-19, H-28 ×2) with render path and the user's verdict; renders by the hidden `VoiceEngine_ListeningRenders` (`[.listen]`) |
| SC-036 | `VoiceEngine_TapTransparency` (engine) | the SC-011 schedule, MONO and POLY: observer attached with seams off, and with `anchorPhaseTap` on, each bitwise equal to the no-observer render; with no observer, `AllocationScope` 0 |

---

## S11. Build integration

### S11.1 CMake

- `dsp/CMakeLists.txt`: append under `# Profundum Phase 9 (specs/profundum-phase9-voice-engine)` the five headers
  (`profundum_motion.h`, `profundum_macro_matrix.h`, `profundum_engine_observer.h`, `profundum_voice.h`,
  `profundum_engine.h`) to `KRATE_DSP_SYSTEMS_HEADERS` (after line 218).
- `dsp/tests/CMakeLists.txt`: append to the `dsp_systems_tests` source list, after the Phase 8 block (line 786), an
  **enumerated** Phase 9 block (comment lines name each TU's SCs, as Phases 7 and 8 do) listing **every `.cpp` of
  S10.1, all 14 by name**: `profundum_voice_engine_helpers_test.cpp`, `profundum_motion_test.cpp`,
  `profundum_macro_matrix_test.cpp`, `profundum_voice_test.cpp`, `profundum_engine_test.cpp`,
  `profundum_engine_nonfinite_test.cpp`, `profundum_engine_click_test.cpp`, `profundum_engine_audibility_test.cpp`,
  `profundum_engine_chain_test.cpp`, `profundum_engine_aliasing_test.cpp`, `profundum_engine_soak_test.cpp`,
  `profundum_engine_probe_test.cpp`, `profundum_engine_perf_test.cpp`, `mass_dynamics_oversampling_test.cpp`; and
  add `unit/systems/profundum_engine_nonfinite_test.cpp` to the `-fno-fast-math -fno-finite-math-only` block after
  line 1285 with a "Profundum Phase 9" comment. The perf TU stays out of that block (shipping FP mode).
- No new target, no plugin CMake.

### S11.2 Commands and lanes (Windows, full CMake path)

```
# ONE rebuild (all Phase 9 TUs and the MassDynamics append are in dsp_systems_tests):
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_systems_tests
# Fast gate, captured to a log once:
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[voice-engine]~[long]~[.perf]" > log 2>&1
# SC-030 consumer gate (mass_dynamics.h modified): the [profundum] per-push cases, then their [long] shards
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[profundum]~[long]~[.perf]"
# [long] lane: one shard per Phase 9 [long] TEST_CASE plus the [long] cases of the mass_dynamics.h consumer TUs
#   (Phases 6-8), listed with --list-tests, run concurrently at (cores - 2), each to its own log.
# Probes before gated renders: P-3 (after T005), P-1 and P-2 (after T009), P-4 (before SC-018).
# CPU lane ALONE, last, after idling:  node tools/run-cpu-tests.js dsp_systems_tests  (filtered to [voice-engine]),
#   plus the Phase 6 SC-017 case (its TU consumes the modified header; recorded as load if it flips under contention).
# Gates: node tools/check-portability.js; node tools/lint-apple-globals.js; node tools/lint-layers.js;
#   node tools/lint-odr.js; node tools/lint-nonfinite-symbols.js; node tools/lint-float-bit-goldens.js;
#   wsl --shutdown afterwards; clang-tidy on the new TUs and headers by file list with a per-file timeout, after
#   reconfiguring build/windows-ninja (the test CMake changed).
```

The lanes follow the change-set rule: the phase's own TUs plus the full `[profundum]` suites of the consumers of the one
modified shared header. No plugin suite is reachable from this change set (no plugin includes `mass_dynamics.h`, S0.4).

---

## S12. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-A | CPU ceilings (S1) | P-1 first; levers L1 → L2 → (ruled) L3; stop rule |
| R-B | SC-005(a) / SC-006(a) on clean low notes (S1) | P-2 first; stop rule |
| R-C | −80 dBFS at the output (S1) | P-3 before the engine; stop rule |
| R-D | H-16 vs SC-030 (S1) | no L3 without a ruling |
| R-E | Denormals in the halfband pair, the meter's double sums and the ADSR tails | FTZ / DAZ is a precondition (as for every Profundum component); the meter resets its sums each period; `stateFinite` / `oversamplerStateFinite` polled in the soak |
| R-F | Non-finite input reaching the limiter (it has no NaN guard, true_peak_limiter.h:138–141) | the field stage writes 0 for a non-finite sample (bass_stereo_field.h:450–480) and MassDynamics sanitises first; SC-023 asserts a finite output |
| R-G | Portability: designated initialisers only; no narrowing in brace init; `std::exp2` / `std::sin` in float vs double exactly as written in S9; no `std::isnan`; the nested `Output` class uses `final`; `VoiceAllocator` atomics as shipped | `check-portability.js`, `lint-apple-globals.js` (a `VoiceMode` nested enum, no global `Rect` / `MAC`), WSL probe for any Linux doubt |
| R-H | Ambiguity between `Krate::DSP::VoiceState` and `BassStereoField::VoiceState` (F-6) | always qualified; `lint-odr.js` |
| R-I | The amp-envelope curve setters rebuild a 256-entry table when a morph moves a curve (adsr_envelope.h:160–176; up to 3 × 256 power evaluations per voice per ramp interval) | change-only (C-20); SC-003's pair differs in all three curves (A −1, B +1), so every ramp interval rebuilds the tables on all 4 voices inside the timed worst-block gate, and SC-003 asserts from the call log that `AmpCurve` setters were issued in the worst block (S10.6) |
| R-J | The 2·fs dynamics shift Phase 6's calibrated tables slightly (finer true-peak) | `MassDynamics_OversamplingTransfer` bounds the steady transfer; the GR / makeup differences at Mass 0 / 50 / 100 are recorded to `evidence/os-dynamics-delta.txt` (not a Phase 6 re-gate, spec Non-Goals) |
| R-K | Soak wall clock (2 × 15 min of audio at up to 25 % of a core plus per-interval polling) | two shards run concurrently; estimated ≤ 6 min wall each; per-interval polling reads only the observer's grid record |
| R-L | `VoiceAllocator` assigning a releasing slot after a mode switch | classified Tail (S6.9); SC-029 covers it |

---

## S13. CPU sizing (sizing only; SC-001 / SC-002 measure)

| Stage | % of one core @ 48 kHz | Source |
|---|---|---|
| Core + excitation + Fold, stereo (pan offsets unlink it) | 2.462 | Phase 2 stereo-forced chain |
| Shape + Character, unlinked worst (Tube, SHAPE 0.25) | 3.876 | Phase 5 max_unlinked |
| Anchor | 0.141 | Phase 3 (spec F-7) |
| Drop + lock + Note Memory + declick + glide | ≤ 0.050 | Phase 4 bar (0.0286 recorded) |
| Field law + pan forwarding | 0.198 | Phase 7 |
| Amp ADSR, a₁ (cached), meter, feed, sum | ≈ 0.10 | estimate |
| **Per voice** | **≈ 6.83** | |
| MassDynamics at 2·fs (2 × 0.991 + halfband pair) | ≈ 2.1 | Phase 6 × 2 + estimate |
| DC ×2 + limiter | 1.235 | Phase 6 record |
| Field stage | 0.10 | Phase 7 |
| Morph, MassLaw, Motion, macros | ≈ 0.06 | Phase 8 + estimate |
| **Globals** | **≈ 3.5** | |
| **MONO** | **≈ 10.3** (bar 8) | R-A |
| **4 voices PerVoice** | **≈ 30.8** (bar 25) | R-A |
| 4 voices PostSum (4 × 2.95 + 3.88 + 3.5) | ≈ 19.2 | C-14 |

---

## S14. Task outline (for tasks.md)

| Task | Content | Verify |
|---|---|---|
| T001 | Test CMake skeleton (enumerated block, nonfinite entry), header-list entries, empty TUs compile | build green |
| T002 | `ProfundumMacroMatrix` + `profundum_macro_matrix_test.cpp` (tests first) | unit cases green |
| T003 | `ProfundumMotion` + `profundum_motion_test.cpp` (tests first) | unit cases green |
| T003b | `profundum_engine_observer.h` (S8 types; includes per S2) | compiles standalone in a header-only TU; lint-layers clean |
| T004 | `MassDynamics` opt-in tests first (`mass_dynamics_oversampling_test.cpp`), then the append (S7) | unit cases green; Phase 6–8 `[profundum]` per-push green |
| T005 | **P-3** probe; record group delay | R-C stop rule |
| T006 | Test helpers (driver, observer, adapters) + self-tests | helpers test green |
| T007 | `ProfundumVoice` (S5) with `profundum_voice_test.cpp` fast cases (SC-019 incl. (b) at g ∈ {0, 0.5, 1} and the (d) feed ramp, SC-020, SC-033 incl. −25 / −15 and hysteresis, SC-034, `VoiceEngine_DropLatchedAtTrigger`, `VoiceEngine_MeterBoundedAtTopNote`) written first | green |
| T008 | `ProfundumEngine` (S6, S8) with `profundum_engine_test.cpp` fast cases written first, including `VoiceEngine_A1MatchesUnbiasedH1`, `VoiceEngine_MonoPriorityAndPortaMode`, `VoiceEngine_HostTempo`, `VoiceEngine_MorphRuntimeRoundTrip`, `VoiceEngine_PolyEnvRetrigger`, `VoiceEngine_GetterReadBack` | green |
| T009 | **P-1** (alone, pinned) and **P-2** | R-A, R-B stop rules |
| T010 | Non-finite TU (SC-023) | green |
| T011 | Macro harvest; fingerprint harvest; paste between markers; `VoiceEngine_FoldPathRecord` (H-29) | evidence files incl. `h29-fold-path.txt` |
| T012 | **P-4**; SC-018; SC-017 (closed-form gate + premises, S9.10); H-13 `VoiceEngine_CharacterRecords` | green; `h13-character-records.txt` |
| T013 | Click TUs (SC-005, SC-006 incl. the FR-009 `ampStep` arm, SC-025) | `[long]` shards green; `mono-replace-clicks.csv` |
| T014 | Audibility TUs (SC-010 incl. the Envelope `ampStep` column and the Mass-shard H-17 record, SC-007, SC-009) | `[long]` shards green; CSV; `h17-poly-gr.txt` |
| T015 | Mono-sum (SC-015 / SC-016), aliasing (SC-021) | `[long]` shards green |
| T016 | Soak (SC-013 / SC-014) | `[long]` shards green |
| T017 | Perf TU (SC-001 … SC-004; SC-003 with the five-group / curve pair, its coincidence asserts and the H-13 crossfade cell); the FR-014 ruling written from SC-004 | CPU lane alone |
| T018 | Listening renders; `evidence/listening-sheet.md` (user verdicts at comply) | sheet complete |
| T019 | Lints, portability, clang-tidy by file list; SC-030 diff check; `evidence/phase-handoffs.txt` | all clean |

Probes T005 and T009 gate everything after them: no gated render is trusted before their stop rules are cleared.

---

## S15. Handoffs out (FR-085; `evidence/phase-handoffs.txt`, copied into the roadmap at comply)

- **Roadmap:** Phase 9 line 666 amended to name `ProfundumMotion` as the Motion bus (FR-048, Q1).
- **Phase 10:** `prepare(fs, ProfundumEngineConfig)` / `reset()` / `processBlock(L, R, n)` (overwrites), latency 0
  (the 2·fs dynamics add ≈ 3.8 samples of low-frequency group delay, not latency); events are applied at the next
  32-sample grid point, so the processor splits host blocks at event offsets (C-9); the opt-in is inactive at 192 kHz
  (C-3); FTZ / DAZ is the wrapper's job.
- **Phase 11:** every engine setter with range, default and unit (S6.2); `ProfundumMacroValues`; the polyphony range
  **1–8, default 4**; the state budget: the 422-byte morph blob (Phase 8 handoff 11.1) plus the FR-031 values
  (≈ 40 bytes); velocity unused (C-12); the compression macro row acts after the law (C-7).
- **Phase 12:** the harvested macro row table (`evidence/macro-rows.txt`), the audibility CSV, the listening verdicts,
  Bipolar ENV resting at −1 (S9.1).
- **Phase 13:** ρ / θ per SC-017 cell for audition.

---

## Review notes

- Every ruling the spec left to the plan is made in S1 (C-1 … C-21) or deferred to a probe with a stop rule (R-A … R-D).
- Four clauses amend spec text and need ratification: C-1 (no limiter copy), C-3's 192 kHz no-op, C-7 (the SC-027(a) reading of the
  compression receiver), C-21 (SC-021(c) on the core tap).
- The CPU sizing (R-A) predicts a breach; the plan does not hide it and does not relax a ceiling.
- **Revision 2026-10-20 (plan review).** Every issue was applied; none was rejected and no threshold moved.
  - SC-017(c) is gated by the spec's closed form 20·log₁₀(|1 + ρe^{jθ}|/2) with ρ, θ from the S taps, and the R-state
    coherence and Anchor equality it assumes are asserted (S9.10). The earlier tap-sum form was a restatement of
    SC-019(b) and could not fail; it is now a recorded sanity line. No spec text changes, so nothing new needs
    ratification.
  - SC-003 names all five selection groups, runs the Anchor group in Blend so p_a switches on the main groups' grid
    point (asserted from the log), and adds the three amp curves to the pair (asserted issued in the worst block); R-I
    now points at a gate that measures the cost. The TEST_CASE is renamed `VoiceEngine_WorstInterval` to match the
    spec's SC-003 row.
  - FR-070 H-13, H-17 and H-29 each have a recording element, an evidence file and a task (S10.4, S10.6, S14).
  - FR-009's step under a morph is recorded as `ampStep` in SC-006's and SC-010's CSVs.
  - SC-033 tests −25 / −15 dB and hysteresis; SC-019 runs (b) at g ∈ {0, 0.5, 1} and gates the feed ramp (d).
  - FR-005 / 006 / 021 / 028 / 030 / 045 / 066 each have a named fast-lane TEST_CASE (S10.3).
  - The observer and seam types moved to `profundum_engine_observer.h` (S2, S8); the old split could not compile.
  - Voice and engine `reset()` are relabelled control-thread, allocation-free and bounded, matching
    `ProfundumCore::reset`'s contract. SC-022 keeps the allocation check and drops `reset()` from its audio-thread list.
  - The meter runs at the clamped f_core with `θ −= floor(θ)` and a hold at f/fs ≥ 0.5; the new top-note case gates it.
  - Code forms corrected against the shipped headers: span-out recipe calls at length 64, copy-then-assign
    `Controls`, `anchorMorphMode()` for m_a, the voice's own `capHz_`, and direct includes (pitch_utils, midi_utils,
    db_utils). S11.1 names all 14 TUs.
