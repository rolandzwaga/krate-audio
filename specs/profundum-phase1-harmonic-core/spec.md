# Feature Specification: Profundum Phase 1 — Harmonic Core

**Spec slug:** `profundum-phase1-harmonic-core`
**Status:** Draft (specify stage, 2026-10-09). Challenged once (fidelity / testability / reality review,
2026-10-09) and revised; clarified 2026-10-09 (see Clarifications); not yet planned.
**Roadmap source:** `specs/Profundum-roadmap.md` → Part A → **Phase 1** (lines 194–242). Supporting
roadmap statements this spec traces to: **Core Philosophy** (lines 38–54, especially "Harmonics are
regenerated, not filtered", line 40, "Alias-free low end is engineered", line 51, and "Nothing ships
inert", line 53); **Key Design Decision 1** (lines 118–124); **Key Design Decision 2** (lines 125–131 —
"per-partial pan/phase offsets", line 127, which Phase 7 consumes); the **Reuse Inventory "Harmonic
generator" row** (line 161); the **ODR note** (lines 174–176); the **dependency note** "Phase 1 first (it
unblocks 2, 3, 7 and 8)" (lines 187–188) and the dependency graph (lines 562–576); the **Cross-Cutting
Constraints** (lines 578–598); and roadmap **Open Question 3** (line 608), the only open question the
roadmap defers to this phase.
**Concept source:** `specs/Profundum-concept.md` §1 Core (lines 44–76), §3 Spectral Transfer
(lines 144–206), §12 Low Note Guard (lines 591–597).
Every roadmap and source line cited below was read this session (2026-10-09).

**Layer:** two new components, both header-only in `Krate::DSP`:

| Component | Layer | Header |
|---|---|---|
| `SpectralShapeRecipe` | 2 (processors) | `dsp/include/krate/dsp/processors/spectral_shape_recipe.h` |
| `ProfundumCore` | 3 (systems) | `dsp/include/krate/dsp/systems/profundum_core.h` |

One shared header receives one **append-only** addition: `HarmonicOscillatorBank` gains a method that
restores its exact centre pan tables (FR-048, FR-064). Nothing existing in it changes.

---

## Overview

Phase 1 builds Profundum's sound source: a **linear, additive harmonic oscillator** whose per-harmonic
gain vector is computed analytically, every control block, from four musical controls — **Depth**
(fundamental dominance), **Body** (low-order harmonic envelope), **Edge** (high-order harmonic envelope)
and **Shift** (the concept's *Spectral Transfer*: moves the Body and Edge envelope centres along the
logarithmic harmonic-number axis). The math lives in a stateless Layer 2 component,
`SpectralShapeRecipe`, which outputs a loudness-normalised gain vector aₙ = Aₙ(D, B, E, S) for
n = 1…N, including a fixed, documented **Low Note Guard** (harmonic-density taper as f₀ falls) and a hard
cap below Nyquist. A Layer 3 component, `ProfundumCore`, drives the existing zero-latency
`HarmonicOscillatorBank` with that vector under a per-control-interval gain-step ceiling, with
deterministic start phases, a retrigger phase policy input that Phase 4 will drive, and an
identity-by-default per-partial pan-offset hook that Phase 7 will drive. Because the spectrum is
regenerated rather than filtered, character tracks the keyboard: harmonic 4 is always harmonic 4. This
phase is linear only — new harmonics and even harmonics from nonlinearity are Phase 2.

## Scope

In scope (all from roadmap lines 201–231 and line 161):

1. `SpectralShapeRecipe` — pure math D/B/E/S → aₙ, with the Depth-driven base slope law, Body curvature and (odd-side, whole-vector) emphasis,
   overlapping Gaussian-like envelopes in log-n space, loudness normalisation, the sine / triangle / saw
   anchor points, the Heavy / Hollow / Growl named distributions, the five published Body colours, the
   Low Note Guard and the Nyquist cap.
2. `ProfundumCore` — recipe → `HarmonicOscillatorBank`; partial count a prepare-time constant (default
   64); per-block recipe evaluation with a shape-vector step ceiling, a never-slewed guard/cap mask and per-sample amplitude smoothing
   in the bank; deterministic harmonic phases; a retrigger phase policy input; a per-partial pan-offset
   hook (identity here) with an exact return to centre.
3. One append-only `HarmonicOscillatorBank` method (exact centre-pan restore, FR-064).
4. Unit tests, render-descriptor audibility gates and the CPU budget test for both components.
5. The partial-count decision (roadmap OQ-3), taken after the CPU measurement this phase produces.

## Non-Goals (owned by later phases, or deliberately excluded)

| Excluded | Owner | Roadmap line |
|---|---|---|
| Edge excitation (oversampled soft clip inside the core), asymmetry / **even harmonics beyond a saw's natural parity**, the *asymmetric → clipped → increasingly complex* tail of the waveform path | Phase 2 | 218–222, 254–258 |
| Harmonic Fold | Phase 2 | 259–265 |
| Sub Anchor, including its phase lock to the core h1. Phase 1 guarantees only that h1's start phase is deterministic (FR-045); it exposes **no** h1 phase readout. The lock mechanism (shared `PhaseAccumulator` or note-on lock, line 284) and any bank accessor it needs are Phase 3's. | Phase 3 | 281–296 |
| Note handling, glide, Note Memory, *choosing* the retrigger phase policy, click-free retrigger | Phase 4 | 317–322 |
| Shape (ROUND / HOLLOW / BITE), Character stage | Phase 5 | 339–349 |
| Mass law (incl. its Depth/Body bias targets) | Phase 6 | 370–374 |
| The SPREAD width law, GRAVITY, MOTION — i.e. any *non-identity* pan-offset vector; per-partial **inter-channel phase offsets / decorrelation** (the "phase" half of line 127), pending the D-10 ruling | Phase 7 | 127, 393–402 |
| Bass Morph, `MorphState` (Phase 1 only keeps the recipe a pure function of its controls so Phase 8 can interpolate controls, roadmap line 431) | Phase 8 | 422–441 |
| Voice, engine, polyphony, amp envelope, Motion, Personality macros | Phase 9 | 460–476 |
| Any plugin, parameter ID, UI or preset; exposing a Low Note Guard strength control | Phases 10–13 | 489–558 |
| `AdditiveOscillator` (IFFT) as the generator — **rejected** by the roadmap for its FFT latency | — | 161 |

Phase 1 adds **no modulation source**, so the `ModulationSource` concept constraint does not apply here.

---

## Clarifications

### Session 2026-10-09

Each line: question, the user's decision, and the FR/SC ids that enforce every behavioural clause of it.

- Q1 — What moves the base slope law along sine → triangle → saw, and how are the anchors reached from six
  `Controls`? → **Depth is the base-law tilt**: depth 0 = 1/n saw, a documented mid value `kDepthTriangle` =
  1/n² triangle, depth 1 = sine; Body and Edge are shape envelopes on top of that slope; the Depth sweep walks
  the sine/triangle/saw path, FR-010 monotonicity follows, anchor coordinates are analytic. [FR-018, FR-010,
  FR-021, SC-002, SC-006]
- Q2 — Pitch-input contract for Phase 4 glides/drops and Phase 9 vibrato? → **`processBlock` takes an optional
  `const float* f0PerSample`** (`nullptr` = hold the `setFrequency` scalar); the core decimates it to its
  pitch-update cadence U; glides and drops are sample-accurate at any host block size. [FR-043, FR-042, FR-049,
  FR-062, SC-020, SC-016]
- Q3 — Are Low Note Guard and Nyquist-cap reductions subject to the slew ceiling during pitch motion? → **No:
  they are a multiplicative mask applied after the slew and never slewed.** FR-050 and SC-010(d) are restated
  over the pre-mask shape vector. The cap tracks pitch exactly; delivered gains may step faster than the
  ceiling during bends, bounded by L_f × pitch speed. [FR-051, FR-050, FR-032, FR-073, SC-010(d), SC-021,
  SC-009]
- Q4 — Full-scale time of the step ceiling? → **About 2.7 ms: kMaxGainSlewPerSec = 375·√P₀ per second**
  (0.5·√P₀ per 64-sample interval at 48 kHz). The SC-010(a)/(b) click and sideband gates remain the pass/fail
  proof; the bank's 2 ms one-pole removes per-interval steps. (Note: the answer also said "4 control
  intervals"; 375·√P₀/s at 48 kHz / 64 is 2 intervals. The numeric rate 375·√P₀/s and "about 2.7 ms" agree and
  are what is encoded.) [FR-050, SC-010(a), SC-010(b), SC-010(d)]
- Q5 — Scope of `bodyEmphasis`? → **The whole gain vector** (base law, Body and Edge); emphasis −1 is odd-only
  everywhere; FR-023's odd/even floor holds trivially; Phase 2's nonlinearity reintroduces even harmonics.
  [FR-013, FR-015, FR-023, SC-005, SC-022]
- Q6 — Build the per-partial inter-channel phase-offset half of the stereo hook now? → **Defer to Phase 7.**
  Phase 1 ships pan offsets only, at identity; no second `HarmonicOscillatorBank` change and no SIMD change in
  this phase. [FR-048(d), FR-064, FR-067, SC-014, SC-023]
- Q7 — Low Note Guard onset? → **C1 = 32.70 Hz, guard always on, onset is a constant, not a user control.**
  SC-011 measures one octave below C1. [FR-030, SC-011(a), SC-011(e)]
- Q8 — Loudness normalisation measure? → **Unweighted power**, Σaₙ² = P₀: deterministic and pitch-invariant;
  perceptual weight is the Mass/Weight macro's job in Phases 6 and 9. [FR-005, SC-003, SC-018]
- OQ-3 — Partial count? → **Fixed at `prepare`, default 64, ceiling 96** (the existing bank maximum); the plan
  chooses 64 vs 96 from the SC-013 CPU figures plus an E1 listening render; **128 is out of scope** and
  `kMaxPartials` = 96 in `harmonic_types.h` is not touched. [FR-041, FR-067, SC-013, SC-016, SC-023]
- D-1 — Body emphasis is the odd/even harmonic bias: confirmed (see Q5). [FR-013, SC-005, SC-022]
- D-2 — Unweighted RMS normalisation and the ±0.5 dB Depth gate: confirmed (see Q8). [FR-005, SC-003]
- D-3 — Every harmonic starts at sine phase 0, output starts at exactly 0, the saw anchor is a true sawtooth:
  confirmed. [FR-045, SC-012, SC-006]
- D-4 — Core output level −12 dBFS RMS per channel: confirmed. [FR-049, SC-018]
- D-5 — Guard onset C1, on by default, constant: confirmed (see Q7). [FR-030, SC-011(e)]
- D-7 — Audibility bars: Body, Edge, Shift each move their descriptor by ≥ 6 dB (energy ratios) or ≥ 1 octave
  (centroid), 0.25 octave and 20 dB where drafted, sweep endpoints ≥ 6 dB apart: confirmed. [FR-071, SC-004,
  SC-005, SC-019]
- D-8 — Anchor tolerances: sine/triangle/saw ±1.5 dB for odd n up to 15; Heavy/Hollow/Growl ±3 dB; suppressed
  harmonics ≤ −50 dB; 5 % L2 shape error; aliasing ≤ −60 dB in the anchor tests: confirmed. The aliasing clause
  was not yet carried by any SC and is now SC-006's aliasing gate; the ±3 dB for the named distributions is
  encoded as rendered-vs-recipe agreement within ±3 dB (their dominance margin of ≥ 3 dB is unchanged).
  [SC-006, SC-007]
- D-9 — Three-part zipper definition (click detector, control-rate sidebands ≤ −60 dB, finite-difference
  smoothness bound) with the slew ceiling re-set per Q4: confirmed. [FR-006, FR-050, SC-010(a), SC-010(b),
  SC-010(c), SC-010(d)]

### Session 2026-10-10 — plan S1 ratification

The user ratified every plan S1 amendment after reading its proof; the amended wording is the governing one.

