# Membrum Recipe — "FM-Bell Crash" (synthetic clangy metallic crash cymbal)

**Body:** Bell (Chladni church-bell partial bank, 16 modes) · **Exciter:** FMImpulse (Chowning bell-FM)

A pitched, clangy metallic crash whose *body* is an inharmonic FM bell rather than a noise wash — used in the modular kit. The strike note sits ~280–300 Hz (the measured "big crash booms at ~300 Hz" region), the tuned Bell partials are warped off-pitch by Mode Stretch + Mode Scatter into a clangy cymbal cluster, and a bright violet-noise shimmer overlay supplies the high "shhh" sheen. Velocity-sensitive NonlinearCoupling models the cymbal's nonlinear brightening on hard hits. No pitch glide.

## Why this body + exciter

- **Bell body** gives a *tonal* inharmonic partial set (hum at 0.25·f0, nominal at 1.0, partials to 12×) — the pitched clang a noise body can't produce. At `size = 0.45`, f0_nominal = 800·0.1^0.45 ≈ **283.8 Hz**, hum ≈ **71 Hz**, top partial ≈ **3.4 kHz** — matching the measured low-frequency mode cluster of a real crash (Wilbur & Rossing; FEM crash studies).
- **FMImpulse** is the archetype's defining feature: a Chowning inharmonic carrier:modulator pair (`fmRatio 0.45 → 1:2.35`) injects a bell-FM sideband cluster into the carrier *before* the body rings — FM clang layered on modal clang. This is exactly the classic synthetic-cymbal route (FM a bright carrier by an inharmonic modulator to spawn hundreds of enharmonic partials; SOS, Gordon Reid).

## Parameter baseline (all values NORMALIZED [0,1])

| Param | Norm | Denormalized target | Rationale |
|---|---|---|---|
| Exciter Type | 0.80 | FMImpulse (idx 4) | Chowning bell-FM excitation |
| Body Model | 0.80 | Bell (idx 4) | tuned inharmonic Chladni bank |
| Material | 0.92 | brightness 0.976, base decay 1.32 s | very metallic, long HF tails |
| Size | 0.45 | f0_nom 283.8 Hz (hum 71 Hz) | ~300 Hz crash body/clang |
| Decay | 0.78 | ring ≈ 2.8 s | long crash wash |
| Strike Position | 0.32 | azimuth 0.50 rad | uneven, clangy partial balance |
| Level | 0.70 | linear 0.70 | sits under peak drums |
| FM Ratio | 0.45 | c:m = 1:2.35 | dense inharmonic FM cluster (brief spec) |
| Mode Stretch | 0.55 | physical 1.325 | warp tuned bell → cymbal dispersion |
| Mode Scatter | 0.65 | ~10% freq dither | mode splitting / shimmer, metal not glockenspiel |
| Nonlinear Coupling | 0.45 | env-driven brightening | velocity-sensitive HF energy cascade |
| Noise Mix | 0.40 | parallel LP-noise layer | broadband sheen/shimmer overlay |
| Noise Cutoff | 0.92 | ~12 kHz LP | bright sheen band |
| Noise Color | 0.85 | Violet | metallic sizzle tilt |
| Noise Decay | 0.72 | ~590 ms | shimmer shorter than body ring |
| Noise Resonance | 0.20 | Q 1.24 | no pitched noise peak |
| Click Mix | 0.35 | contact tick | bright metal-strike onset |
| Click Brightness | 0.85 | ~6.6 kHz BP | sharp metallic tick |
| Click Contact | 0.30 | 2.9 ms | crisp brief impact |
| Body Damping b1 | 0.30 | 15.1 s⁻¹ flat | long metallic ring floor |
| Body Damping b3 | 0.00 | 0 (no f² damping) | bronze's weak HF damping → long shimmer |

## Deliberately left at default (per-pad coverage)

