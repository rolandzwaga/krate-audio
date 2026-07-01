# Membrum Snare Body Investigation

*Audience: Membrum DSP maintainer. All file:line references are to the shipped default snare path unless noted.*
*Produced 2026-07-01 by a multi-agent investigation workflow (map → research → diagnose → adversarial verify → plan).*

## 1. Symptom

The default Membrum snare reads as a thin hi-hat rather than a struck drum: it lacks a definite pitched "body"/"thock" and its perceptual mass sits in the upper-mid/high broadband band instead of the ~180-330 Hz membrane region. A dedicated diagnostic already exists for exactly this complaint (`plugins/membrum/tests/unit/diagnose/test_snare_body_diagnose.cpp:1-19`, header literally "why the snare sounds like a thin hi-hat"). The symptom is real and acknowledged in-tree; this report isolates the causes that survived adversarial verification and rules out the ones that did not.

## 2. Root causes (design mistakes), most-severe first

The four confirmed causes are **compounding** — none is independently sufficient, but together they collapse the body toward the always-on broadband layers so the spectrum ends up looking like the hi-hat archetype. Two are gain-staging/engine bugs (CODE); two are recipe/tuning mistakes (PRESET/DATA).

### H4 — Strike-normalization probe uses a clean raised-cosine, not the real excitation, so it under-scales the body *(HIGH, CODE)*

- **What it is:** `probeStrikePeak()` drives the modal bank with a canonical 2 ms raised-cosine of amplitude 1.0 (`drum_voice.h:1200-1208`; `in = 0.5*(1-cos(2*pi*ph))`), with output gain forced to 1.0 and soft-clip 0 so the returned peak is raw (`drum_voice.h:1197-1198`). `measuredStrikeOutputGain()` then sets the body gain = `kBodyHeadroom / probePeak = 0.5 / probePeak` (`drum_voice.h:1222-1225`, `kBodyHeadroom = 0.5` at `drum_voice.h:84`).
- **Causal chain:** The real note never receives that pulse. It excites the body with `exc = exciterBank_.process(...) + clickSample*0.5f` (`drum_voice.h:482-486`; slow path `958-960`), where the snare exciter is **NoiseBurst** — violet noise through an 800→9600 Hz bandpass, resonance 2.0 (`noise_burst_exciter.h:104,109,138-139,57-59`), returned unscaled (`exciter_bank.h:62-67`). A coherent raised-cosine rings the low membrane modes (~199-1600 Hz, `membrane_mapper.h:100-118`) in phase and peaks *higher* than the phase-random, HF-tilted noise burst does. Since gain = `0.5 / probePeak` and `probePeak` is inflated, the real body lands several dB **below** its −6 dBFS budget. The project's own audit confirms the magnitude: `AUDIT-signal-path-2026-06-07.md:193-202` states the intended N-1 design was a measured render using the *real* excitation and that the raised-cosine proxy "may be inflated ~2-3x" (≈6-10 dB) — and that fix was never shipped. The over-estimate is likely larger for the HF-tilted NoiseBurst than for the Mallet the audit measured.
- **Severity: HIGH.** Directly steals 6-10 dB from the only body-character source.

### H3 — Noise-layer gain is calibrated at cutoff=0.5 but the snare runs cutoff=0.72, so the layer ships ~5 dB hot *(HIGH, CODE)*

- **What it is:** `kStandaloneOutputGain = 0.243f` (`noise_layer.h:220`, documented "raw peak ~0.518 → ~−18 dBFS") is pinned by `gain_staging_balance_test.cpp:40-43`, which measures the raw noise peak at `p.cutoff=0.5` (~849 Hz LP) / `p.resonance=0.3` and asserts the effective peak lands in (−24,−15) dBFS (`:52,59-60`). The snare instead runs `noiseLayerCutoff=0.72` (~3.25 kHz) / `resonance=0.25` (`default_kit.h:98-99`).
- **Causal chain:** A 2nd-order LP passes white-noise energy roughly proportional to bandwidth (RMS/peak ∝ √fc). The single fixed 0.243 constant is applied unconditionally in `DrumVoice` with no cutoff compensation (`drum_voice.h:494`, `:798`, `:980`). A direct SVF simulation on white noise (48 kHz, 20 seeds) gives peak 0.607 @ 849 Hz/Q1.71 vs **1.111 @ 3254 Hz/Q1.48 — a factor of 1.83 (+5.2 dB)**. So the snare's noise layer ships near ~−13 dBFS, not the −18 dBFS the constant was tuned for. It is no longer safely under the body's −6 dBFS budget; it sits level with a body that H4 has already knocked down.
- **Severity: HIGH.** The calibration test "pins" a ratio the snare never actually ships.