- C-1 — SC-004 Body span: strictly monotonic at all 27 grid points; ≥ 6 dB at the 22 points whose headroom is
  ≥ 6 dB; ≥ 0.7 × headroom at the 5 depth-0 low-headroom points. → **Accepted.** [SC-004]
- C-2 — SC-004 Shift span gate over the 16-point set (depth ∈ {0, 0.5}, body = edge = 0 excluded); strictly
  increasing still asserted at depth 1 where body + edge > 0; FR-016 reworded to "strictly increasing in `shift`
  whenever body + edge > 0; constant at body = edge = 0". → **Accepted.** [FR-016, SC-004]
- C-3 — Envelope gain 2^(3·shift) and Body entering as body²: internal reparameterisations. → **Accepted.**
  [FR-018, FR-006]
- C-4 / C-5 — Extra spectral and non-finite TUs, two test-support headers; hidden `[.perf]` and `[.listen]`
  tags. → **Accepted.** [FR-070]
- C-6 — Search-calibrated Body-colour coordinates kept; re-voiced by ear in Phase 13 with `recipe-model.js` as
  the regression. → **Accepted.** [SC-019]
- C-8 — dB-linear cap taper (0.09 dB/cent over 1 420 cents, 10-cent tail); SC-001 measures a harmonic iff
  n·f0 < capHz and its delivered gain is ≥ −60 dB re h1. → **Accepted.** [FR-032, SC-001, SC-011(c)]
- C-9 — FR-042's control-grid counter restarts at every `Reset`-policy (or first) `noteOn`; `FreeRunning`
  noteOns leave it alone. → **Accepted.** [FR-042, SC-012, SC-020]
- Task order — CMake registration first (T003, compiling stubs so new tests fail-first), re-audited last
  (T018). → **Accepted.**
- OQ-3 — 64 vs 96 partials is decided in the build stage by plan S9.3's rule; the listening condition is the
  user's verdict on the E1 64-vs-96 renders. `kDefaultPartials` stays 64 until then. → **Unchanged.**

### Session 2026-10-10 — build defects D-1 / D-2 (tasks.md "Defects found")

- D-1 — SC-010(a)'s detector flags the steady saw-like waveform once per period (core 196 / ideal 197 on a
  static C3 note); no implementation of the required timbres can read zero on the raw render. → **Measure on
  the residual core − ideal per-sample reference**; bar stays zero; positive control re-based on the residual.
  [SC-010(a)]
- D-2 — SC-010(b)'s 2 Hz triangle has its own spectral tail in the region (−50.7 dB on depth even at I = 1).
  → **Measure the residual's region power re the core's total**; bar stays −60 dB; triangle kept; held-raw
  staircase positive control added. [SC-010(b)]
- D-3 — "ideal" was undefined for a control step and for the note onset (the bank ramps from zero through its
  2 ms one-pole; the core slews steps per FR-050). → **The ideal includes every spec-mandated smoothing:**
  per-sample recipe targets → FR-050's slew followed continuously per sample → the bank's 2 ms one-pole from
  zero at noteOn, `Reset` phases, per-sample frequency, same mask. The residual is then the control-rate and
  pitch-update quantisation only; the splice positive control keeps its jump. [SC-010(a), SC-010(b)]
- D-4 — With the corrected residual the core shows real control-rate zipper at `kControlInterval` = 64: the
  2.7 ms slew is two sub-steps and the bank's 2 ms one-pole cannot hide a 1.33 ms stair (aligned residual
  −58.5 dB re core on depth, 2 clicks). → **`kControlInterval` = 32 (plan S9.4's fallback, extended to the click
  arm) and the ideal is centred by the zero-order hold's mean delay** ((I−1)/2 samples on steps, (I+1)/2 on
  sweeps). Measured green on all four axes (depth −65.0, body −69.2, edge −73.5, shift −70.0 dB re core,
  0 clicks). f₀ = 140.625 Hz stays valid for SC-010(b); U = 16 unchanged; T021 re-measures CPU against the
  unchanged 1 % budget. [FR-042, FR-050, SC-010(a), SC-010(b), SC-010(d), SC-013]
- OQ-3 closed — E1 listening verdict (user): "in some cases the 96 version has more presence, in others I can't
  really make out much of a difference". Plan S9.3's rule needs all three conditions; (2) is partial and the
  96-partial CPU reads 0.943 % against the 1.0 % bar on the pinned comply run. → **64 ships**; 96 stays
  reachable at `prepare`. [SC-013, FR-041]
- CC-layers — `spectral_shape_recipe.h` (L2) included `processors/harmonic_types.h` (same layer). → the recipe
  restates the ceiling as `kRecipeMaxPartials = 96` and `profundum_core.h` (L3) `static_assert`s it equal to
  `kMaxPartials`; no same-layer include remains. [FR-067, cross-cutting layer discipline]

---

## Existing components (verified this session)

Every row was opened and read on 2026-10-09; the signature column quotes the header.

| Component | Header (path relative to `dsp/include/krate/dsp/`) | Reused for | Verified signature / fact |
|---|---|---|---|
| `HarmonicOscillatorBank` | `processors/harmonic_oscillator_bank.h:75` | The additive generator | Layer 2 (line 62); 96 Gordon-Smith MCF oscillators, SoA, `alignas(32)` (lines 4–9, 1154–1168); non-copyable, movable (lines 117–120). |
| — prepare | `:129` | `ProfundumCore::prepare` | `void prepare(double sampleRate) noexcept` — computes `ampSmoothCoeff_` from `kAmpSmoothTimeSec = 0.002f` (lines 85, 137–138); calls `reset()` (line 193). No allocation. |
| — reset | `:203` | Reset phase policy | `void reset() noexcept` — `sinState_ = 0`, `cosState_ = 1`, `currentAmplitude_ = 0`, `frameLoaded_ = false`, `panPosition_ = 0`, and pan tables filled with a function-local `constexpr float kCenterGain = 0.7071067811865476f` (lines 204–227); invalidates the cached pan/detune bases (`panBaseValid_ = false`, lines 231–232). |
| — loadFrame | `:255` | Per-block gain-vector delivery | `void loadFrame(const HarmonicFrame& frame, float targetPitch, bool skipNormalization = false) noexcept`. Start phases are taken from `partial.phase` **only when `!frameLoaded_`** (lines 286–294); otherwise phase is continuous (FR-039, line 248). With `skipNormalization == false` it rescales to `kTargetOscRms` with a per-call smoothed gain (lines 337–359) — Profundum passes `true` and normalises in the recipe. Recomputes frequencies and anti-alias gains every call (lines 362–363). Triggers the bank's 3 ms crossfade when the pitch ratio since the last call exceeds 1 semitone (lines 260–268). |
| — setTargetPitch | `:386` | Pitch updates and bends | `void setTargetPitch(float frequencyHz) noexcept` — returns early on `frequencyHz <= 0.0f` (line 387), **which a NaN passes**; recomputes ε and anti-alias gains for active partials (lines 400–401); same >1-semitone crossfade trigger (lines 390–397). |
| — applyPanOffsets | `:642` | Phase 7 pan hook (FR-048) | `void applyPanOffsets(const std::array<float, kMaxPartials>& offsets) noexcept` — for every partial, `newPos = clamp(panPosition_[i] + offsets[i], −1, 1)`, `angle = π/4 + newPos·π/4`, `panLeft_[i] = std::cos(angle)`, `panRight_[i] = std::sin(angle)` (lines 647–653); does not mutate `panPosition_`. Nothing in it restores `kCenterGain`. |
| — setStereoSpread | `:540` | **Not used** | Odd partials left / even right, h1 at 25 % (lines 1104–1120) — a parity law, not the frequency-dependent law Phase 7 needs, and it would move h1. Its cached-base restore (lines 552–557) is skipped while `panBaseValid_` is false (always, after `reset()`), and its recompute path builds the tables with `cos`/`sin` of π/4 (line 1123), so `setStereoSpread(0)` is **not** a bit-exact centre restore. Phase 1 never calls it. |
| — applyExternalFrequencyMultipliers | `:663` | Candidate per-sample pitch path (plan decides, FR-043) | `void applyExternalFrequencyMultipliers(const std::array<float, kMaxPartials>& multipliers) noexcept` — multiplies **in place** into `detuneMultiplier_` (line 668); the documented per-sample restore is `setDetuneSpread`'s cached-base copy (lines 579–594). |
| — processStereo / processStereoBlock | `:682`, `:802` | Audio output | `void processStereo(float& left, float& right) noexcept`; `void processStereoBlock(float* leftOutput, float* rightOutput, size_t numSamples) noexcept`. Uses the SIMD kernel when no partial has bandwidth (lines 701–707). Per-partial one-pole amplitude smoothing toward `targetAmplitude_ * antiAliasGain_` (lines 712–713). Output clamp `kOutputClamp = 2.0f` (lines 91, 790–791). |
| — process (mono) | `:829` | **Not used** | Scalar loop, ignores `detuneMultiplier_` and pan (lines 837–864); the stereo path is the SIMD path. |
| — anti-aliasing | `:1061` | Second guard behind the recipe's cap | Full gain below `kAntiAliasFadeStart = 0.8f` × Nyquist, linear fade to 0 at Nyquist, ×`cos(πf/fs)` MCF orbit correction (clamped at 0 past Nyquist) (lines 88, 1062–1088). |
| — stateFinite | `:623` | Non-finite diagnosis in tests | `[[nodiscard]] bool stateFinite() const noexcept` — bit-test based, fast-math-immune (lines 620–631). |
| — public read-only accessors | `:607–618, 933–945` | — | `panRecomputeCount`, `detuneRecomputeCount`, `getStereoSpread`, `getDetuneSpread`, `isPrepared`, `getTargetPitch`, `getActivePartials`, `getInharmonicityAmount`, `isFrameLoaded`. **No accessor exposes per-partial phase, amplitude or pan**, which is why FR-047 is withdrawn and FR-048 needs FR-064's addition. |
| `processMcfBatchSIMD` | `processors/harmonic_oscillator_bank_simd.h:33` | Reached through the bank | `void processMcfBatchSIMD(float* sinState, float* cosState, const float* epsilon, const float* detuneMultiplier, float* currentAmplitude, const float* targetAmplitude, const float* antiAliasGain, const float* panLeft, const float* panRight, float ampSmoothCoeff, int numPartials, float& sumL, float& sumR) noexcept` (lines 33–46). Highway runtime dispatch. One shared sin/cos state per partial for both channels — a per-channel phase offset is not expressible through it without a change (D-10). Not called directly by Profundum. |
| `kMaxPartials`, `Partial`, `HarmonicFrame` | `processors/harmonic_types.h:21, 36, 54` | Frame payload | `inline constexpr size_t kMaxPartials = 96;` (line 21). `struct Partial { int harmonicIndex; float frequency; float amplitude; float phase; float relativeFrequency; float inharmonicDeviation; … float bandwidth; }` (lines 36–46). `struct HarmonicFrame { float f0; …; std::array<Partial, kMaxPartials> partials{}; int numPartials = 0; … }` (lines 54–62). |
| Phase-reset idiom | `plugins/membrum/src/dsp/unnatural/mode_inject.h:116–125` | Reset phase policy (FR-046) | `bank_.reset();` then `bank_.loadFrame(frame_, f0, /*skipNormalization=*/true);` — the established way to re-seed start phases. |
| `detail::isFinite` | `core/db_utils.h:118` | Input sanitising (FR-062) | `[[nodiscard]] KRATE_DETAIL_FORCEINLINE constexpr bool isFinite(float x) noexcept` — the fast-math-immune check the bank itself includes (bank line 31). |
| `midiNoteToFrequency` | `core/midi_utils.h:71` | Test fixtures only | `[[nodiscard]] constexpr float midiNoteToFrequency(int midiNote, float a4Frequency = kA4FrequencyHz) noexcept`. The core takes f₀ in Hz; note→Hz conversion is the caller's (Phase 4/9). |
| `semitonesToRatio` | `core/pitch_utils.h:23` | Test fixtures (bend trajectories) | `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept`. |
| `PhaseAccumulator` | `core/phase_utils.h:154` | **Not used in Phase 1** | `struct PhaseAccumulator { double phase; double increment; bool advance(); void reset(); void setFrequency(float, float); }` (lines 154–181). The roadmap lists it in the row (line 161); it is Phase 3's candidate for the Anchor phase lock. Phase 1's harmonic phases live in the bank's MCF state. |
| `AdditiveOscillator` | `processors/additive_oscillator.h:62` | **Rejected** | `kMaxPartials = 128` (line 69); `size_t latency() const noexcept { return prepared_ ? fftSize_ : 0; }` (lines 394–396) — one full FFT frame of latency, which the roadmap rejects (line 161). |
| `Window::generateBlackmanHarris` / `generateKaiser` | `core/window_functions.h:179, 155` | **Not sufficient for SC-001/SC-009** | The 4-term Blackman–Harris is documented at ≈ −92 dB sidelobes (line 177); the Kaiser generator computes in `float` (lines 155–169). Neither reaches the ≤ −140 dB sidelobe floor SC-001/SC-009 need, so those tests generate a 7-term Blackman–Harris in `double`, test-locally (no new library component). |
| `FFT` | `primitives/fft.h:147` | Test analysis | pffft-backed `prepare(size_t)` accepts any power of two that `pffft_new_setup` accepts (lines 147–159); `kMaxFFTSize = 8192` (line 47) is a documented constant, not enforced in `prepare`. `low_frequency_metrics.h` already analyses `kLowFrequencyFftSize = 262144`-point frames (line 64) through it. |

