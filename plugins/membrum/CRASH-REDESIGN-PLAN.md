# Crash Cymbal Redesign Plan

**Status:** PLANNED — not yet implemented
**Scope:** Membrum crash/splash (NoiseBody-based cymbals). Rides (Bell body) get only the Phase 4 NonlinearCoupling change plus preset polish.
**Author context:** Produced from a source audit of the current crash path plus online research into cymbal acoustics and synthesis (sources at the bottom). Written so a model can implement it phase-by-phase without re-deriving the analysis.

---

## 1. Problem statement

Membrum's crash presets do not sound like a crash cymbal. They sound like a short, clangy,
gong-ish chime with a synthetic drone under it. This plan explains *why* (six concrete,
verified defects), what the physics says a crash must do, and a phased implementation
that fixes the defects inside Membrum's existing architecture (no new PDE solver, no
new body type).

## 2. What a real crash does (research summary)

A crash cymbal is a thin free-edge bronze plate driven hard into a **strongly nonlinear
(wave-turbulence) regime**. The perceptually load-bearing features, in priority order:

| # | Feature | Measured behaviour |
|---|---------|--------------------|
| T1 | **Noise-like onset** | The first tens of ms of a hard crash are essentially broadband noise — chaotic shell vibration, not resolved modes (Stowell; Fletcher/Rossing). |
| T2 | **Bloom (energy cascade)** | Energy cascades from the initially-excited low modes **up** into high frequencies over ~20–200 ms after the strike. Brightness *rises after the hit*, peaks, then falls. Classic synthesis emulates this with a high-pass/low-pass filter that *opens over ~200 ms* (Sound on Sound), or with an explicit inter-mode energy-transfer matrix (Poirot et al. 2024). |
| T3 | **Dense inharmonic spectrum** | ~100 resolvable modes in a crash, >300 in a 16″ cymbal; above ~1 kHz the modes are an unresolved cluster that reads as colored noise. Practical syntheses use ≥100 resonators (Stowell: 100 filters over 300 Hz–20 kHz) or thousands (Skare & Abel 2019: >2000 modes/cymbal on GPU). 32 modes is bell/gong territory, not cymbal territory. |
| T4 | **Frequency-dependent decay** | Exponential decay rates run from **~50 ms at HF to ~8000 ms at LF** (FEM/experimental crash studies). The sound *darkens* through the tail. Low modes ring for seconds. |
| T5 | **Long wash** | A crash is audible 2–5 s. The mid/high "wash" (the actual *crash* part) persists 1.5–3 s, not 300 ms. |
| T6 | **No pitch** | No definite pitch, no harmonic series, no pitch glide. Any sustained harmonic stack destroys the illusion instantly. |
| T7 | **Velocity morphs regime** | Soft tap → sparse bell-like modes; hard hit → chaotic broadband + stronger/faster bloom (Stowell "tap = bell / hard = broadband"). |

The TR-808 lesson (multi-band architecture): a convincing cymbal needs **at least two
spectral bands with different envelopes** — a mid "ping" that decays fast and a high
"sizzle" band whose relative level *increases* as the sound progresses.

## 3. What the current implementation does — verified defects

Current crash = NoiseBurst exciter → NoiseBody (32-mode plate bank + internal noise) →
parallel noise layer → ModeInject → NonlinearCoupling → ToneShaper.
Preset values from `tools/membrum_preset_generator.cpp` pad 13 of the Acoustic kit
(Mat 0.93 · Size 0.35 (f0 ≈ 670 Hz) · Decay 0.70 · b1 0.30 · b3 0.0 · ModeInject 0.25 ·
DecaySkew 0.58 · NLC 0.35 · Noise 0.50/cut 0.85/dec 0.60).