### H5 — Snare recipe is noise-and-click dominant by design *(HIGH, PRESET)*

- **What it is:** The recipe deliberately loads broadband upper-mid layers: `noiseLayerMix=0.9`, `noiseLayerCutoff=0.72` (~3.25 kHz), `clickLayerMix=0.6`, `clickLayerBrightness=0.7` (`default_kit.h:97-104`), and — critically — the **body is excited by NoiseBurst** (`default_kit.h:86`), not a coherent strike. Click is summed both into the excitation (`exc += clickSample*0.5`) and full-amplitude directly into the body output (`combinedBody = body + noiseSample + clickSample`, `drum_voice.h:482-495`).
- **Causal chain:** Even with H3/H4 fixed, the recipe drives the body with incoherent violet BP noise centered well *above* the body's mode band, so the body barely reaches its budget, while the always-on layers deliver full calibrated energy. The snare's `noiseLayerMix` (~0.82-0.9) is essentially the same as the closed-hat archetype's 0.85 (`recipes.json:17946`), so a weak body leaves a spectrum dominated by the same layer the hi-hat uses → reads as a hi-hat.
- **Two corrections vs the original hypothesis** (do not chase these): `color=0.75` maps to **White, not Violet** — the threshold for Violet is 0.80 (`noise_layer.h:204-207`); and the layers are **not** calibrated louder than the body in peak terms (both ~−18 dBFS, `noise_layer.h:220` / `click_layer.h:156`) — the dominance comes from the noise-burst under-driving a strike-normalized body, not from layer gains exceeding it.
- **Severity: HIGH.** A recipe intending the noise to carry "most of the sound" (`acoustic-snare-wire-buzz.md:21`) cannot be rescued by gain-staging alone.

### H6 — Default snare Size=0.5 tunes the body to ~158 Hz, below the well-tuned window *(MEDIUM, PRESET/DATA)*

- **What it is:** Membrane `f0 = 500 * 0.1^size` and ignores any pitch param (`membrane_mapper.h:88-100`; `pitchHz` is `/*ignored*/`). Default snare `size=0.5` (`default_kit.h:89`) → **158.11 Hz**, versus the archetype's documented `Size 0.40 → 199 Hz` on the measured (0,1) fundamental (`acoustic-snare-wire-buzz.md:20`, target window 170-200 Hz).
- **Causal chain:** A body ~41 Hz / a whole tone below the "sounds great" window is less present and pushes perceptual weight further toward the ~3.25 kHz noise centroid. The default snare has no pitch envelope to lift it (`tsPitchEnvTime=0`, `pad_config.h:198`; `isPitchEnvActive()` false, `tone_shaper.h:308-311`), so it rings at 158 Hz for the whole note.
- **Severity: MEDIUM.** Concrete tuning mismatch, minor next to H3/H4/H5.

## 3. Ruled out (do not re-chase)

- **H1 — "body normalized to peak while pitched tone lives far below, dwarfed by a sustained noise bed."** REFUTED. The noise layer has `setSustain(0.0f)` (`noise_layer.h:54,92`) — it is a decaying transient (~100 ms), not a sustained −19 dBFS bed. At onset the body (−6 dBFS budget) is ~13 dB *louder* than the noise peak (~−19 dBFS), which *satisfies* the "body loudest at onset" target, opposite the hypothesis.
- **H2 — "NoiseBurst distributes excitation into upper modes, so the body itself rings noise-colored."** REFUTED. The modal bank injects a single scalar exciter sample into every mode weighted only by `inputGain_[k]`, which comes purely from the mapper's Bessel amplitude (`modal_resonator_bank.h:749-846`, `membrane_mapper.h:113-118`) — identical regardless of exciter color. The exciter cannot preferentially load upper modes; mode balance is fixed by strike position. (Note: NoiseBurst is still implicated via H4/H5 — it *under-drives* the low modes and adds its own upper-mid energy — but not via the "recolors the modes" mechanism claimed here.)
- **H7 — "tone-shaper highpass/low-cutoff cuts the ~158 Hz body."** REFUTED for the default. The tone shaper is a bit-identity bypass on the default snare: per-note denorm yields cutoff 20000 Hz and filterEnvAmount 0.0 → `filterBypass` true (`voice_pool.cpp:833-836` with `pad_config.h:191,193`; bypass test `tone_shaper.h:425-426`), and the DC blocker is gated off (no drive/fold, `tone_shaper.h:418`). A body cut only occurs if a preset explicitly configures a Highpass/low cutoff (e.g. Brush Sweep).