Test helpers (verified, `tests/test_helpers/`): `low_frequency_metrics.h` — `magnitudeSpectrum(const float* x, std::size_t n, std::vector<float>& mags)` (line 118, **periodic Hann** window, lines 104–150), `harmonicMainLobePower(const std::vector<float>& mags, double harmonicHz, double binHz)` (line 180; its ±2-bin main-lobe sum is exact only for the Hann window it is paired with — its "identically zero beyond 2 bins" comment, lines 172–176, does not make Hann *sidelobes* vanish for non-bin-centred tones), `measureLowFrequencyThdPercent(...)` (line 308), `calculateCorrelation(const float* a, const float* b, std::size_t n)` (line 478); `artifact_detection.h` — `ClickDetectorConfig` (line 38; defaults `frameSize = 512`, `hopSize = 256`, `detectionThreshold = 5.0f`, `energyThresholdDb = −60.0f`, `mergeGap = 5`, lines 39–44) and `ClickDetector` (line 99); `allocation_detector.h` — `AllocationScope` (line 111); `render_fingerprint.h` — `fingerprintRender(std::span<const float>)` (line 73), `compareFingerprints(...)` (line 122).

## New components

| Class | Layer | Header | ODR sweep (`grep -rnE "(class|struct|enum class|enum|using) <Name>\b" dsp/ plugins/ tests/ tools/`, 2026-10-09) |
|---|---|---|---|
| `SpectralShapeRecipe` | 2 | `processors/spectral_shape_recipe.h` | **0 hits.** File does not exist. |
| `ProfundumCore` | 3 | `systems/profundum_core.h` | **0 hits.** File does not exist. |
| Nested types only: `SpectralShapeRecipe::Controls` (struct), `ProfundumCore::RetriggerPhase` (enum class) | — | — | Nested, so no namespace-scope ODR exposure. The namespace-scope spellings were swept anyway: `SpectralShapeParams`, `SpectralShape`, `RetriggerPhasePolicy`, `CorePhasePolicy`, `ProfundumCorePhasePolicy`, `LowNoteGuard` — **0 hits each.** |
| New bank method (name the plan's; candidate `restoreCenterPan`) | 2 (existing class) | `processors/harmonic_oscillator_bank.h` | Member function, no namespace-scope exposure; `restoreCenterPan` / `resetPanToCenter` — **0 hits** in `dsp/` and `plugins/`. |

Near-name check: `SpectralState` (`processors/spectral_state.h:44`), `SpectralStateId` (`:317`) and
`SpectralTilt` (`processors/spectral_tilt.h:88`) exist; none collides with the names above. None of the
roadmap's hazard names (`CharacterProcessor`, `SubOscillator`, `StereoField`, `MorphEngine`, `MorphNode`,
`TransientDetector`, `HarmonicCloud`, `SpectralState`, `MacroMapper`, roadmap lines 174–176) is used. The
test-local 7-term Blackman–Harris generator lives inside the test TUs' anonymous namespace.

Include discipline: `spectral_shape_recipe.h` includes Layer 0 (`core/`) and the stdlib only — plus
`processors/harmonic_types.h` for `kMaxPartials`, a same-layer header with no further dependencies.
`profundum_core.h` includes the recipe and `processors/harmonic_oscillator_bank.h` (Layer 2) and
Layer 0. Nothing points upward.

---

## Functional Requirements

### Descriptor definitions (used by FRs and SCs)

For a gain vector a with power weights wₙ = aₙ² over n = 1…N, or for a render where wₙ is the
measured power of harmonic n (with the window named by the SC that uses it):

- **Sub energy** E_sub = w₁; **Body energy** E_body = Σ wₙ, n = 2…8 (the 2026-10-09 "≈ 2–8" ruling,
  roadmap line 207); **Presence energy** E_pres = Σ wₙ, n = 9…N. These are the roadmap's Sub / Body /
  Presence descriptors (line 113) expressed in harmonic order — descriptors, not bands in the signal path.
- **h1/rest ratio** = w₁ / Σₙ≥₂ wₙ (dB). **Low-order energy ratio** R_body = E_body / E_total (dB).
  **High-order energy ratio** R_pres = E_pres / E_total (dB).
- **Harmonic centroid** C = Σ log₂(n)·wₙ / Σ wₙ (octaves above h1). **Harmonic spread** σ = the
  energy-weighted standard deviation of log₂(n) (octaves).
- **Odd/even ratio** = Σ wₙ (n odd, n ≥ 3) / Σ wₙ (n even) (dB). Where an SC compares it as a distance
  axis it is clipped to [−30, +30] dB so an odd-only state (even energy ≈ 0) stays finite.
- **Normalised descriptor distance** between two states: Euclidean distance over
  (R_body dB, R_pres dB, 6·C, 6·σ) — octaves weighted 6 dB/oct so the four axes share units.
- **Body-colour distance**: the normalised descriptor distance extended by a fifth axis, the clipped
  odd/even ratio (dB).

### FR-001 series — `SpectralShapeRecipe`: contract

- **FR-001** `SpectralShapeRecipe` is a Layer 2, header-only, stateless component: a pure function of
  its inputs (Controls, f₀ in Hz, sample rate, harmonic count N) to a gain vector a₁…a_N written into a
  caller-owned span. Same inputs → bit-identical output within one binary. (Roadmap line 203: "pure
  math, no audio".)
- **FR-002** `SpectralShapeRecipe::Controls` holds exactly: `depth` [0, 1], `body` [0, 1],
  `bodyCurvature` [0, 1], `bodyEmphasis` [−1, 0], `edge` [0, 1], `shift` [−1, +1]. Out-of-range values
  are clamped; non-finite values are replaced by the field's documented default (FR-062). It is plain
  data so Phase 8 can interpolate it (roadmap line 431). It holds **no** note-behaviour field: the Low
  Note Guard is a fixed property of the recipe (FR-030), because the concept files the guard under Note
  Behaviour (concept line 591) and `MorphState` excludes note behaviour (roadmap line 425).
- **FR-003** N is any value in [1, `kMaxPartials`] (`harmonic_types.h:21`, = 96). aₙ ≥ 0 for all n.
- **FR-004** Evaluation is allocation-free, lock-free, exception-free and `noexcept`.
- **FR-005** **Loudness normalisation:** after every other stage (shape, guard, cap) the vector is
  scaled to a fixed power, Σ aₙ² = P₀, so no control — Depth included — is a volume knob (roadmap
  lines 122, 205). P₀ is derived from FR-049's output level: P₀ = 2·(10^(kCoreOutputRmsDb/20) /
  kCenterGain)², kCenterGain = 0.7071067811865476 (bank line 225), i.e. P₀ ≈ 0.2524. The pre-normalisation
  h1 gain has a documented floor > 0 in every state, and h1 is exempt from the Nyquist cap (FR-032), so
  the normalisation never divides by zero and is continuous everywhere. The measure is **unweighted power**
  (no K-weighting, equal-loudness or harmonic-order weighting): deterministic and pitch-invariant;
  perceptual weight is the Mass/Weight macro's job (Phases 6 and 9), not the recipe's.
- **FR-006** The vector is a continuous, Lipschitz function of every control and of log₂ f₀, with
  **stated** ceilings: for any change of one control by Δ (in that control's own units),
  max |Δaₙ| ≤ L_c·|Δ| with **L_c ≤ 8·√P₀**; for any change of f₀ by Δ octaves,
  max |Δaₙ| ≤ L_f·|Δ| with **L_f ≤ 8·√P₀ per octave**. The recipe's actual constants are documented in
  the header and must not exceed these ceilings. No step anywhere in the control space or in f₀,
  including at the Low Note Guard onset, inside the Nyquist cap taper, and at the f₀ clamp ceiling
  (FR-032). These ceilings bound the recipe; the delivered per-block amplitude delta the roadmap names
  (line 239) is bounded by FR-050 on the pre-mask shape vector and, under pitch motion, by L_f (FR-051,
  SC-021).

### FR-010 series — The four controls

- **FR-010 Depth** sets fundamental dominance by tilting the base slope law (FR-018): the h1/rest ratio is
  strictly increasing in `depth` at every fixed (B, E, S, curvature, emphasis, f₀). The Depth sweep walks the
  sine / triangle / saw path. `depth = 1` yields a near-pure sine (rest ≤ −30 dB re h1) at any B/E/S.
  (Roadmap line 205.)
- **FR-018 Base law (Depth is the tilt).** Beneath the Body and Edge envelopes the recipe has a base slope law
  Aₙ ∝ n^(−p(depth)), a smooth, monotone function of `depth` documented in the header: depth 0 → p = 1 (the
  1/n sawtooth law); `kDepthTriangle` (a documented value in (0, 1)) → p = 2 exactly (the 1/n² triangle
  law); depth 1 → the sine (h1 only; rest ≤ −60 dB re h1 at body = edge = 0). Body and Edge are shape
  envelopes multiplied onto this slope, never alternative slopes. The anchor coordinates are analytic, not
  found by search or tuning: `kSawAnchor.depth` = 0, `p(kTriangleAnchor.depth)` = 2, `kSineAnchor.depth` = 1,
  all with body = edge = 0, and with `bodyEmphasis` = −1 for the triangle (odd-only, FR-013) and 0 for saw and
  sine. The h1 floor of FR-005 keeps p(depth) → h1-only at depth 1 Lipschitz (FR-006).
- **FR-011 Body** is a smooth envelope over the low-order harmonics, nominally centred in n ≈ 2–8 (roadmap
  line 207), Gaussian-like in log₂ n. `body` sets its amount: R_body strictly increasing in `body`.
- **FR-012 Body curvature** sets the envelope's breadth/peakedness in log-n (broad and round ↔ narrow
  and nasal). It must move the harmonic spread σ monotonically (audibility, FR-071).