| # | Defect | Evidence | Violates |
|---|--------|----------|----------|
| D1 | **Wash dies in <0.35 s.** NoiseBody's internal noise decay is `60ms × exp(lerp(ln0.3, ln3, decay))` → ≈ **90 ms** at Decay 0.70 (`noise_body_mapper.h:145-146`). The parallel noise layer at norm 0.60 → ≈ **317 ms** (`noise_layer.h:217-225`). After that only the 32-partial chime + drone remain. This is the single most audible defect. | `plugins/membrum/src/dsp/bodies/noise_body_mapper.h:144-146`, `plugins/membrum/src/dsp/noise_layer.h:217-225` | T5 |
| D2 | **No bloom mechanism exists.** NonlinearCoupling drives a waveshaper from an RMS follower with **5 ms attack** (`nonlinear_coupling.h:58`), so brightening is maximal at onset and decays monotonically — the exact *opposite* trajectory of the cascade. Waveshaping a sparse modal sum also just adds odd harmonics of existing partials (sounds like distortion, not turbulence). Nothing anywhere delays HF onset. | `plugins/membrum/src/dsp/unnatural/nonlinear_coupling.h:56-59,88-107` | T2 |
| D3 | **Flat, too-fast modal decay.** Preset b1 norm 0.30 → b1 = 0.2 + 0.30×49.8 = **15.1 s⁻¹** → T60 = 6.908/15.1 ≈ **0.46 s for every mode equally** (b3 = 0). Real crash: T60 3–8 s at LF, ~0.05–0.5 s at HF. No spectral evolution during the ring; everything gates off together at half a second. | `plugins/membrum/src/dsp/bodies/membrane_mapper.h:71-78`, `dsp/.../modal_resonator_bank.h:286` (`decayRate_k = b1 + b3·f²`) | T4, T5 |
| D4 | **Sustained pitched drone.** ModeInject 0.25 injects an 8-partial **integer-harmonic** series (1/k amplitudes ≈ sawtooth spectrum) at f0 = 670 Hz, with the documented "rings undamped (flat plateau)" behaviour. A crash has *no pitch*; this reads as a synth organ tone under the cymbal. | `plugins/membrum/src/dsp/unnatural/mode_inject.h:34-66`, preset `tools/membrum_preset_generator.cpp:764` | T6 |
| D5 | **DecaySkew 0.58 cuts the shimmer by up to −18 dB.** The per-mode tilt multiplies amplitudes by `ratio^(-decaySkew)` clamped to 8× (`noise_body_mapper.h:119-129`, `membrane_mapper.h:51`); with plate ratios up to 29.4 the top modes lose ~7–8× amplitude. The crash's high cluster — the part that makes it a cymbal — is pre-muted. | `plugins/membrum/src/dsp/bodies/noise_body_mapper.h:119-129`, preset `:767` | T3 |
| D6 | **Only 32 sparse modes.** `kNoiseBodyModeCount = 32` spanning 670 Hz–19.7 kHz → mean spacing ~600 Hz; each partial individually resolvable → inharmonic *bell*, not dense *wash*. (Bank supports 96; the ratio table has 48 entries.) | `plugins/membrum/src/dsp/bodies/noise_body_mapper.h:64`, `plate_modes.h:51` | T3 |
| D7 | *(minor)* **Narrowband strike.** NoiseBurst is violet noise through a bandpass at Q = 2.0 whose center rides velocity to ~9.6 kHz (`noise_burst_exciter.h:57-58,104`), so on hard hits the low plate modes are underexcited (>20 dB down at f0) and the onset is a hiss-tick rather than a broadband splash. Shared with snare/hats/clap, so treat with care. | `plugins/membrum/src/dsp/exciters/noise_burst_exciter.h:53-59,101-105` | T1, T7 |

**Net result:** ~10 ms narrow hiss tick + 0.46 s uniform-decay 32-partial chime (with its
top octaves pre-cut) + ≤0.3 s of noise + an undamped 670 Hz harmonic drone + onset-heavy
waveshaper distortion. That is a synth gong, not a crash.

## 4. Design overview

Fix inside the existing architecture, perceptual-model style (Poirot's insight: you don't
need the von Kármán PDE — you need the *audible signatures*: long HF wash, delayed HF
onset, frequency-dependent decay, density, no pitch):

