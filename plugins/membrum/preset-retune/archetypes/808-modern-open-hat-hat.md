# Membrum Recipe — 808 / Modern Open Hat (hat)

**Body:** NoiseBody (5)  ·  **Exciter:** NoiseBurst (2)  ·  **Choke group:** 1 (cut by closed hat)

## What the real thing is
The TR-808 open hi-hat is a *circuit*, not a miked cymbal. Six analog square-wave oscillators (measured **205.3 / 304.4 / 369.6 / 522.7 / 540 / 800 Hz**; osc 1 & 2 trimpot-tunable to 800 & 540 Hz, ±20% unit-to-unit) are summed into a deliberately **inharmonic hum**, fed through **two bandpass filters (~3440 Hz and ~7100 Hz)**, an envelope-controlled VCA, then a **high-pass filter**. The result is a thin, bright, metallic "tsss" with energy concentrated 3–10 kHz. The open-hat decay pot spans **~90–600 ms** (closed hat is fixed ~50 ms). There is **no pitch glide** and **no mechanical beater click** — the onset is just the VCA opening on filtered noise.

Sources: Baratatronix 808 cymbal/hi-hat synthesis breakdown; joesul.li "Synthesizing Hi-Hats"; Werner/Abel/Smith DAFx "The TR-808 Cymbal" model; ModeAudio hi-hat design.

## How it maps onto Membrum
**NoiseBody** gives exactly the right hybrid: a plate-ratio (free-plate Chladni) metallic modal bank reproducing the inharmonic hum, plus an internal filtered-noise layer for the wash. **NoiseBurst** is the correct synthetic-hat exciter — a short violet-noise bandpass burst (no impulsive click). A hot parallel **Noise Layer** (Violet, ~6 kHz) supplies the sustained shimmer; the ToneShaper **high-pass at ~800 Hz** reproduces the 808 output HP that keeps it thin.

| Param | Norm | Physical target | Why |
|---|---|---|---|
| Exciter Type | 0.40 | NoiseBurst | filtered-noise onset, not a strike |
| Body Model | 1.00 | NoiseBody | metallic modes + internal noise = 808 hum-under-noise |
| Material | 0.90 | brightness≈0.97, noise cut≈6 kHz | bright synthetic metal |
| Size | 0.40 | f0≈597 Hz | modal cluster in the 808 hum band (520–800 Hz) |
| Decay | 0.50 | ~0.95× modal ring | sustained open-hat tail |
| Strike Pos | 0.60 | off-center plate | dense bright inharmonic spread |
| Level | 0.72 | linear 0.72 | sits under kick/snare; tames hot noise layers |
| Filter Type | 0.50 | Highpass | 808 output HP |
| Filter Cutoff | 0.534 | ~800 Hz HP | strips lows, leaves shimmer |
| Filter Reso | 0.00 | Q≈0.707 | clean HP corner |
| Filter Env Amt | 0.50 | 0 (no mod) | static HP |
| Noise Mix | 0.80 | hot parallel noise | hat is mostly filtered noise |
| Noise Cutoff | 0.82 | ~6 kHz LP | shimmer band between the 3440/7100 bandpasses |
| Noise Reso | 0.20 | q≈1.24 | focused hiss, no whistle |
| Noise Decay | 0.52 | ~220 ms (rel ~110 ms) | open-hat wash (vs ~50 ms closed) |
| Noise Color | 0.90 | Violet | bright HP-tilted source |
| Click Mix | 0.00 | off | no beater click |
| NoiseBurst Dur | 0.15 | ~4 ms burst | soft "chht" onset |
| Mode Stretch | 0.45 | phys≈1.18 | nudge toward inharmonic clang |
| Decay Skew | 0.60 | +0.2 | keep top shimmering as body shortens |
| Choke Group | 0.125 | group 1 | cut by closed hat |
| Pan | 0.50 | center | conventional hat placement |

**Off / sentinel:** Mode Inject 0, Nonlinear Coupling 0, Drive 0, Fold 0, PitchEnv Time 0 (no glide), Tension Mod 0, Air Loading 0, Secondary disabled, Body Damping b1/b3 at −1 sentinel (derive from Decay/Material), Mode Scatter 0, macros neutral 0.5.

## Voicing notes (post-audit)
- Voiced against the **corrected** gain staging: body is unit-peak normalized at −12 dBFS, noise/click rebalanced, −1 dBTP bus limiter owns the ceiling, so `Level` is a true linear trim. The two hot noise sources (NoiseBody internal + parallel Noise Layer) justify `Level 0.72`.
- **No Mode Inject**: it adds an *integer* harmonic series (a pitch) — the opposite of the inharmonic 808 hum.
- Pairs with an 808 closed hat (same NoiseBody+NoiseBurst, Material/Size matched, but Decay≈0.08 / Noise Decay≈0.10, also choke group 1) so the closed hit chokes this open tail.