- **FR-013 Body emphasis** is an odd-harmonic bias that acts on the **whole gain vector** — the base law
  (FR-018), the Body envelope and the Edge envelope alike, never the Body region alone: `bodyEmphasis` ∈
  [−1, 0], −1 = odd-only **everywhere** (every even n ≥ 2 at or below −50 dB re h1 at any D/B/E/S/curvature,
  SC-022; hollow / triangle-like), 0 = neutral (a saw's natural all-harmonic parity). The odd/even ratio is strictly decreasing in `bodyEmphasis` over [−1, 0]. The
  recipe never favours even harmonics beyond the neutral law: even harmonics and new harmonics come from
  Phase 2's nonlinearity, not from the recipe (roadmap lines 221–222, 254–256). "Thick" (line 209) is
  reached through Body amount, curvature and Shift, not through even-harmonic bias (FR-014). (Roadmap
  line 208 "adjustable curvature and emphasis"; the odd-harmonic triangle anchor of line 219 is
  unreachable by smooth log-n envelopes alone — decision D-1.)
- **FR-014** The five Body colours named by the roadmap (round ↔ hollow ↔ woody ↔ nasal ↔ thick, line
  209) are each published as a `constexpr` `Controls` coordinate — `kBodyRound`, `kBodyHollow`,
  `kBodyWoody`, `kBodyNasal`, `kBodyThick` — documented in the header's table. They are descriptive
  anchors for later presets, not separate modes, and each must be reachable and audibly distinct
  (SC-019): round = the lowest harmonic centroid C of the five; hollow = the highest odd/even ratio;
  nasal = the smallest harmonic spread σ; thick = the highest R_body; woody = distinct from all four by
  the SC-019 distance bar.
- **FR-015 Edge** is a smooth envelope raising the high-order partials (smooth ↔ bright ↔ buzzy, roadmap
  line 210): R_pres strictly increasing in `edge`. At `edge = 0` the recipe adds no Edge envelope. Edge is subject to `bodyEmphasis` like the rest of the vector
  (FR-013): at `bodyEmphasis = −1` raising Edge adds odd harmonics only.
- **FR-016 Shift** moves the Body and Edge envelope centres together along the log₂-harmonic-number axis
  (roadmap lines 212–214): −1 heavier/rounder, +1 upper-partial emphasis, 0 nominal centres. The
  harmonic centroid C is strictly increasing in `shift`. Shift is not a pitch shift (harmonic
  frequencies are unchanged) and not an EQ sweep (the gains are regenerated in harmonic order, so the
  result is pitch-invariant — FR-031).
- **FR-017** The envelopes overlap, are combined and then normalised (FR-005). No hard boundary in n:
  the gain of every harmonic is a smooth function of log n (roadmap lines 121–123, 223–224). The
  controls are perceptually distinct, not mathematically independent (line 224).

### FR-020 series — Anchor points and named distributions

- **FR-020** The recipe publishes `constexpr` named `Controls` coordinates: `kSineAnchor`,
  `kTriangleAnchor`, `kSawAnchor`, `kHeavy`, `kHollow`, `kGrowl`, plus FR-014's five Body colours.
- **FR-021** **Waveform anchors** (roadmap lines 218–220): at `kSineAnchor` the vector matches a pure
  sine; at `kTriangleAnchor` it matches the odd-harmonic 1/n² law; at `kSawAnchor` it matches the all-
  harmonic 1/n law — each within the SC-006 tolerance, measured in magnitude only (phases are FR-045's).
  The anchors are reached by the Depth-driven base law of FR-018 (body = edge = 0; `bodyEmphasis` = −1 for
  the triangle), whose coordinates are analytic; the law remains a smooth function of log n (FR-017).
- **FR-022** **Named distributions** (roadmap lines 214–217, concept lines 176–198): at `kHeavy`
  E_sub > E_body > E_pres; at `kHollow` E_body > E_sub > E_pres; at `kGrowl` E_pres > E_body > E_sub —
  the bar ordering the concept draws — each with the SC-007 dominance margin.
- **FR-023** The *asymmetric → clipped → increasingly complex* tail of the waveform path is **not**
  reachable by the recipe and must not be emulated by it (roadmap lines 220–222). In particular no
  coordinate of the control space has an odd/even ratio below the neutral (saw) law's at the same
  shape (FR-013). Because `bodyEmphasis` acts on the whole vector the floor holds by construction; even
  harmonics are reintroduced only by Phase 2's nonlinearity.

### FR-030 series — Low Note Guard, Nyquist cap, pitch invariance

- **FR-030 Low Note Guard** (roadmap lines 225–226, concept lines 591–597): a **fixed** documented taper
  that reduces the energy of upper harmonics as f₀ falls below `kLowNoteGuardOnsetHz` = **32.70 Hz
  (C1)**. It has no strength control (no field in `Controls`, no recipe argument). The onset is the compile-time
  constant `kLowNoteGuardOnsetHz`, not a runtime parameter or user control, and the guard is always on (no
  bypass); a 5-string low B (30.87 Hz), C0 and the tail of any drop below C1 are therefore guarded, as the
  header documents. Properties: identity
  at and above the onset; monotonically stronger as f₀ falls; attenuation non-decreasing with n; h1 is
  never attenuated and h1's share of E_total is never reduced by the guard; continuous in f₀ (FR-006).
  The taper law is a documented formula in the header. C1 is chosen so SC-008's C1 → C3 invariance holds
  with the guard present while C0 (the concept's "extremely low C") is guarded.
- **FR-031 Pitch invariance:** for f₀ ≥ `kLowNoteGuardOnsetHz` and no harmonic n ≥ 2 inside the cap
  taper, aₙ does not depend on f₀ — the spectrum is defined in harmonic order (roadmap line 236
  "character tracks the keyboard").
- **FR-032 Nyquist cap:** every harmonic **n ≥ 2** whose frequency n·f₀ is at or above
  `kNyquistCapFraction` × (fs/2) has aₙ = 0, approached through a continuous taper that obeys FR-006's
  L_f. **h1 is exempt from the cap and its taper**: FR-062 clamps f₀ to at most
  `kNyquistCapFraction` × (fs/2), so h1 never exceeds the cap frequency. At the clamp ceiling every
  harmonic n ≥ 2 is at or above 2× the cap and therefore zero, so h1 alone carries P₀, reached
  continuously as h2 tapers out. `kNyquistCapFraction` ≤ `HarmonicOscillatorBank::kAntiAliasFadeStart`
  (0.8, bank line 88), so the recipe, not the bank's fade, defines the spectrum; the bank's fade remains
  a second guard and is flat (gain 1 after the orbit correction) for h1 at the clamp ceiling. The cap is
  applied as part of the never-slewed mask (FR-051): it tracks f₀ exactly, so a partial that crosses the cap
  during a bend is zero in the first delivered vector in which it is at or above the cap.

### FR-040 series — `ProfundumCore`

- **FR-040** `ProfundumCore` is a Layer 3, header-only class owning one `HarmonicOscillatorBank`, one
  `HarmonicFrame` and its gain buffers — no heap allocation anywhere; all storage fixed-size members.
- **FR-041** `prepare(double sampleRate, int numPartials)`: `numPartials` is a prepare-time constant (fixed
  for the life of a prepared instance; changed only by calling `prepare` again) in [1, `kMaxPartials` = 96],
  default `kDefaultPartials = 64` (roadmap line 228); values above 96 are clamped to 96. 96 is the bank's
  existing maximum: 128 is **out of scope** and `kMaxPartials` in `harmonic_types.h` is not touched
  (FR-067). The plan decides whether the shipped default stays 64 or becomes 96 from SC-013's 64- and
  96-partial CPU figures plus an E1 listening render (OQ-3, resolved). Supported sample rates 22.05–192 kHz. `prepare` and `reset` are control-thread calls; every
  other method is audio-thread safe.
- **FR-042** **Per-block recipe evaluation:** controls are latched per control interval (host blocks
  are subdivided at a documented `kControlInterval` ≤ 64 samples, counted from a global sample counter since
  `prepare`/`reset` rather than from the block start, so the render does not depend on the host block size,
  SC-020); the recipe is evaluated once per
  interval and delivered via `loadFrame(frame, f0, /*skipNormalization=*/true)`
  (`harmonic_oscillator_bank.h:255`) through FR-050's step ceiling and then FR-051's mask; per-sample amplitude smoothing
  is the bank's one-pole (`kAmpSmoothTimeSec`, line 85). The recipe is skipped when neither controls nor
  the capped/guarded partial set changed and FR-050's slew has converged. (Roadmap line 229.)
- **FR-043** **Pitch input:** `setFrequency(float f0Hz)` sets the held target f₀, and `processBlock`
  takes an optional `const float* f0PerSample` (FR-049): `nullptr` = hold the `setFrequency` scalar;
  non-null = a per-sample f₀ trajectory in Hz, `numSamples` long, that overrides the scalar for that block,
  after which the scalar becomes its last (sanitised, FR-062) value. The core decimates the trajectory to its
  pitch-update cadence U (it samples `f0PerSample` every U-th sample, U counted from the global sample
  counter, not the block start), so glides and drops (Phase 4's `DropEnvelope`, portamento, Phase 9 vibrato)
  are sample-accurate to within U at any host block size, and a 512-sample host block can carry a 10 ms drop
  without a staircase (SC-020). The core moves from the previous f₀ to the new one without per-block steps —
  at a cadence fine enough to meet SC-010's zipper
  bounds and cheap enough to meet SC-013's CPU bound, both measured with vibrato active. Mechanism is a
  plan decision between sub-block `setTargetPitch` (line 386; costs two trig calls per partial per update)
  and a per-sample frequency multiplier via `applyExternalFrequencyMultipliers` (line 663) with the
  documented base restore (lines 579–594). The plan states the resulting pitch-update cadence U (samples
  between updates; U = 1 for per-sample) because SC-010(b) derives its sideband set from it. Pitch
  updates are emitted so that no single bank update exceeds the bank's 1-semitone crossfade threshold
  (lines 390–397) except on a note-on jump (FR-046 / edge case E-7).
- **FR-044** Harmonic frequencies are exact integer multiples of f₀: every frame partial has
  `harmonicIndex = n`, `relativeFrequency = n`, `inharmonicDeviation = 0`, `bandwidth = 0` (so the
  bank's SIMD path is always taken, line 701, and its noise generator is never used).
- **FR-045** **Deterministic phases:** every harmonic starts at sine phase 0 (`partial.phase = 0`, so
  `sinState = 0`, `cosState = 1`, bank lines 289–291) on every phase-reset event. The output therefore
  starts at exactly 0.0, and at `kSawAnchor` the waveform is a true sawtooth (decision D-3).
- **FR-046** **Retrigger phase policy input** (roadmap line 230): `ProfundumCore::RetriggerPhase
  { Reset, FreeRunning }`, set by the caller and consumed on `noteOn(float f0Hz)`, which takes effect at
  sample offset 0 of the next `processBlock` (by contract; there is no mid-block note-on in Phase 1 —
  sample-accurate note timing is the caller's, Phase 4/9). `Reset` re-seeds every partial to FR-045's
  phases via the `reset()` + `loadFrame(…, true)` idiom (`mode_inject.h:116–125`); `FreeRunning` keeps
  the MCF state and only changes pitch (a `FreeRunning` `noteOn` at the current f₀ changes nothing).
  Choosing the policy and making the transition click-free are Phase 4's (Non-Goals).
- **FR-047** *Withdrawn (review 2026-10-09).* Phase 1 exposes no h1 phase or reset readout. A reset
  flag with an always-zero offset cannot support the 60 s zero-drift lock Phase 3 requires (roadmap
  line 296), and FreeRunning has no reset to report; the lock mechanism belongs to Phase 3 (line 284).
- **FR-048** **Per-partial pan-offset hook** (roadmap lines 127, 230, 393–395):
  `setPartialPanOffsets(const std::array<float, kMaxPartials>& offsets)`. Rules:
  (a) while the vector is all zeros (the default and the only value Phase 1's product path sets) the
  core does **not** call `applyPanOffsets`, because that call rewrites the pan tables as `cos`/`sin` of
  π/4 (bank lines 650–652), which need not be bit-equal to each other;
  (b) a non-zero vector is forwarded once per control interval through `applyPanOffsets` (line 642);
  (c) on a transition from a non-zero vector back to all zeros the core restores the bank's **exact**
  centre tables — every `panLeft_[i]` and `panRight_[i]` equal to the same `kCenterGain` value `reset()`
  writes (bank lines 225–227) — through the append-only bank method of FR-064, without touching phase,
  amplitude or any other bank state, so L == R bit-identically again from the next sample;
  (d) Phase 1 ships the **pan** half of the roadmap's "pan/phase offsets" (line 127) only; per-partial
  inter-channel phase offsets / decorrelation (line 394) are deferred to Phase 7 (ruled in the 2026-10-09 clarify session,
  D-10): Phase 1 ships pan offsets only, at identity, and makes no second `HarmonicOscillatorBank` change and
  no SIMD-kernel change (FR-067, SC-023).
  Pan-offset changes are Phase 7's to smooth.
- **FR-049** **Output:** `processBlock(float* left, float* right, size_t numSamples, const float* f0PerSample = nullptr)`
  (FR-043) renders stereo
  through `processStereo`/`processStereoBlock` (lines 682, 802). The mono `process()` (line 829) is not
  used. Steady-state output level per channel with an all-zero pan-offset vector is
  **`kCoreOutputRmsDb` = −12 dBFS RMS** (RMS re 1.0 full scale; ≈ −6 dBFS peak at the saw anchor),
  in every `Controls` state and at every f₀ (consequence of FR-005), leaving headroom for Phase 2's
  nonlinear stages (SC-018).
- **FR-050** **Shape-vector step ceiling** (the roadmap's "per-block amplitude delta bounded", line 239):
  between any two consecutive control intervals, every gain of the core's **pre-mask shape vector** (the
  recipe's guard-free, cap-free, P₀-normalised vector, FR-051) changes by at most **kMaxGainSlewPerSec ×
  kControlInterval / fs**, with **kMaxGainSlewPerSec = 375·√P₀ per second** (= 0.5·√P₀ per 64-sample
  interval at 48 kHz; a full-scale √P₀ change takes ≥ about 2.7 ms). The core approaches the recipe's target
  shape at that rate; every shape vector keeps Σaₙ² = P₀ within 1e-4 relative (the internal slew step and
  renormalisation order are the plan's). The bank's 2 ms one-pole (`kAmpSmoothTimeSec`) removes the
  per-interval steps that remain; the SC-010(a)/(b) click and sideband gates, not this ceiling, are the
  pass/fail proof. The core exposes the pre-mask shape vector read-only
  (`[[nodiscard]] std::span<const float> shapeGains() const noexcept`) and the last delivered (masked)
  vector (`deliveredGains()`) so SC-010(d) and SC-021 can measure them. One exemption, asserted separately:
  a `Reset` `noteOn` delivers the target shape directly (the bank's amplitudes restart from 0 and ramp
  through its 2 ms one-pole, FR-045). The Low Note Guard and the Nyquist cap are **not** part of this
  ceiling: they are FR-051's mask.
- **FR-051** **Two-stage evaluation; the guard/cap mask is never slewed.** The recipe exposes its evaluation
  as two composable stages: (1) **shape** — the P₀-normalised vector from `Controls` alone, with no guard and
  no cap, independent of f₀ (so FR-031 holds by construction); (2) **mask** — a per-harmonic factor in
  [0, 1] equal to the Low Note Guard taper (FR-030) × the Nyquist-cap taper (FR-032), computed from f₀ and fs
  alone, followed by renormalisation of the masked vector to P₀ (FR-005). Composing (1) then (2) equals the
  single full evaluation of FR-001 within 1e-6 relative. In the core the pipeline is shape → slew (FR-050) →
  mask (+ renormalisation) → `loadFrame`; the mask is recomputed every control interval from the current f₀
  and is never slewed. Consequences, all asserted by SC-021: (a) the cap tracks pitch exactly — no partial at
  or above the cap frequency carries gain in the vector delivered for that interval; (b) delivered gains may
  change faster than FR-050's ceiling during pitch motion, bounded by L_f (FR-006) times the pitch change:
  with controls converged, |Δ `deliveredGains()`ₙ| ≤ L_f × (octaves f₀ moved in that interval); (c) every
  delivered vector keeps Σaₙ² = P₀ within 1e-4 relative; (d) on any `noteOn` the mask of the new f₀ is
  already in the first delivered vector (partials at or above the cap are zero in it); h1 is exempt from the
  cap (FR-032).
### FR-060 series — Real-time safety, robustness, determinism, portability

- **FR-060** No allocation, lock, exception or I/O in any audio-thread method of either component;
  every such method is `noexcept`.
- **FR-061** Before `prepare`, `processBlock` outputs silence and setters are no-ops.
- **FR-062** Non-finite inputs (f₀, any control) are rejected with `detail::isFinite`
  (`core/db_utils.h:118`) — never `std::isnan` — before reaching the bank: a non-finite f₀ keeps the
  previous f₀ (the bank's own `<= 0.0f` guard, line 387, lets NaN through); the same rule and clamp apply to every
  element of an `f0PerSample` trajectory (a non-finite element holds the previous sample's f₀); a non-finite control keeps
  its previous value. f₀ is clamped to [`kMinF0Hz` = 8 Hz, `kNyquistCapFraction` × fs/2]; the upper
  clamp is what keeps h1 (exempt from the cap, FR-032) below the cap frequency.
- **FR-063** No randomness: Phase 1 owns no RNG and no seed. Two instances given the same call sequence
  produce bit-identical output within one binary; cross-toolchain comparisons use tolerances only
  (`render_fingerprint.h`), never bit-exact float goldens.
- **FR-064** **Shared-header change, append-only:** `HarmonicOscillatorBank` gains exactly one public
  `noexcept` method that refills `panLeft_` and `panRight_` with the same `kCenterGain` value `reset()`
  uses (bank lines 225–227) and touches nothing else (FR-048(c)). Nothing existing in the bank changes
  behaviour; if `kCenterGain` is hoisted from `reset()` to class scope to share it, `reset()`'s output
  must stay bit-identical. The Ruinae, Seraphis, Vorago, Innexus and Membrum suites stay green (roadmap
  lines 597–598; Membrum is added because `mode_inject.h` also consumes the bank). Decorrelation was ruled out of Phase 1 by the
  clarify stage (D-10, FR-067): this is the only bank change in this phase.
- **FR-065** Portable code: no narrowing in brace initialisation; no aligned SIMD loads introduced (none
  are needed — the SIMD is the bank's existing kernel); passes `node tools/check-portability.js` and
  `tools/lint-apple-globals.js`.
- **FR-066** Naming per CLAUDE.md: PascalCase classes, camelCase functions, trailing-underscore members,
  `kPascalCase` constants.

- **FR-067** **Shared-header footprint.** Phase 1's whole footprint on shared DSP headers is the single
  FR-064 method: (a) `harmonic_oscillator_bank_simd.h`/`.cpp` and every SIMD kernel are untouched — there is
  no per-partial inter-channel phase-offset path and no second `HarmonicOscillatorBank` change (deferred to
  Phase 7); (b) `harmonic_types.h` is untouched — `kMaxPartials` stays 96 and 128 partials is out of scope;
  (c) `additive_oscillator.h` is untouched. Verified by a scripted diff check against the phase base,
  recorded in comply (SC-023).

### FR-070 series — Tests, audibility, registration

- **FR-070** Tests: `dsp/tests/unit/processors/spectral_shape_recipe_test.cpp` (→ `dsp_processors_tests`)
  and `dsp/tests/unit/systems/profundum_core_test.cpp` plus a separate
  `profundum_core_perf_test.cpp` (→ `dsp_systems_tests`), registered in `dsp/tests/CMakeLists.txt`; the
  bank addition is covered in the existing bank test file under `dsp/tests/unit/processors/`. The perf
  TU and any multi-second render are tagged so per-push CI excludes them (`[performance]`, `[long]` per
  CLAUDE.md); NaN/Inf-guard and L == R identity tests are never `[long]`. Any TU that injects NaN/Inf by
  bit pattern is listed in the `-fno-fast-math` source list; the perf TU is not.
- **FR-071** **Audibility (theme-level FR, roadmap lines 53, 582–584):** every lever of both components —
  Depth, Body, Body curvature, Body emphasis, Edge, Shift, the five Body colours, the Low Note Guard, and
  the retrigger phase policy — has a render-descriptor gate measured on the *rendered audio* of
  `ProfundumCore`, not only on the recipe vector (SC-004, SC-005, SC-011, SC-012, SC-019).
- **FR-072** **Mono compatibility (theme-level FR, lines 585–586):** the core is the first
  stereo-capable stage; with an all-zero pan-offset vector — including after a return from a non-zero
  one — L == R bit-identically, so the mono sum has no cancellation (SC-014).
- **FR-073** **Anti-aliasing (theme-level FR, lines 587–588):** Phase 1 has no nonlinear stage; its
  aliasing obligation is the Nyquist cap, applied as the never-slewed mask of FR-051, gated by SC-009 and
  SC-021.
- **FR-074** **CPU budget is an FR** (lines 589–591): SC-013, never relaxed to pass.

---

## Success Criteria

**Notation.** "Sweep" = 33 evenly spaced control values over the field's full range unless stated.
"Grid" = the 27 combinations of the three *other* main controls (of D, B, E, S) at **{0, 0.5, 1}** for
`depth`, `body`, `edge` and **{−1, 0, +1}** for `shift`. During every sweep and grid the secondary
controls are fixed at **`bodyCurvature = 0.5`, `bodyEmphasis = 0`**; the guard is the fixed FR-030
taper; f₀ is stated per test; the pan-offset vector is all zeros. "Mid grid" = depth 0.5, body 0.5,
edge 0.5, shift 0 with the same secondaries. Renders are 48 kHz unless stated.
**Rendered descriptors** are measured on steady state (≥ 50 ms after the last control change, after
FR-050's slew has converged) with `magnitudeSpectrum` + `harmonicMainLobePower`
(`low_frequency_metrics.h:118, 180`) on a frame of `kLowFrequencyFftSize` = 262 144 samples (line 64);
these are ratios of main-lobe powers, which the Hann pairing makes exact (lines 172–176), and the Hann
sidelobe floor (≈ −31 dB, 18 dB/oct) is irrelevant to them because no criterion using this path sums
off-harmonic bins. **SC-001 and SC-009 do sum or isolate off-harmonic content and therefore use the
7-term Blackman–Harris window** (`BH7`: highest sidelobe ≈ −180 dB, main-lobe half-width 7 bins),
generated in `double` in the test, with the FFT length stated per criterion.
**Every SC fixture that renders with an all-zero pan-offset vector asserts L == R bit-identically through
one shared test helper** (SC-014).

| ID | Criterion | Threshold | Method (test-name sketch) |
|---|---|---|---|
| **SC-001** | Partial frequency accuracy, static | every measured harmonic within **< 0.1 cent** of n·f₀ | `ProfundumCore_PartialFrequencyAccuracy`: state **`kSawAnchor`** (edge as the anchor defines it), notes MIDI 12–60 (C0–C4, four octaves, roadmap line 241) at 44.1 / 48 / 96 kHz. Candidate harmonics n ∈ {1, 2, 3, 8, 16, 32, 64}; a candidate is **measured** iff n·f₀ is below the cap taper and its recipe gain is ≥ **−60 dB re h1** in that state (the test asserts this from the recipe vector). The test also asserts n ∈ {1, 2, 3} are measured at every note and that each of 16, 32, 64 is measured at ≥ 1 note in every octave C1–C4 (no silent skips). Frequency = Goertzel phase advance of that harmonic between two **1 s BH7-windowed** frames whose starts are 1 s apart, unwrapped against the nominal n·f₀. With BH7 the leakage from the nearest neighbour (≥ 16 bins away at MIDI 12, 1 Hz bins) is ≤ −100 dB re that neighbour, i.e. a phase error far below the 0.1-cent tolerance (≈ 0.006 rad at h1 of MIDI 12). |
| **SC-002** | Depth monotonic in h1/rest | strictly increasing over the Depth sweep at every grid point; recipe and render both. The sweep is augmented with the point `depth = kDepthTriangle`; at body = edge = 0 and `bodyEmphasis = 0` the base-law exponent p, measured as the least-squares log-log slope of aₙ over n = 1…15, is **1 at depth 0** and **2 at `kDepthTriangle`** (each within ±0.05), and the rest is ≤ −60 dB re h1 at depth 1 (FR-018) | `SpectralShapeRecipe_DepthMonotonic`, `ProfundumCore_DepthRenderMonotonic` (render at C1, C2, C3), `SpectralShapeRecipe_DepthWalksWaveformPath`. |
| **SC-003** | Depth is not a volume knob | rendered **unweighted** RMS per channel (no K-weighting or other frequency weighting, FR-005) within **±0.5 dB** of the sweep median at every Depth step, every grid point, C1/C2/C3; recipe Σaₙ² = P₀ within 1e-4 relative for 10 000 random Controls × f₀, with f₀ drawn log-uniformly over **[8 Hz, the clamp ceiling]** at 44.1 / 48 / 96 kHz (so the cap taper and the h1-only region are sampled) | `ProfundumCore_DepthLoudnessFlat`, `SpectralShapeRecipe_PowerNormalised`. |
| **SC-004** | **Audibility gate 1** — Body, Edge, Shift each move their primary descriptor monotonically and distinguishably | Body → R_body, Edge → R_pres, Shift → C: each strictly monotonic over its sweep at every grid point; full-sweep span ≥ **6 dB** (R_body, R_pres) and ≥ **1 octave** (C); pairwise normalised descriptor distance between the Body-max, Edge-max and Shift-max endpoint states (from mid grid) ≥ **6 dB**. Measured on recipe vectors and on renders at C1, C2, C3. | `ProfundumCore_AudibilityGate_BodyEdgeShift`. |
| **SC-005** | Body sub-controls audible | curvature: σ strictly monotonic over its sweep, span ≥ **0.25 octave**; emphasis: odd/even ratio strictly monotonic over the sweep of **[−1, 0]**, span ≥ **20 dB** — rendered at C2, mid grid; plus the FR-023 floor: at every point of the emphasis sweep the odd/even ratio is ≥ the neutral (`bodyEmphasis = 0`) value | `ProfundumCore_AudibilityGate_BodyCurvatureEmphasis`. |
| **SC-006** | Waveform anchors reached | `kSineAnchor`: rest ≤ **−60 dB** re h1. `kTriangleAnchor`: odd n ≤ 15 within **±1.5 dB** of 1/n² (re h1), every even n ≤ **−50 dB** re h1. `kSawAnchor`: n ≤ 16 within **±1.5 dB** of 1/n, 17 ≤ n ≤ N within **±3 dB**. Plus relative L2 magnitude error ‖a − r‖₂/‖r‖₂ ≤ **5 %** for triangle and saw. The anchor coordinates are analytic (FR-018): `kSawAnchor.depth == 0`, `p(kTriangleAnchor.depth) == 2` within 1e-6, `kSineAnchor.depth == 1`, each with body = edge = 0, and `bodyEmphasis = −1` for the triangle only. **Aliasing:** in the rendered anchor tests (all three anchors, C1) the power outside SC-009's exclusion zones (same BH7 method, FFT-length rule and 50 % non-vacuity guard) is ≤ **−60 dB** re total power. Recipe and render at C1. | `SpectralShapeRecipe_WaveformAnchors`, `ProfundumCore_WaveformAnchorsRendered`, `SpectralShapeRecipe_AnchorCoordinatesAnalytic`. |
| **SC-007** | Heavy / Hollow / Growl reached | the FR-022 ordering holds and the dominant region's energy exceeds the second by ≥ **3 dB**, recipe and render, at C1, C2, C3; and the rendered per-harmonic levels (harmonics ≥ −40 dB re the loudest) match the recipe vector within **±3 dB** at the three coordinates | `SpectralShapeRecipe_NamedDistributions`, `ProfundumCore_NamedDistributionsRendered`. |
| **SC-008** | Character tracks the keyboard | for f₀ ≥ `kLowNoteGuardOnsetHz` (guard at identity) and nothing in the cap taper: every harmonic with reference level ≥ −40 dB re h1 within **±0.5 dB** of the recipe vector, at every semitone C1 → C3 (25 notes), for the six named coordinates, the five Body colours and mid grid | `ProfundumCore_SpectrumInvariantC1toC3`. |
| **SC-009** | Energy above Nyquist (aliasing) | aliased power ≤ **−96 dBFS**, where 0 dBFS = the mean-square of a full-scale sine (0.5) and aliased power = the Parseval-normalised power (bin powers scaled so their sum equals the windowed frame's mean square ÷ the window's mean square) summed over every bin from 1 to fs/2 **outside the exclusion zones** | **Static** — `ProfundumCore_NoAliasingAllNotes`: every MIDI note 0–127 at 44.1 / 48 / 96 kHz, state `kSawAnchor` with `edge = 1`, `shift = +1`; frame starts ≥ 50 ms after note start; **BH7** window; FFT length = the smallest power of two ≥ max(2¹³, 64·fs/f₀) (bin width ≤ f₀/64; 2²⁰ at MIDI 0 / 96 kHz); exclusion zone = ±8 bins around every n·f₀ < fs/2 (BH7 main lobe ±7 + 1) and bins 0–8. **Non-vacuity guard:** the test fails if the exclusion zones cover > 50 % of the bins (by construction ≈ 27 %). **During bend** — `ProfundumCore_NoAliasingDuringBend` at 44.1 and 48 kHz, same state: f₀ climbs MIDI 24 → 108 and back in 1-semitone legs, each leg a `setFrequency` glide of **10 ms** (100 semitones/s, faster than a 2 s 24→108→24 glide), followed by a hold. Each analysis frame starts at the **first sample after the leg's last `setFrequency` call plus at most 2 × `kControlInterval`** (so it contains the decay of any partial that crossed the cap during the leg, the risk this case exists for) and uses the static rules at the held f₀ (BH7, same FFT-length rule, ±8-bin exclusion around the held n·f₀, same 50 % guard); the hold lasts frame length + 50 ms. |
| **SC-010** | No zipper under control sweeps and pitch bends | **Measurement signal (amended 2026-10-10, user ruling on build defects D-1/D-2):** both (a) and (b) are measured on the **residual** r[n] = core[n] − ideal[n], where ideal[n] is a double-precision per-sample additive synthesis of the **same** control trajectory and pitch trajectory with the recipe targets evaluated at **every sample** (no control-rate steps, no smoothing), the same start phases (`Reset` alignment), the same frequency law and the same mask. Zipper is by definition the core's deviation from this continuous ideal; the raw render cannot be used because the spec-required saw-like waveforms trip the detector once per period (D-1 measured 196 detections on a static C3 note, 197 on the ideal) and the modulator's own spectral tail lands in the sideband region even with per-sample control (D-2: −50.7 dB on depth at I = 1). The bars are unchanged. **(a) clicks:** `ClickDetector` (`artifact_detection.h:99`) with `ClickDetectorConfig{ .sampleRate = fs, .frameSize = 512, .hopSize = 256, .detectionThreshold = 5.0f, .energyThresholdDb = −60.0f, .mergeGap = 5 }` (the documented defaults, lines 39–44) reports **zero** clicks on the residual for: a 0 → 1 → 0 step of each of D/B/E/S (shift −1 → +1 → −1) with 0.5 s holds, a 10 ms full-range sweep of each, and a ±12-semitone bend in 50 ms — at C2, mid grid. The residual's RMS re the core's RMS is recorded per fixture (diagnostic, not gated). **Positive control (teeth):** the same detector and config must report **≥ 1** click on the residual of a spliced signal: the first half of a steady-state render of the step's start state and the second half of a steady-state render of its end state, both rendered from `Reset` (identical phases), joined at one sample — an instantaneous gain change — minus the ideal of the un-spliced step. **(b) sidebands:** at **f₀ = 140.625 Hz** (= 750 / 5⅓, so at 48 kHz with `kControlInterval` = 64 the k = 1, 2 control-rate sidebands k·750 ± n·f₀ fall ≥ f₀/3 ≈ 46.9 Hz from every harmonic; if the plan picks a different `kControlInterval` it re-derives f₀ by the same rule and records it), with a 2 Hz full-range triangle on each of D/B/E/S in turn, and separately with a 5 Hz ±2-semitone vibrato at `kSineAnchor`: power of the **residual** in the **sideband region** ≤ **−60 dB re the core's total power**. **Positive control (teeth):** the same measure on (held-raw reference − ideal), where the held-raw reference holds the ideal's targets for `kControlInterval` samples with no smoothing, must read **above −60 dB** for at least one lever (D-2's diagnostic put the raw staircase 3–11 dB above the smoothed core). Region = bins within ±20 Hz of k·fs/`kControlInterval` ± n·f₀ (k = 1, 2) and, for the vibrato case, of k·fs/U ± n·f₀ (k = 1, 2; U from FR-043; no pitch-update clause when U = 1), for every n with level ≥ −80 dB re h1; **minus** bins within ±(2 Hz × 5 + 7 bins) of any n·f₀ (the intended triangle AM, first five odd orders) and, for vibrato, within ±(n·Δf_peak + 2 × 5 Hz + 7 bins) of any n·f₀ (Carson band). BH7 window, FFT length 2¹⁷ (0.37 Hz bins), frame starting ≥ 1 s into the modulation. **Non-vacuity guard:** fails if the remaining region holds < 25 % of its nominal (pre-subtraction) bins. **(c) recipe Lipschitz:** FR-006's L_c and L_f ceilings hold by finite difference over 10 000 random pairs (control pairs Δ ≤ 0.01; f₀ pairs ≤ 1 cent apart, across the guard onset, the cap taper and the clamp ceiling, at 44.1 / 48 / 96 kHz). **(d) shape-vector step ceiling:** in every (a) and (b) fixture (all at an f₀ where the mask is the identity: C2, and the ±12-semitone bend never goes below the guard onset), max over n and over consecutive control intervals of |Δ `shapeGains()`ₙ| ≤ **kMaxGainSlewPerSec × kControlInterval / fs** (kMaxGainSlewPerSec = 375·√P₀; `kControlInterval` = 32 by the D-4 ruling of 2026-10-10, i.e. 0.25·√P₀ at 48 kHz / 32), and every shape and delivered vector has Σaₙ² = P₀ within 1e-4 relative. This is a smoothness bound; (a) and (b) are the pass/fail proof, and the mask is outside (d) (FR-051, SC-021) | `ProfundumCore_NoZipperControlSweeps`, `ProfundumCore_NoZipperPitchBend`, `ProfundumCore_ClickDetectorPositiveControl`, `ProfundumCore_ShapeGainStepCeiling`, `SpectralShapeRecipe_LipschitzBound`. |
| **SC-011** | Low Note Guard measurably works | (a) one octave below `kLowNoteGuardOnsetHz` (16.35 Hz, C0), R_pres is reduced by ≥ **6 dB** relative to the same state at the onset (32.70 Hz) — which, by FR-031, is the guard's own effect — rendered, at `kSawAnchor` and mid grid; (b) at and above the onset the vector equals the onset vector within 1e-6 relative (nothing in the cap taper); (c) **1-cent continuity:** sweeping f₀ over MIDI 0–127 in 1-cent steps at 44.1 / 48 / 96 kHz (crossing the guard onset, every harmonic's cap taper and the clamp ceiling), adjacent vectors differ by ≤ **0.1 dB** in every harmonic whose level is ≥ **−80 dB re h1** in both, and by |Δaₙ| ≤ L_f × (1/1200) for every other harmonic; (d) h1's share of E_total below the onset is never lower than at the onset; (e) **the onset is a constant, not a control:** `static_assert(kLowNoteGuardOnsetHz == 32.70f)`, `Controls` has exactly the six FR-002 fields (a six-element structured binding compiles, a seven-element one does not), no recipe or core entry point takes a guard parameter, and the (a) reduction is measured through the public API alone (the guard is always on) | `ProfundumCore_LowNoteGuardReducesUpperEnergy`, `SpectralShapeRecipe_LowNoteGuardContinuity`, `SpectralShapeRecipe_GuardIsConstant`. |
| **SC-012** | Retrigger phase policy behaves as specified | **`Reset`:** the first output sample after `noteOn` is exactly 0.0 (FR-045), and two renders after `noteOn` from different prior states (different f₀, Controls and elapsed time) are bit-identical. **`FreeRunning`, same f₀ and Controls:** the render with the `noteOn` is bit-identical (in-process) to an uninterrupted render with no `noteOn`. **`FreeRunning`, new f₀ within ±1 semitone** (no bank crossfade): at the boundary sample k₀, \|y[k₀] − y[k₀−1]\| ≤ the maximum \|y[k] − y[k−1]\| over the preceding 100 ms of steady state at the old f₀, and y[k₀] ≠ 0.0 (no reset to phase 0) — tested at `kSawAnchor` and mid grid, C2, with the boundary landing at a waveform sample where the saw anchor's output is ≥ 0.25 of its peak | `ProfundumCore_RetriggerPhasePolicy`. |
| **SC-013** | **CPU budget** | **≤ 1.0 % of one core per voice @ 48 kHz for 64 partials** (roadmap line 240), host block 64, with a D/B/E/S full-range sweep and 5 Hz vibrato active (recipe re-evaluated every interval, FR-050 slew active). The 96-partial figure is recorded for OQ-3 together with an E1 (41.2 Hz) listening render at 64 and at 96 partials; the plan records the chosen shipped default (64 or 96) citing both figures and the listening result. It does not relax the 64-partial gate; 128 partials is out of scope (FR-067). | `ProfundumCore_CpuBudget` `[performance]`, run alone via `node tools/run-cpu-tests.js dsp_systems_tests`; % = wall time / audio time × 100, median of 5 runs. |
| **SC-014** | Mono compatibility and the pan hook | (a) **every render with an all-zero pan-offset vector** in this spec has L == R bit-identically (asserted in each fixture through the shared helper); (b) a non-zero pan-offset vector produces L ≠ R (hook is live); (c) **return to centre:** set a non-zero vector, render ≥ 1 control interval, set all zeros, and every sample from the next control interval on is L == R bit-identically, while the rendered left channel is continuous across the change (no phase or amplitude reset — the bank's MCF state is untouched); (d) bank unit test: the FR-064 method leaves `panLeft_[i] == panRight_[i] ==` the value `reset()` writes, observed as L == R bit-identical output after an `applyPanOffsets` excursion, and changes no other output | `ProfundumCore_ZeroPanBitIdentical`, `ProfundumCore_PanHookForwards`, `ProfundumCore_PanReturnToCentreBitIdentical`, `HarmonicOscillatorBank_RestoreCenterPan`. |
| **SC-015** | RT safety | zero allocations (`AllocationScope`, `allocation_detector.h:111`) across `setControls`, `setFrequency`, `noteOn`, `setPartialPanOffsets` (zero and non-zero, and the return to zero), `processBlock` and recipe evaluation; every audio-thread method `noexcept` (static_assert) | `ProfundumCore_NoAllocationOnAudioThread`, `SpectralShapeRecipe_NoAllocation`. |
| **SC-016** | Robustness | NaN / ±Inf / denormal f₀ and controls (by bit pattern, including elements inside an `f0PerSample` trajectory) leave output finite and `stateFinite()` true; f₀ at 8 Hz and at the clamp ceiling render finite, and at the clamp ceiling the recipe vector is h1-only with a₁² = P₀ within 1e-4 relative; N = 1 and N = 96 work, and `prepare(fs, 128)` behaves as `prepare(fs, 96)` (clamp; `kMaxPartials` is 96) | `ProfundumCore_NonFiniteInputsRejected` (in the `-fno-fast-math` list), `ProfundumCore_PartialCountExtremes`. |
| **SC-017** | Determinism | two instances, same call sequence, bit-identical in-process; sample-rate change via `prepare` (48 → 96 → 44.1 kHz) re-establishes SC-001 and SC-008 at the new rate | `ProfundumCore_Deterministic`, `ProfundumCore_SampleRateChange`. |
| **SC-018** | Output level constant | steady-state rendered RMS per channel = **−12 dBFS ± 0.1 dB** (`kCoreOutputRmsDb`, FR-049), measured over an integer number of f₀ periods ≥ 1 s, at the six named coordinates, the five Body colours and mid grid, at C1, C2, C3 and at 44.1 / 48 / 96 kHz | `ProfundumCore_OutputLevelConstant`. |
| **SC-019** | Body colours reachable and distinct (FR-014) | at C2, rendered: pairwise **Body-colour distance** between the five `kBody*` coordinates ≥ **6 dB** (the SC-004 bar); and the FR-014 extremal orderings hold — `kBodyRound` has the lowest C, `kBodyHollow` the highest odd/even ratio, `kBodyNasal` the smallest σ, `kBodyThick` the highest R_body, each by ≥ 1 dB (ratios) or ≥ 0.05 octave (C, σ) over the runner-up. Recipe and render. | `ProfundumCore_BodyColoursDistinct`. |
| **SC-020** | Pitch trajectory is sample-accurate and block-size independent (FR-043) | **(a)** `processBlock(…, nullptr)` after `setFrequency(f)` is bit-identical to a trajectory filled with `f`. **(b)** The same `f0PerSample` trajectory (a 10 ms log-linear 24-semitone drop, and separately a 5 Hz ±2-semitone vibrato), rendered with host blocks of 512, 64 and 37 samples and no setter calls between blocks, is bit-identical in-process. **(c)** At `kSineAnchor`, a C3 → C1 drop that starts and ends inside one 512-sample host block: the phase of h1 against ∫ `f0PerSample` dt has \|error\| ≤ **0.5·(U/fs)·\|f₀_end − f₀_start\| + 0.02 cycle** at the end of the drop and 100 ms later (U from FR-043; the first term is the sample-and-hold bound, so a coarse U cannot hide behind the second). **(d)** NaN/Inf elements inside a trajectory leave output finite and hold the previous f₀ (FR-062). | `ProfundumCore_PitchTrajectoryNullptr`, `ProfundumCore_PitchTrajectoryBlockSizeIndependent`, `ProfundumCore_PitchTrajectorySampleAccurate`, `ProfundumCore_PitchTrajectoryNonFinite` (the last in the `-fno-fast-math` list). |
| **SC-021** | Guard/cap mask is never slewed; bends bounded by pitch speed (FR-051) | **(a)** Over 10 000 random Controls × f₀ at 44.1 / 48 / 96 kHz, the shape stage composed with the mask stage equals the full recipe evaluation within **1e-6 relative**. **(b)** With controls converged, over the SC-009 bend legs, a ±12-semitone 50 ms bend and a 24-semitone 10 ms drop through the guard onset: max over n and consecutive control intervals of \|Δ `deliveredGains()`ₙ\| ≤ **L_f × (octaves f₀ moved in that interval) + 1e-6**. **(c)** No lag: in every control interval, `deliveredGains()`ₙ is **exactly 0** for every n ≥ 2 with n·f₀ ≥ `kNyquistCapFraction` × fs/2 at the f₀ used for that interval. **(d)** On every `noteOn` the first delivered vector already has zeros at all n ≥ 2 at or above the cap of the new f₀. **(e)** Σaₙ² = P₀ within 1e-4 relative for every delivered vector in these fixtures. | `SpectralShapeRecipe_ShapeMaskComposition`, `ProfundumCore_MaskNeverSlewedBend`, `ProfundumCore_CapTracksPitchExactly`, `ProfundumCore_NoteOnMaskImmediate`. |
| **SC-022** | Body emphasis acts on the whole vector (FR-013, FR-015) | At `bodyEmphasis = −1`, at every point of the notation grid and the mid grid crossed with `edge` ∈ {0, 0.5, 1} and `bodyCurvature` ∈ {0, 0.5, 1}, every even n ≥ 2 is ≤ **−50 dB re h1** — in particular for n ≥ 10 with `edge = 1` (where evens would otherwise reappear above the Body region). Recipe and render at C2. The SC-005 FR-023 floor (odd/even ≥ the neutral value) holds at every emphasis step of the same grid. | `SpectralShapeRecipe_EmphasisActsOnWholeVector`, `ProfundumCore_EmphasisOddOnlyRendered`. |
| **SC-023** | Shared-header footprint (FR-067, FR-064) | A scripted `git diff` against the phase base (run in comply, output quoted) shows: `harmonic_oscillator_bank.h` is purely additive — exactly one new public method, plus at most the `kCenterGain` hoist whose `reset()` output stays bit-identical (SC-014(d)); **zero** diff lines in `harmonic_oscillator_bank_simd.h`/`.cpp`, `harmonic_types.h` and `additive_oscillator.h`; `static_assert(kMaxPartials == 96)` in the core test TU; the Ruinae, Seraphis, Vorago, Innexus and Membrum suites stay green. | `node` diff-check script recorded in comply; `ProfundumCore_KMaxPartialsUntouched`. |

Evaluation (roadmap lines 241–242): static notes across four octaves (C0–C4), pitch bends and automated
sweeps are rendered to WAV by a test-only `[long]` case for listening; the gates above are the pass/fail.

---

## Edge Cases

- **E-1 RT boundaries.** `prepare` / `reset` run on the control thread only; `noteOn` with
  `RetriggerPhase::Reset` calls the bank's `reset()` from the audio thread — allowed, because
  `reset()` is array fills only (bank lines 203–240) and allocation-free (SC-015). `reset()` also
  restores the exact centre pan tables; the core then re-forwards a non-zero pan-offset vector, if set,
  on the next control interval.
- **E-2 Before prepare / zero-length blocks / null buffers.** Silence; no state change (FR-061). The
  bank already returns on null/zero (line 804).
- **E-3 Control extremes.** All clamped (FR-002). `depth = 1` with `edge = 1`: Depth wins the h1/rest
  ratio (FR-010) and Edge only redistributes the small remainder. All envelopes at zero: the base law
  plus normalisation still yields P₀ (h1's pre-normalisation floor, FR-005).
- **E-4 f₀ extremes.** At 8 Hz (clamp floor) the guard is fully engaged and N harmonics reach only
  ≈ 0.5 kHz. At the clamp ceiling (`kNyquistCapFraction` × fs/2) every n ≥ 2 is capped and h1 alone
  carries P₀ (FR-005, FR-032); h1 itself is never capped. MIDI 127 (12.5 kHz) at 44.1 kHz is below the
  0.8 × Nyquist ceiling (17.6 kHz) so it is not clamped.
- **E-5 Pitch bends across the cap and the guard onset.** Both are continuous in f₀ (FR-006) so a bend
  fades partials rather than switching them; SC-009's bend case and SC-011's 1-cent continuity check
  cover it. Because the cap and the guard are the never-slewed mask (FR-051), no partial lags into the cap taper
  during a bend; delivered gains may change faster than FR-050's ceiling, bounded by L_f × pitch speed
  (SC-021), and the bank's fade remains a second guard.- **E-6 Sample-rate change.** `prepare` at the new rate recomputes the bank's coefficients (line 129),
  the recipe's cap, the control interval and FR-050's per-interval ceiling; output resumes from the
  FR-045 phases. Rates 22.05–192 kHz.
- **E-7 Large pitch jumps.** A `FreeRunning` `noteOn` more than 1 semitone away triggers the bank's
  3 ms crossfade from a held level (lines 260–268, 781–787). Phase 1 asserts only boundedness and
  finiteness; making note transitions click-free is Phase 4's. The core never triggers that crossfade
  from its own pitch smoothing (FR-043). Partials that the jump puts above the cap are zeroed at once
  by the never-slewed mask (FR-051(d), SC-021(d)).- **E-8 Reset while sounding.** `Reset` zeroes `currentAmplitude_` (bank line 207): the new note ramps
  in over ≈ 2 ms from silence, and the old note is cut. Phase 4 owns the click-free version.
- **E-9 Partial-count reduction.** If the cap shrinks the effective count, the core keeps `numPartials`
  frame entries and zeroes gains instead of shrinking `numPartials`, so the bank's tail-fade logic
  (lines 745–778) is never relied on for correctness.
- **E-10 Seed determinism.** Not applicable — no RNG (FR-063). The bank's LCG (line 992) only runs for
  partials with bandwidth, which the core never sets (FR-044).
- **E-11 Denormals.** Amplitudes decaying toward zero in the bank's one-pole can go denormal; tests run
  with FTZ/DAZ (`tests/test_helpers/enable_ftz_daz.h`), and the plan must state whether the core flushes
  sub-threshold gains to zero.
- **E-12 Fast-math.** All finiteness checks use `detail::isFinite`; NaN-injection tests sit in the
  `-fno-fast-math` list (FR-070), the perf TU does not.
- **E-13 Pan excursion and return.** A non-zero → zero pan-offset transition restores the exact centre
  tables (FR-048(c)); a zero → non-zero transition forwards the vector on the next control interval. The
  restore does not touch phases or amplitudes, so it is a pan change only.

---

## Decisions taken where the roadmap is silent (ratified by the 2026-10-09 clarify session)

Every value below is also written into the FR/SC body it governs; this list records the rationale for
clarify, it is not the only home of any value.

- **D-1 Body emphasis = odd-side parity bias, range [−1, 0].** The roadmap requires the odd-harmonic
  1/n² triangle as a reachable anchor (line 219) and gives Body "adjustable curvature and emphasis"
  (line 208). A smooth envelope in log n cannot zero the even harmonics, so the triangle is unreachable
  without a parity control; this spec reads "emphasis" as that odd-side bias (FR-013). The even-favoured
  side is **excluded** because the roadmap gives even harmonics to Phase 2's nonlinearity (lines
  221–222, 254–256, 267); "thick" comes from Body amount / curvature / Shift (FR-014). Alternative for
  clarify: an explicit separate parity control outside Body. Confirm the [−1, 0] boundary.
- **D-2 Loudness = unweighted power.** "Loudness normalised" (line 122) and "loudness flat within
  ±0.5 dB" (line 234) are implemented and measured as unweighted RMS (Σaₙ² = P₀). A perceptual weighting
  (e.g. K-weighting) would make a pure sub at C1 several dB louder than a saw of equal RMS; the
  unweighted choice is deterministic and pitch-independent. Confirm.
- **D-3 Start phases = sine phase 0 for all harmonics.** "Deterministic phases" (line 122) does not say
  which. All-zero sine phases make the output start at exactly 0 (clean onset after `Reset`) and make
  `kSawAnchor` a true sawtooth. Crest factor at the saw anchor is ≈ +6 dB, which Phase 2's drive
  calibration will see.
- **D-4 Output level** (FR-049, SC-018): −12 dBFS RMS per channel at zero pan offsets (≈ −6 dBFS peak
  at the saw anchor), leaving headroom for Phase 2's nonlinear stages. Fixes P₀ ≈ 0.2524 (FR-005).
- **D-5 Low Note Guard** (FR-030, SC-011): onset 32.70 Hz (C1), **fixed taper, no strength control**.
  C1 keeps SC-008's C1 → C3 invariance exact while C0 is guarded. The roadmap says only "a documented
  taper" (lines 225–226); the concept files the guard under Note Behaviour (line 591), which `MorphState`
  excludes (roadmap line 425), so it is not in `Controls`. Exposing a strength control is Phase 11's call
  and would then be a recipe argument outside `Controls`.
- **D-6 Descriptor regions in harmonic order** (Sub = h1, Body = h2–8, Presence = h9–N). Phase 12's
  energy view may present them in Hz; Phase 1's pitch-invariant gates need harmonic order.
- **D-7 Audibility bars.** 6 dB (ratios), 1 octave (centroid), 6 dB pairwise distance, 0.25 octave
  (spread), 20 dB (odd/even), 6 dB Body-colour distance — SC-004 / SC-005 / SC-019. The roadmap says
  "monotonically and distinguishably" without a figure.
- **D-8 Anchor tolerances.** ±1.5 dB / ±3 dB / −50 dB / 5 % L2 / −60 dB (SC-006). The roadmap says
  "within a ruled tolerance" (line 220) without a figure.
- **D-9 Zipper definition** (FR-006, FR-050, FR-051, SC-010). "Per-block amplitude delta bounded" (line 239)
  is realised as a **numeric** shape-vector ceiling, kMaxGainSlewPerSec = 375·√P₀/s (0.5·√P₀ per 64-sample
  interval at 48 kHz, full scale in about 2.7 ms), measured on `shapeGains()`; plus recipe Lipschitz ceilings
  L_c ≤ 8·√P₀ per control unit and L_f ≤ 8·√P₀ per octave (the guard/cap mask follows pitch at L_f, SC-021);
  plus a click detector with a positive control; plus a −60 dB control-rate sideband bound. Confirmed with the
  ceiling re-set from 93.75·√P₀/s (10.7 ms) to 375·√P₀/s so Motion-ENV and pluck attacks are not smeared.
- **D-10 Per-partial phase offsets / decorrelation deferred to Phase 7 (ruled, option (a), 2026-10-09)**
  (FR-048(d), FR-067). Key Design Decision 2 names "per-partial pan/phase offsets" (line 127) and Phase 7's
  SPREAD consumes a "pan/decorrelation vector for the core bank (Phase 1 hook)" (line 394); Phase 1 ships
  pan offsets only, at identity. The phase half needs an append-only bank/SIMD extension (the bank keeps one
  sin/cos MCF state per partial for both channels, `processMcfBatchSIMD`,
  `harmonic_oscillator_bank_simd.h:33–46`) with its own CPU cost; Phase 7 owns that extension and its CPU
  against its own budget, and Phase 1 makes no such change (SC-023).
- **D-11 Analysis windows** (SC-001, SC-009, SC-010(b)). Off-harmonic energy sums use a test-local
  `double` 7-term Blackman–Harris (≈ −180 dB sidelobes) because `magnitudeSpectrum`'s Hann (≈ −31 dB,
  18 dB/oct) and the library's 4-term Blackman–Harris (≈ −92 dB, `window_functions.h:177`) leak far
  above −96 dBFS for non-bin-centred harmonics. FFT lengths are scaled so the bin width is ≤ f₀/64 and
  a non-vacuity guard fails any frame whose exclusion zones exceed 50 %.

## Resolved Open Questions

- **OQ-3 Partial count — 64 vs 96 (resolved 2026-10-09).** Fixed at `prepare`, default 64, ceiling 96 (the
  bank's existing maximum), per FR-041. The plan chooses 64 vs 96 from SC-013's 64- and 96-partial CPU
  figures plus an E1 (41.2 Hz; 64 partials reach 2.64 kHz, 96 reach 3.96 kHz) listening render. **128 is out of
  scope:** `kMaxPartials = 96` (`harmonic_types.h:21`) also sizes `HarmonicFrame::partials` (line 57) and is
  used by Innexus (`plugins/innexus/src/dsp/harmonic_blender.h:135`) and Membrum (`mode_inject.h`); it is not
  touched (FR-067, SC-023).
## Traceability

| Roadmap statement (line) | FR | SC |
|---|---|---|
| Recipe: pure math aₙ = Aₙ(D,B,E,S) (203–204) | FR-001–FR-006 | SC-003, SC-010(c) |
| Depth (205–206) | FR-010, FR-018 | SC-002, SC-003 |
| Body, curvature, emphasis; round ↔ hollow ↔ woody ↔ nasal ↔ thick (207–209) | FR-011–FR-014 | SC-004, SC-005, SC-019 |
| Edge (210–211) | FR-015 | SC-004 |
| Spectral Shift; Heavy/Hollow/Growl (212–217) | FR-016, FR-022 | SC-004, SC-007 |
| Waveform progression anchors; even harmonics are Phase 2's (218–222) | FR-013, FR-018, FR-020, FR-021, FR-023 | SC-005, SC-006, SC-022 |
| Overlapping envelopes, no hard boundaries (223–224, 121–123) | FR-017, FR-006 | SC-010(c), SC-011 |
| Low Note Guard + Nyquist cap (225–226) | FR-030, FR-032, FR-051, FR-062 | SC-009, SC-011, SC-016, SC-021 |
| Core → bank, 64 partials, prepare-time count (227–228) | FR-040, FR-041, FR-067 | SC-013, SC-016, SC-023 |
| Per-block evaluation, per-sample smoothing (229) | FR-042, FR-043, FR-050, FR-051 | SC-010, SC-020, SC-021 |
| Deterministic phases, retrigger policy input (229–230) | FR-045, FR-046 | SC-012, SC-017 |
| Per-partial pan/offset hook, identity (230; 127, 394) | FR-048, FR-064, FR-067, D-10 | SC-014, SC-023 |
| Frequency accuracy < 0.1 cent (233) | FR-044 | SC-001 |
| Loudness normalised / Depth not a volume knob (122, 205, 234) | FR-005, FR-049 | SC-003, SC-018 |
| Spectrum invariant C1 → C3 (236) | FR-031 | SC-008 |
| No zipper, per-block amplitude delta bounded (238–239) | FR-006, FR-050, FR-051 | SC-010, SC-021 |
| Energy above Nyquist ≤ −96 dBFS (239) | FR-032, FR-051 | SC-009, SC-021 |
| CPU ≤ 1 % per voice, 64 partials (240) | FR-074 | SC-013 |
| Cross-cutting: RT, audibility, mono, anti-aliasing, layers, ODR, goldens, portability, shared headers (53, 578–598) | FR-060–FR-066, FR-070–FR-073 | SC-014–SC-017, SC-019 |

## Assumptions

- The bank's anti-alias fade and MCF orbit correction (lines 1061–1088) are flat (gain 1 × cos(πf/fs)
  compensating the MCF's 1/cos(πf/fs) orbit) for every harmonic below the recipe's cap and for h1 at the
  clamp ceiling; SC-008 and SC-018 measure this rather than assuming it.
- Phase 4 will call `noteOn` and `setFrequency`; Phase 7 will call `setPartialPanOffsets` (and owns the
  D-10 phase half, as ruled by the clarify stage); Phase 8 will interpolate
  `SpectralShapeRecipe::Controls`; Phase 6's Mass law will bias `depth` and `body`; Phase 3 builds its
  own h1 lock. None of them requires more from Phase 1 than the contracts above.

---

## Review notes (2026-10-09 challenge round)

No issue was rejected. Where the review offered alternatives, the choice taken:

- **Pan/phase hook (fidelity, major):** option (b) — explicit deferral D-10 with the shared-header impact
  named, so clarify can rule; option (a) remains available there.
- **Zipper bound (fidelity, major):** numeric ceilings in FR-006 (L_c, L_f) and FR-050 (delivered-gain
  slew), measured by SC-010(d).
- **Body emphasis (fidelity, major):** restricted to [−1, 0]; SC-005's 20 dB span restated over that
  range; FR-023 floor asserted.
- **lowNoteGuard (fidelity, minor):** removed from `Controls`; the guard is a fixed taper; SC-011 compares
  below-onset against the onset, which FR-031 makes equivalent to guard-on vs guard-off.
- **h1 phase readout (fidelity minor + testability major):** FR-047 withdrawn rather than extended — an
  h1 phase accessor needs a bank change whose shape is Phase 3's lock decision (roadmap line 284), and
  `noteOn` is defined at block offset 0 by contract (FR-046).
- **SC-009 (testability, blocker):** BH7 window in `double`, FFT length rule (bin ≤ f₀/64), ±8-bin
  exclusion, Parseval normalisation, 50 % non-vacuity guard; the continuous 2 s glide is replaced by
  10 ms one-semitone legs analysed at each held pitch immediately after the leg, which keeps the
  fast-motion risk (slew/smoothing lag across the cap) inside the measured frame while every frame has
  a fixed n·f₀ set.
- **h1 and the cap (testability, major):** h1 exempt from cap and taper (FR-032); SC-003 / SC-010(c) /
  SC-011(c) / SC-016 sample up to the clamp ceiling.
- All other issues (SC-001 fixture, SC-010(a)/(b), SC-011 floor, SC-012 operational FreeRunning, SC-014
  wording, grid definition, D-4/D-5 values into the body, Body colours gate, pan return-to-centre)
  applied as suggested.