1. **Wash** — make NoiseBody's internal noise layer the long "sizzle" band: decay tied to
   the effective lowest-mode T60, lowpass (not resonant bandpass), seconds not ms.
2. **Bloom** — give the wash a **cutoff envelope** (LP cutoff starts mid, opens to ~10 kHz
   over 30–150 ms — faster/wider with velocity — then falls through the tail). Depth is
   driven by the existing NonlinearCoupling pad parameter routed into VoiceCommonParams,
   so pads with NLC = 0 (hats, toms, everything else) are *bit-identical*.
3. **Delayed nonlinear brightening** — slow the NonlinearCoupling follower attack
   (velocity-dependent 30–120 ms) so the waveshaper rides *up* after the hit.
4. **Frequency-dependent damping** — preset-side: b1 ≈ 2 s⁻¹ (T60 ≈ 3.5 s at the bottom)
   plus a small b3 so 15 kHz modes die in ~0.5 s. The plumbing already exists (D3 is a
   preset bug, not a code bug).
5. **Density** — NoiseBody 32 → 64 modes; extend the Chladni table 48 → 96 entries with
   a committed generator script. CPU-gated.
6. **De-pitching** — ModeInject → 0 on all cymbal pads; DecaySkew → ~0.1.

Phases are ordered so the *preset-only* fixes land first (biggest win, zero code risk),
then code changes one component at a time, each with failing-test-first discipline.

**Branch:** create `feature/membrum-crash-redesign` off `main` before any work (never
implement on main).

---

## Phase 0 — Baseline captures + failing acceptance tests

**Goal:** Objective before/after evidence and executable acceptance criteria.

1. Build Release; render the current Acoustic-kit crash (pad 13, MIDI note 49) at
   velocities 40, 90, 127 with `krate-render` (see `tools/krate-render/`); save WAVs to
   `plugins/membrum/preset-retune/renders/crash-baseline/`. Also render: splash (note 55),
   closed hat (42), open hat (46), snare (38) — these are the collateral-damage watchlist.
2. Add a new test file `plugins/membrum/tests/unit/dsp/test_crash_acceptance.cpp` that
   configures a DrumVoice exactly like the crash preset (NoiseBurst + NoiseBody + the pad-13
   values above, but with the *target* values from Phase 1 once it lands) and renders 4 s
   at 48 kHz, velocity 1.0. Implement these metrics in the test (FFT helpers exist — see
   the `testing-dsp-analysis` skill and existing spectral tests for patterns):
   - `bandT60(loHz, hiHz)`: fit exponential decay of band RMS (50 ms hops).
   - `hfRatio(t)`: energy(6–16 kHz)/energy(total) in a 50 ms window at time t.
   - `pitchSalience(t)`: max normalized autocorrelation peak in 100 ms window at t.
3. Encode the acceptance criteria (all `SECTION`s, initially FAILING — that is the point):
   - **AC-1 (wash length):** RMS in the 1.4–1.6 s window ≥ −50 dB relative to overall peak
     (currently fails: everything but the drone is gone by ~1.2 s).
   - **AC-2 (bloom):** `hfRatio(t)` reaches its maximum at some t in [20 ms, 300 ms] — i.e.
     HF fraction *rises* after onset. Exclude the first 10 ms (click transient).
   - **AC-3 (frequency-dependent decay):** T60(500–1500 Hz) ≥ 2.0 s; T60(8–16 kHz) within
     [0.3, 1.2] s; and T60 non-increasing across bands 0.5–1.5 k / 2–4 k / 8–16 k.
   - **AC-4 (no pitch):** `pitchSalience(t = 0.8 s)` < 0.4 (drone gone).
   - **AC-5 (velocity regime):** render at velocity 0.3: `hfRatio` peak ≤ 0.75× the
     velocity-1.0 peak (soft hit = darker, less bloom).
4. Commit ("test(membrum): failing crash-cymbal acceptance tests + baseline renders").
   The suite must be wired so these tests are **expected-failing** until later phases —
   use a `[!shouldfail]` Catch2 tag initially, then remove the tag in the phase that makes
   each criterion pass (AC-4/AC-1 partly pass after Phase 1; flip tags per-phase, never
   leave a silently-green shouldfail).