## 4. Target (from the research)

A convincing snare needs the pitched body as a **co-equal or dominant** partner, never buried:

- **Body:** pitched energy at coupled J(0,1) ≈ **180-220 Hz** (two detuned partials) + J(1,1) ≈ **330 Hz** + shell modes ~600-1100 Hz. Body should be the **loudest single spectral feature at onset**, ~0 dB (peak) reference in the **150-400 Hz** band.
- **Wire noise:** broadband, mass in **2-10 kHz** (crack 2.5-5 kHz, sizzle 6-10 kHz), roll-off to 15-20 kHz, essentially **nothing below ~1 kHz**.
- **Balance:** peak broadband noise **−3 to −9 dB relative to the body-band peak** for a natural medium hit (**−6 dB** is the good "snare-not-hi-hat" default). Integrated energy: roughly **45-60% body / 40-55% noise**. Failure threshold: if the sub-1 kHz pitched body is **more than ~12-15 dB below** the noise peak, the sound collapses into hi-hat/filtered-noise territory.
- **Decay:** near-instant attack on both; body fast (~50-200 ms, J(0,1) decays ≥2× faster than upper partials); noise tail equal-to-longer (~150-400 ms). Crossover where noise overtakes body ~1-1.5 kHz.

## 5. Fix plan (ordered, minimal-risk-first)

Fixes are independent but compounding — do them in order and re-measure the body-band/noise-band ratio (§6) after each so you can see each one's contribution rather than over-correcting.

### Fix A (PRESET, lowest risk) — Retune the default snare body
- **File:** `plugins/membrum/src/dsp/default_kit.h`
- **Change:** `size 0.5 → 0.4` (line 89) → f0 199 Hz, into the 170-200 Hz window. Optionally nudge Mode Scatter `0.20 → 0.28` (line 107) for the (0,1)/(1,1) split.
- **Audible effect:** body gains a definite, more present pitch; less perceptual weight ceded to the noise centroid.
- **Verify:** peak of the band-limited body RMS (150-400 Hz) shifts up from 158→199 Hz; re-run `test_snare_body_diagnose.cpp`.
- **Constraint:** data-only. Do **not** try to fix pitch via a pitch param — `membrane_mapper.h:100` ignores `pitchHz`.

### Fix B (PRESET) — Rebalance the recipe toward the body
- **File:** `plugins/membrum/src/dsp/default_kit.h` (Snare template, lines 85-113)
- **Change:** exciter `NoiseBurst → Impulse` (or Mallet) so the strike-normalized body actually reaches its −6 dBFS budget and produces a real ~199 Hz onset; `noiseLayerMix 0.9 → ~0.4-0.5`; `clickLayerMix 0.6 → ~0.4-0.5`; darken wires (`noiseLayerCutoff 0.72 → ~0.5`/~800 Hz, `color` down toward pink); add HF body damping (`bodyDampingB3` ~0.5-0.7) to kill the metallic sizzle. The diagnostic's candidate C is a ready-made starting point (`test_snare_body_diagnose.cpp:317-330`).
- **Audible effect:** body becomes the loudest onset feature; noise recedes to a wire-rattle accent.
- **Verify:** §6 assertion; A/B the diagnostic's NoiseBurst-driven vs Impulse/Mallet body-only peaks (`test_snare_body_diagnose.cpp:412-415`).
- **Constraint:** data-only. Switching the exciter is a preset choice, not an engine change.

### Fix C (CODE) — Make noise-layer standalone gain track cutoff
- **File:** `plugins/membrum/src/dsp/noise_layer.h` (compute in `configure()`/`configureRaw()`); apply in `plugins/membrum/src/dsp/drum_voice.h:494,798,980`
- **Change:** replace the bare `kStandaloneOutputGain` with an effective gain = `0.243f * sqrt(denormCutoff(0.5) / denormCutoff(p.cutoff))` (fcRef = 849 Hz). This holds the layer at ~−18 dBFS across presets. Extend `gain_staging_balance_test.cpp` to assert the effective peak stays in −18±3 dBFS at the *actual* per-preset cutoffs (0.72 snare, 0.88 hat, 0.82 cymbal), not just 0.5.
- **Audible effect:** removes the ~5 dB hot bias on the snare's wire layer (and fixes hat/cymbal too).
- **Verify:** the extended `gain_staging_balance_test.cpp`.
- **Constraint (CLAUDE.md):** RT-safe — compute the effective gain once at `configure()` time (control-rate), not per-sample; `sqrt` is fine off the audio hot loop. No allocation. Cross-platform: uses only `std::sqrt`/`std::exp` already in `denormCutoff`.