PitchEnv family (Time=0 → off: a crash has no pitch glide), Mode Inject (0 → no synthetic harmonic series fighting the clang), ToneShaper filter/drive/fold (transparent → body+noise define the spectrum), Material Morph (static timbre), Secondary shell + Coupling Strength (a crash is one radiating plate, no drum shell), Tension Mod & Air Loading (Membrane-only no-ops here), all five macros (neutral 0.5 to preserve the explicit values), Choke/Bus/Pan (kit/mix decisions), and the three non-selected exciter params (Feedback/NoiseBurst/Friction).

## Physics → params (key research-derived numbers)

- **~300 Hz body / sub-kHz clang cluster** → Bell `size 0.45` (f0_nom 283.8 Hz). *(Musical-U; gearpage; Wilbur & Rossing low n=0 mode family.)*
- **Strongly inharmonic, partials deviate from integers** → `modeStretch 0.55` + `modeScatter 0.65`. *(FEM crash study; Rossing cymbal modes.)*
- **Frequency-banded decay (HF ~50 ms, LF up to ~8 s)** → long body `decay 0.78` + `b3=0` (slow HF), shorter `noiseDecay 0.72`. *(FEM crash/splash study.)*
- **2–3 kHz "shhh" + 10–12 kHz sheen, flat broadband** → violet noise overlay, `cutoff ~12 kHz`. *(SOS; Musical-U; gearpage.)*
- **Nonlinear brightening on hard hits (energy cascade)** → `nonlinearCoupling 0.45`. *(Stowell; Ducceschi/Bilbao.)*
- **Inharmonic c:m bell FM** → `fmRatio 0.45 (1:2.35)`. *(Wikipedia FM synthesis; CCRMA CLM FM2.)*

## Sources

- Wikipedia — Frequency modulation synthesis: https://en.wikipedia.org/wiki/Frequency_modulation_synthesis
- CCRMA CLM FM tutorial (bell/tubular-bell FM): https://ccrma.stanford.edu/software/clm/compmus/clm-tutorials/fm2.html
- Sound on Sound — Synthesizing Realistic Cymbals: https://www.soundonsound.com/techniques/synthesizing-realistic-cymbals
- Musical-U — Percussion Frequencies: Cymbals: https://www.musical-u.com/learn/percussion-frequencies-part-2-cymbals/
- Drum frequencies (kick/snare/crash): https://www.audiorecording.me/drum-frequencies-of-kick-bass-drum-hi-hats-snare-and-crash-cymbals.html
- The Gear Page — crash cymbal frequency range: https://www.thegearpage.net/board/index.php?threads/what-frequency-range-are-crash-cymbals.2309482/
- Normal modes of an 18-inch crash cymbal (Wilbur & Rossing): https://www.researchgate.net/publication/28576788_Normal_modes_of_an_18_inch_crash_cymbal
- The normal modes of cymbals: https://www.researchgate.net/publication/289898852_The_normal_modes_of_cymbals
- FEM study, vibro-acoustic behaviour of crash & splash cymbals: https://www.researchgate.net/publication/359611258_A_Detailed_FEM_Study_on_the_Vibro-acoustic_Behaviour_of_Crash_and_Splash_Musical_Cymbals
- Cymbal vibration modes (m,n classification): https://oemcymbal.com/understanding-cymbal-vibration-modes/ , https://arboreacymbal.com/understanding-cymbal-vibration-modes/

*Membrum source of truth:* `plugins/membrum/src/dsp/bodies/bell_modes.h`, `bell_mapper.h`, `natural_fundamental.h`; `plugins/membrum/src/dsp/exciters/fm_impulse_exciter.h`; `noise_layer.h`, `click_layer.h`, `unnatural/nonlinear_coupling.h`; `plugins/membrum/src/voice_pool/voice_pool.cpp` (denorm). Voiced against post-audit corrected behaviour (`AUDIT-signal-path-2026-06-07.md`): N-1 measured-strike body norm, corrected (k+1) Mode Stretch, env-level NonlinearCoupling, L-3 FM modulation-index fix.