## Phase 1 — Preset-only fixes (de-pitch + damping law) — no code changes

**Goal:** Kill D3, D4, D5 for every crash/splash pad in every kit. This alone transforms
the sound from "0.46 s clangy gong + drone" to "3 s darkening inharmonic ring".

Edit `tools/membrum_preset_generator.cpp`. For **every NoiseBody cymbal pad** (crash 1,
crash 2, splash, china, suspended cymbal — search for `BodyModelType::NoiseBody` pads
with `outputBus = 1` and the crash/splash comments; do NOT touch hats/tambourine/shaker):

| Field | Old (crash 1) | New | Why |
|---|---|---|---|
| `modeInjectAmount` | 0.25 | **0.0** | D4 — no harmonic drone on an unpitched instrument |
| `decaySkew` | 0.58 | **0.10** | D5 — stop −18 dB tilt against the HF cluster (keep a whisper of LF emphasis) |
| `bodyDampingB1` | 0.30 | **0.036** | b1 = 0.2 + 0.036×49.8 ≈ 2.0 s⁻¹ → lowest-mode T60 ≈ 3.5 s |
| `bodyDampingB3` | 0.0 | **5.2e-5** | b3 = 5.2e-8 s → decayRate(15 kHz) ≈ 2 + 11.7 ≈ 13.7 s⁻¹ → T60 ≈ 0.5 s; midband (3 kHz) T60 ≈ 2.8 s |
| `noiseLayerDecay` | 0.60 | **0.95** | parallel sizzle ≈ 1.6 s (denorm cap is 2 s) |
| `noiseLayerColor` | 0.79 | keep per-kit (White↔Violet) | |
| `nonlinearCoupling` | 0.35 | keep 0.35 for now (Phase 3/4 use it) | |

Splash: same shape but shorter — `bodyDampingB1` ≈ 0.012 (b1 ≈ 0.8) is *wrong* direction
for a splash; use b1 norm ≈ 0.06 (b1 ≈ 3.2, T60 ≈ 2.2 s) and b3 norm ≈ 8e-5 (faster HF),
`noiseLayerDecay` 0.80. Scale the two crash variants per kit the way the existing per-kit
variations do (dark crash: slightly lower b3, White noise).

Numbers derivation, for checking: `decayRate_k = b1 + b3_seconds·f_k²` with
`b3_seconds = norm × 1e-3` (`membrane_mapper.h:75-78`), `T60 = 6.908 / decayRate`.

Steps:
1. Update every kit's crash/splash pads in the generator (grep `Crash`, `Splash`, `China`,
   `Suspended` — ~20 sites, list in the commit message).
2. Update the matching archetype docs (`preset-retune/archetypes/crash-cymbal-cymbal.md`,
   `splash-...md`) and kit docs (`preset-retune/kits/*.md`) so docs match generator —
   they claim "ModeInject 0.25 harmonic fill" and "b3 = 0 metallic"; both claims are now
   known-wrong (b3=0 means *no* frequency-dependent decay, which is anti-metallic in the
   large; the metallic character comes from the inharmonic ratios + long HF-capable b1).