### Fix D (CODE, highest engineering effort) — Probe with the real excitation
- **File:** `plugins/membrum/src/dsp/drum_voice.h:1188-1214` (`probeStrikePeak`)
- **Change:** replace the raised-cosine loop with a render driven by the configured exciter at a reference velocity (temporary NoiseBurst/Mallet/etc. + the `clickLayer*0.5` excitation path) so `probePeak` reflects the real per-exciter, per-body ring-up. Key the `StrikeNormKey` cache additionally on `exciterType` and excitation-shaping params (contactMs, noise color/cutoff) so it re-measures when they change. Delivers the audit's stated N-1 goal (`AUDIT-signal-path-2026-06-07.md:199-202`) and removes the ~2-3× (larger for HF-tilted NoiseBurst) over-estimate, lifting the body back to −6 dBFS. Cheaper interim: per-exciter empirical correction `probePeak / kExciterOverExciteFactor[type]` calibrated from `test_snare_body_diagnose.cpp`, but the real-excitation render is the correct fix.
- **Audible effect:** body lands at its intended budget under the excitation it actually ships with.
- **Verify:** measured real-body peak (NoiseBurst-driven) reaches −6 dBFS ±3 dB; `test_snare_body_diagnose.cpp` A/B.
- **Constraint (CLAUDE.md):** RT-safe — no allocation; reset the probe exciter/click state after measuring (mirror the existing `reset()` on the bank). Keep the measurement off the audio thread if it's currently a control-rate cache population; otherwise bound its cost.

**Doc cleanup (trivial, any commit):** stale comments — `noise_layer.h:213` and `click_layer.h:152` still reference the old −12 dBFS body budget (now −6 dBFS via `kBodyHeadroom=0.5`); the `default_kit.h:101` comment claims Violet where `color=0.75` is actually White.

## 6. Suggested verification test

Add a regression assertion (extend `plugins/membrum/tests/unit/diagnose/test_snare_body_diagnose.cpp`, or promote it to a non-diagnostic unit test) that renders the full default snare voice at a medium velocity and measures **band-limited RMS over the first N ms of the onset**:

- Render the default snare (velocity ~0.8), N = first ~80 ms (onset window covering the body thock before the noise tail dominates).
- Compute `bodyRms` = RMS band-limited to **150-400 Hz** and `noiseRms` = RMS band-limited to **5-10 kHz** (FFT-bin sum, or paired band-pass SVFs).
- **Assert a body-band floor and a body-vs-noise ceiling:**
  - `20*log10(bodyRms) > kBodyBandFloorDbfs` (body-band energy must exist — catches a body knocked below its budget by H3/H4).
  - `20*log10(bodyRms) - 20*log10(noiseRms) >= -9.0` dB, ideally `>= -6.0` (body must be within the research "snare-not-hi-hat" window; a bare −12-15 dB deficit is the hi-hat-collapse threshold and must fail).
- Optionally a spectral-centroid guard: onset-window centroid must be **below ~1.5 kHz** (the research crossover), so a noise-flooded snare fails.

Cross-platform note (CLAUDE.md): use `Approx().margin()` / dB tolerances rather than exact FP compares (MSVC/Clang diverge at the 7th-8th decimal), and fix the RNG seed(s) for the noise layers so the measurement is deterministic across runs and platforms.

---

**Bottom line:** the snare-as-hi-hat symptom is the compounding product of H3 (noise layer ~5 dB hot vs its calibration), H4 (body under-scaled ~6-10 dB by a mis-shaped normalization probe), H5 (a recipe that intentionally lets noise/click carry the sound while the body is noise-burst-excited), and H6 (body tuned a whole tone flat of the well-tuned window). Two are engine/gain-staging CODE fixes (H3→Fix C, H4→Fix D), two are PRESET/DATA fixes (H5→Fix B, H6→Fix A). The tone shaper (H7) and the modal-recoloring theory (H2) are dead ends. Ship the preset fixes first (cheap, reversible, big audible gain), then the gain-tracking fix, then the probe rework.
