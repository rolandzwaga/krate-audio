# Membrum Recipe — Vibraslap (perc)

**Body:** NoiseBody (norm 1.0)  ·  **Exciter:** NoiseBurst (norm 0.4)

> The rattling buzz of loose steel rivets/pins in a wooden resonator box — a bright, band-formant metallic noise rattle with a longer, woodier, decelerating decay than a shaker.

## Acoustics (researched)

A vibraslap is a modernized quijada (donkey-jawbone-with-loose-teeth): a U-shaped stiff steel wire links a **wooden ball** to a **hollow wooden box**; inside, a metal frame carries several **loosely-fastened steel rivets/pins** that rattle against the box ([Wikipedia](https://en.wikipedia.org/wiki/Vibraslap); [BYU Percussion](https://percussion.byu.edu/vibraslap)). Striking the ball sends an impulse down the wire that shakes the rivets into the famous **"decelerating ratchet"** rattle ([HISSandaROAR](https://hissandaroar.com/v3/soundlibrary/ufx035-vibraslap/)).

- **Pitch:** none — inharmonic, noise-dominated. Perceived "tone" = wooden-box band-formant + metallic-rivet impact cloud.
- **Spectrum:** ~80% bright metallic noise, band-shaped by a woody box resonance (bright but not pure white hiss).
- **Inharmonicity:** strong, from loose-rivet scatter (not stiff-bar dispersion).
- **Transient:** a soft wooden slap (ball-on-palm) into immediate rattle — not a sharp tick.
- **Decay (T60):** MEDIUM — "a rattle, usually lasting a few seconds … quickly damped, two-to-three seconds or less" ([afterglow](https://www.afterglowatx.com/blog/2022/12/14/the-case-of-the-recognizable-yet-unknown-instrument-the-vibraslap)). Longer/woodier than a shaker/cabasa (~0.1–0.2 s); trimmed to a musical ~0.4–0.55 s kit tail. Energy decays as the rattle decelerates.
- **Pitch glide:** NONE (the only time-variation is rattle-density decay).

## Mapping → Membrum (post-audit semantics)

| Param | Norm | Physical target | Why |
|---|---|---|---|
| Exciter | 0.4 | NoiseBurst | violet-noise burst = rivet-rattle excitation |
| Body | 1.0 | NoiseBody | metallic plate-ratio box + internal noise |
| Material | 0.85 | brightness ≈0.96, noise cutoff ≈5.7 kHz | steel rivets → bright/metallic |
| Size | 0.22 | f0 ≈ 542 Hz | small wooden box |
| Decay | 0.32 | box ring ≈ 0.56 s | medium, woody, > shaker |
| Level | 0.70 | linear 0.70 | perc accent under the drums |
| NoiseBurst Duration | 0.55 | 9.15 ms burst | LONG burst = sustained rattle |
| Noise Mix | 0.85 | dominant layer | ~80% rattle noise |
| Noise Cutoff | 0.78 | LP ≈ 3.6 kHz | bright but woody, not white hiss |
| **Noise Resonance** | **0.30** | **Q ≈ 1.71 band** | **the wooden-box "formant"** |
| Noise Decay | 0.32 | ≈110 ms env | decelerating woody tail |
| Noise Color | 0.62 | White | flat broadband rattle source |
| Click Mix | 0.30 | 0.30 | soft wooden slap onset |
| Click Contact | 0.30 | 2.9 ms | brief, not razor |
| Click Brightness | 0.60 | ≈1.9 kHz | mid-bright woody thwack |
| **Mode Scatter** | **0.62** | **~9% dither** | **loose rivets → inharmonic** |
| **Complexity** | **0.78** | coupling/nonlin/inject deltas | rattle density |
| Body Damping b1 | 0.32 | ≈16 s⁻¹ flat | woody decay floor |
| Body Damping b3 | 0.00 | no f² damping | metallic highs ring |
| Air Loading | 0.00 | n/a (box body) | membrane-only, off |
| Pan | 0.50 | center | perc default |
| Pad Enabled | 1.0 | on | — |

## Deliberately defaulted

ToneShaper SVF + filter env (band-shaping done in the noise layer); Drive/Fold (no extra saturation); **all PitchEnv params OFF** (no glide); Mode Stretch neutral (scatter carries inharmonicity); Decay Skew neutral; Mode Inject / Nonlinear Coupling base 0 (Complexity macro adds the small deltas); Material Morph off; Choke 0 / main bus; FM/Feedback/Friction (wrong-exciter no-ops); Tightness/Brightness/BodySize/Punch macros neutral; Secondary shell off; Tension Mod 0 (Membrane-only, no glide).

**Sources:** [Wikipedia – Vibraslap](https://en.wikipedia.org/wiki/Vibraslap) · [HISSandaROAR UFX035](https://hissandaroar.com/v3/soundlibrary/ufx035-vibraslap/) · [afterglow](https://www.afterglowatx.com/blog/2022/12/14/the-case-of-the-recognizable-yet-unknown-instrument-the-vibraslap) · [BYU Percussion](https://percussion.byu.edu/vibraslap) · [organology.net](https://organology.net/instrument/vibraslap/) · [CarvedCulture tutorial](https://www.carvedculture.com/blogs/articles/how-to-play-the-vibraslap-tutorial)