3. Rebuild the generator target, regenerate presets, install to
   `C:\ProgramData\Krate Audio\Membrum\Kits\{category}\` via the build target (never
   hand-copy).
4. Run `membrum_tests` (factory round-trip + infinite-ring must stay green — removing
   modeInject only helps).
5. Re-render Phase-0 set; A/B against baseline. Flip AC-3/AC-4 `shouldfail` tags off.
   AC-1 should now be close (modal ring carries it); AC-2 still fails (no bloom yet).
6. Commit.

## Phase 2 — Wash: NoiseBody internal noise becomes the long sizzle band

**Goal:** Fix D1 in code. The wash must live as long as the body ring.

File: `plugins/membrum/src/dsp/bodies/noise_body_mapper.h`.

1. **Write the failing test first** (`plugins/membrum/tests/unit/bodies/test_noise_body.cpp`,
   new sections): map with crash-like params (decay 0.7, b1 norm 0.036, b3 norm 5.2e-5,
   size 0.35) → expect `noiseDecayMs` in [1500, 2600]. Map with closed-hat params
   (decay 0.05, b1 norm 0.50) → expect `noiseDecayMs` ≤ 80. Build, watch it fail.
2. Replace the fixed formula (lines ~144-146) with a T60-coupled one:
   ```cpp
   // Wash decay rides the effective lowest-mode T60 so the noise bed and the
   // modal skeleton die together (T4/T5: the wash IS the crash; a 90 ms noise
   // bed under a 3.5 s ring reads as a gong, not a cymbal). decay scales the
   // fraction so short-Decay presets (closed hats) keep a tight bed.
   const float rateLow  = out.modal.damping.b1
                        + out.modal.damping.b3 * f0 * f0;   // s^-1
   const float t60LowMs = 6.9078f / std::max(rateLow, 0.2f) * 1000.0f;
   out.noiseDecayMs = std::clamp(
       t60LowMs * lerp(0.15f, 0.65f, params.decay), 20.0f, 3000.0f);
   ```
   (With crash numbers: 3454 ms × lerp(...,0.7) = 3454 × 0.5 ≈ 1730 ms ✓. Closed hat
   b1 norm 0.50 → rate 25.1 → t60 275 ms × 0.175 ≈ 48 ms ✓.)
   Note `out.modal.damping` is already populated before this point — move the noise-layer
   block after the `dampingLawFromParams` call if needed.
3. **Watchlist:** this changes every NoiseBody pad (hats, tambourine, shaker, clap-like).
   Re-render the Phase-0 watchlist; closed-hat and open-hat gross duration must stay within
   ±25% of baseline (open hat's decay 0.55/b1 0.30 → t60 460 ms × 0.425 ≈ 196 ms vs old
   ~154 ms — acceptable). If any pad regresses audibly, adjust that pad's preset, not the
   formula.
4. Also raise the internal wash **level** balance: `out.noiseMix` 0.4 → `lerp(0.35f, 0.55f,
   params.material)` — brighter/more-metallic pads get more wash, and matching test.
5. Build, zero warnings, `membrum_tests`, update goldens the tests own, commit.

## Phase 3 — Bloom: cutoff envelope on the wash filter

**Goal:** Fix D2's spectral half: HF onset must be *delayed* (AC-2).

Files: `plugins/membrum/src/dsp/noise_layer.h`,
`plugins/membrum/src/dsp/bodies/noise_body.h`, `noise_body_mapper.h`,
`plugins/membrum/src/dsp/voice_common_params.h`, `plugins/membrum/src/dsp/drum_voice.h`.

1. **Route the knob:** add `float nonlinearCoupling = 0.0f;` to `VoiceCommonParams`
   (precedent: `modeStretch`/`decaySkew` are routed the same way). In `DrumVoice`, where
   VoiceCommonParams is filled for the body mappers (grep `modeScatter =` in
   `drum_voice.h`), copy the UnnaturalZone coupling amount in.
2. **NoiseLayer bloom support** (default OFF = bit-identical):
   ```cpp
   /// Optional cutoff envelope ("bloom"): cutoff starts at startHz, rises
   /// exponentially to peakHz with time-constant riseMs, then falls to endHz
   /// with time-constant fallMs. Emulates the cymbal wave-turbulence cascade
   /// (delayed HF onset) at filter level. configureRaw()/configure() disable it.
   void configureBloom(float startHz, float peakHz, float endHz,
                       float riseMs, float fallMs) noexcept;
   ```
   Implementation: keep two one-pole ramp states; update the SVF cutoff every
   `kBloomUpdateInterval = 16` samples inside `processSample`/`processBlock` (a per-sample
   `setCutoff` is needlessly expensive; 16-sample control rate at 48 kHz = 0.33 ms, far
   below audibility for a 30+ ms sweep). Rise phase: `fc += (peak-fc)*aRise` until within
   5% of peak, then fall phase toward `endHz`. `trigger()` resets `fc = startHz`.
   When bloom is not configured (flag false) the code path must be *exactly* today's —
   guard everything behind `if (bloomActive_)`.
3. **NoiseBody wiring:** in `NoiseBody::configureForNoteOn`, after `configureRaw`, switch
   the internal wash filter to Lowpass (`noiseLayer_.setFilterMode(SVFMode::Lowpass)` —
   set once in `prepare`, mirroring what DrumVoice already does for the parallel layer at
   `drum_voice.h:147`) and call `configureBloom` when the mapper says so.
4. **Mapper:** extend `NoiseBodyMapper::Result` with bloom fields; map:
   ```cpp
   // T2: energy-cascade bloom. Depth/speed ride NonlinearCoupling and velocity.
   // NLC == 0 (hats, toms) => bloom disabled entirely (bit-identical path).
   out.bloomEnabled = params.nonlinearCoupling > 0.0f;
   out.bloomStartHz = out.noiseFilterCutoffHz * 0.35f;              // dark at contact
   out.bloomPeakHz  = std::clamp(out.noiseFilterCutoffHz
                        * (1.0f + 1.2f * params.nonlinearCoupling), 2000.0f, 16000.0f);
   out.bloomEndHz   = out.noiseFilterCutoffHz * 0.55f;              // darkening tail
   out.bloomFallMs  = 0.6f * out.noiseDecayMs;
   ```
   `riseMs` needs velocity, which the mapper doesn't get — pass it through
   `configureForNoteOn` (it already receives nothing velocity-shaped; add a `float velocity`
   param — `DrumVoice` has it at noteOn) and compute
   `riseMs = lerp(140.0f, 35.0f, velocity)` (hard hit = faster, research T2/T7).
5. **Tests first:** unit test on NoiseLayer alone — configure bloom 2 kHz→10 kHz→4 kHz,
   rise 50 ms; feed it 1 s; spectral centroid of the 0–30 ms window < centroid of the
   80–130 ms window; centroid of the last 200 ms < the 80–130 ms window. Plus a
   bit-identity test: no `configureBloom` call → output identical to a pre-change golden
   render (or to `configure()`-only reference).
6. Flip AC-2/AC-5 shouldfail off. Build, tests, re-render watchlist (hats must be
   bit-identical — NLC is 0 there; **verify**, since closed hats currently have
   `nonlinearCoupling` 0 in the generator — if any hat has NLC > 0, exclude it by keeping
   its preset at 0). Commit.

## Phase 4 — Delayed nonlinear brightening (NonlinearCoupling attack)

**Goal:** Fix D2's waveshaper half; also improves rides/snare bloom coherence.

File: `plugins/membrum/src/dsp/unnatural/nonlinear_coupling.h`.

1. Failing test first (`plugins/membrum/tests/unit/dsp/` — there are existing NLC tests;
   extend): feed a decaying 200 Hz burst; measure the added-harmonic energy
   (output − input residual) in 20 ms windows; its peak must occur ≥ 25 ms after input
   peak at velocity 1.0, and later at velocity 0.3.
2. Change the follower attack from fixed 5 ms to velocity-dependent slow attack set in
   `setVelocity()` (or a new `noteOn(velocity)` — but `setVelocity` is already called at
   every noteOn from `drum_voice.h:322`):
   ```cpp
   void setVelocity(float velocity) noexcept
   {
       velocity_ = velocity;
       // T2: the cascade takes time. Slow, velocity-dependent attack makes the
       // waveshaper drive RISE over 30-120 ms after the hit instead of spiking
       // at onset (which was the exact inverse of the physical bloom).
       envFollower_.setAttackTime(120.0f - 90.0f * velocity_);
       envFollower_.setReleaseTime(200.0f);
   }
   ```
   Release 50 → 200 ms so brightening rides the early decay rather than fluttering.
   `amount_ == 0` bypass stays bit-exact (unchanged code path).
3. **Watchlist:** every preset with NLC > 0 changes character: crashes, splash, rides,
   ride-bell, snare (per kit docs). Re-render snare + rides, A/B. The snare's NLC is an
   attack-crack enhancer; if the delayed attack audibly softens the snare crack, set the
   snare's `nonlinearCoupling` to 0 and compensate with `clickLayer`/`noiseLayer` preset
   nudges in the generator — decide by ear from the renders, document the choice.
4. Build, tests, commit.

## Phase 5 — Modal density: 32 → 64 modes (CPU-gated)

**Goal:** Fix D6. Halve the mode spacing so the upper cluster fuses into wash.

1. **Table extension:** write `tools/gen-plate-chladni.js` (Node.js — repo rule; the
   comment in `plate_modes.h:38-40` references such a generator but it was never
   committed). It must: enumerate (m, n) modes, exclude (0,0)/(1,0), compute
   `chladni = (m + 2n)^1.7 × (1 + 0.11·n)`, sort ascending, normalize to the (2,0)
   fundamental, emit the first **96** entries as the two C++ arrays. **Constraint: the
   first 48 entries must reproduce `plate_modes.h` exactly** (same generator params
   P = 1.7, κ = 0.11) — diff them in a unit test or in the script itself. Update
   `kPlateModeCount`/`kPlateMaxModeCount` to 96 in `plate_modes.h`, keeping PlateBody's
   *used* count unchanged (PlateBody reads its own mode count — verify via
   `plate_mapper.h`; it must keep using 48).
2. `NoiseBodyMapper::kNoiseBodyModeCount` 32 → **64** (8 clean AVX2 iterations — the
   history comment at `noise_body_mapper.h:33-64` documents why multiples of 8 are
   mandatory; extend that comment, don't delete it).
3. **CPU gate:** run the existing voice benchmark (`[.perf]`-tagged tests; see the
   perf history in the mapper comment). Budget: ≤ 1.25% single voice. Measure both paths:
   - Fast path (NoiseBurst + NoiseBody): expect ~2× modal cost of the 0.38%-class number —
     should fit.
   - Slow path (Feedback + NoiseBody, per-sample `smoothCoefficients`): at 40 modes this
     measured 1.43% (over budget) — 64 will be worse. **Mitigation to implement with it:**
     clamp NoiseBody to 32 modes when the exciter is Feedback: pass a `maxModes` hint into
     the mapper via `VoiceCommonParams` (set from `DrumVoice` where the exciter type is
     known) and `std::min` it in the mapper. Feedback+NoiseBody is a drone/FX combo where
     wash density matters least.
4. Sanity: at f0 = 670 Hz the 64th ratio must stay below Nyquist margin — the bank already
   guards per-mode (verify: modes above ~0.45·fs are dropped or clamped by
   `ModalResonatorBank::setModes`; read `modal_resonator_bank.h` around the frequency clamp
   before assuming — if it doesn't guard, add per-mode frequency capping in the mapper).
5. Tests: `test_noise_body.cpp` mode-count expectations, table lock-step test
   (frequencies[k] = f0 × ratio[k] for 64 entries), perf tags, full `membrum_tests` +
   `dsp_processors_tests`. Re-render; the chime should audibly fuse into wash. Commit.

## Phase 6 — Preset polish, docs, compliance

1. Re-tune final crash/splash/china levels per kit against the new engine (level, modalMix
   vs wash balance, per-kit color/darkness), regenerate + install presets, update
   `preset-retune` docs and `MANUAL.md` (the manual documents crash behaviour), CHANGELOG
   entry under Unreleased.
2. Full gate per repo workflow: zero-warning build, **all** of `membrum_tests`,
   `dsp_processors_tests` (bank), pluginval strictness 5 on Membrum, clang-tidy
   (`./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja`).
3. Acceptance test file: all ACs green, `shouldfail` tags gone.
4. Final A/B render set (crash soft/med/hard, splash, both hats, snare, ride) into
   `preset-retune/renders/crash-redesign/`; listen; compare against a reference crash
   sample if available (`tools/membrum-fit` can extract features from a real crash WAV
   for objective comparison — optional but recommended).
5. Commit; PR.

### Explicit non-goals (do not do these)

- No full von Kármán / Ducceschi-Bilbao nonlinear modal simulation (research-grade CPU cost).
- No Poirot coupling-matrix implementation in v1 — the bloom envelope + slow-attack NLC
  approximates its audible output for ~zero CPU. (Candidate for a future spec if the bloom
  reads as "filtery" — note it in the plan's future-work section, don't build it.)
- No new user-facing parameters, no plugin_ids.h changes, no uidesc changes.
- No changes to NoiseBurstExciter (D7) in this pass — it is shared by snare/hats/clap.
  Log D7 as a follow-up: candidate fix is Q 2.0 → 1.0 plus a 20–30% dry-noise blend,
  gated behind its own A/B of all NoiseBurst consumers.
- Do not change `NoiseLayer::denormDecayMs` range — it would silently rescale every
  preset's parallel-layer decay.

### Risk table

| Risk | Guard |
|---|---|
| Hats/shaker/tambourine collateral from Phase 2 wash formula | Watchlist renders each phase; per-pad preset compensation, never formula hacks for one pad |
| Bit-identity regressions where NLC = 0 | Explicit bit-identity tests in Phases 3–4; bloom/attack changes gated on amount/coupling > 0 |
| CPU budget at 64 modes (slow path) | Phase 5 gate + Feedback-exciter mode clamp |
| Infinite-ring test vs longer decays | b1 ≈ 2 s⁻¹ → T60 3.5 s, well under the bank's 5 s legacy clamp region and `flushSilentModes` retirement; run the infinite-ring test every phase |
| Param wire precision for b3 norm 5.2e-5 | Float has ~7 significant digits — fine; add a round-trip assertion in the factory round-trip test for the crash pad's b3 |

### Sources

- Fletcher & Rossing — nonlinear vibrations and chaos in gongs and cymbals; Chladni (m+2n)^p mode law; ~100+ modes in crashes.
  https://www.researchgate.net/publication/49946650_Nonlinear_vibrations_and_chaos_in_gongs_and_cymbals
- FEM/experimental crash & splash study — band decay rates **50 ms (HF) … 8000 ms (LF)**; splash = faster/brighter crash.
  https://www.researchgate.net/publication/359611258_A_Detailed_FEM_Study_on_the_Vibro-acoustic_Behaviour_of_Crash_and_Splash_Musical_Cymbals
- Ducceschi & Bilbao — modal approach to nonlinear plate vibrations (cymbals/gongs); wave-turbulence energy cascade to HF.
  https://www.sciencedirect.com/science/article/abs/pii/S0022460X15000759
- Poirot et al. 2024 — simplified controllable mode-coupling (power-transfer matrix, phase randomization, real-time 200–1000 modes); the perceptual-model justification this plan leans on.
  https://link.springer.com/article/10.1186/s13636-024-00358-2
- Skare & Abel, DAFx 2019 — real-time modal crash cymbals, >2000 modes/cymbal on GPU; linear/nonlinear regime split.
  https://www.dafx.de/paper-archive/2019/DAFx2019_paper_48.pdf
- Dan Stowell — cymbal synthesis tutorial: 100 resonators 300 Hz–20 kHz, noise-like onset, delayed-HF "bwoosh", HP shimmer layer, tap=bell/hard=broadband.
  https://mcld.co.uk/cymbalsynthesis/
- Sound on Sound, *Synthesizing Realistic Cymbals* — dual-path ping+tail, HP filter opening over ~200 ms, ~0.2 s ping / ~3.7 s tail decays.
  https://www.soundonsound.com/techniques/synthesizing-realistic-cymbals
- Sound on Sound, *Practical Cymbal Synthesis* — TR-808 three-band different-decay architecture.
  https://www.soundonsound.com/techniques/practical-cymbal-synthesis
- Wave turbulence buildup in vibrating plates (HF buildup ms–100s of ms after strike).
  https://arxiv.org/pdf/1509.02737
