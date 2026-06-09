# Membrum Recipe — Cajon (world)

**Archetype:** Cajon — a box drum whose plywood front board (*tapa*) radiates. Two voices on one (or two adjacent) pad(s): a **BASS** tone struck at the center and a **SLAP** struck at the top corner/edge. Built on the **Plate** body (free-plate Chladni) + **Impulse** exciter.

## Acoustic basis (cited)
- The tapa is a thin (2.5–3 mm) clamped **plywood plate** that flexes and radiates — physically a struck **plate**, not a membrane. [Kopf Percussion](https://www.kopfpercussion.com/blog/understanding-the-anatomy-and-physics-of-cajon-drums/), [Hluru tapa thickness](https://www.hluru.net/en-us/blogs/skills-tips/how-cajon-front-plate-thickness-2-5mm-2-8mm-3mm-3-5mm-shapes-high-frequencies-sensitivity)
- Low spectrum has ~3 peaks 60–400 Hz: a **Helmholtz cavity** peak (~63→94 Hz with hole size) + tapa **(0,0) mode 131.5 Hz** + higher modes (2,0),(1,2). [UBC PHYS341](https://wiki.ubc.ca/Course:PHYS341/2018/project/cajon), [Scribd CajonAcoustics](https://www.scribd.com/document/861723614/CajonAcoustics)
- Engineers treat the cajon "kick" fundamental as **80–140 Hz** (higher than a real kick), boxy mids **250–500 Hz**, slap snap **5–10 kHz** (+8–10 dB @ ~8 kHz). [Pro Audio Files](https://theproaudiofiles.com/recording-and-mixing-cajon/)
- **Center palm** strike → strong bass / kick-like; **top-corner fingers** → bright snare-like snap+sizzle. Bass tones decay **fast**; the seated player damps the box. [UBC PHYS341](https://wiki.ubc.ca/Course:PHYS341/2018/project/cajon), [Pro Audio Files](https://theproaudiofiles.com/recording-and-mixing-cajon/)

## Mapping to Membrum (post-audit semantics)
- **Body = Plate** (free-plate (m+2n)^1.7 Chladni, fundamental (2,0)) — confirmed in `plate_modes.h`; correct inharmonic plate spectrum.
- **Exciter = Impulse** for both voices (hard hand/finger contact); attack character carried by the always-on **Click** layer.
- The **box cavity / back-board** is the per-pad **Secondary shell** (Coupling Strength + Secondary Enabled), tuned below the tapa for Helmholtz-region weight — used on the BASS voice only.
- **PitchEnv** supplies the bass "kerthump" (~165→85 Hz, ~35 ms); SLAP has PitchEnv **off**.
- Plate body ⇒ **airLoading** and **tensionMod** are NO-OPs (membrane-only) and stay at default.

## Voice 1 — BASS (center)
| Param | Norm | Physical |
|---|---|---|
| Body Model | 0.2 | Plate |
| Material | 0.30 | woody (brightness 0.65) |
| Size | 0.965 | f0 ≈ 87 Hz |
| Decay | 0.45 | ~0.7 s body RT |
| Strike Position | 0.50 | center |
| PitchEnv Start/End/Time/Curve | 0.4585 / 0.3143 / 0.07 / 0.35 | 165→85 Hz, 35 ms, fast drop |
| Mode Stretch | 0.42 | phys 1.13 (mild inharmonic) |
| Decay Skew | 0.62 | +0.24 (low modes ring) |
| Coupling Str / Secondary En / Sec Size / Sec Mat | 0.45 / 1.0 / 0.55 / 0.30 | box cavity, shell f0 ≈51 Hz, woody |
| Click Mix / Contact / Bright | 0.35 / 0.50 / 0.40 | soft 3.5 ms ~1.1 kHz thud |
| Noise Mix | 0.0 | clean |

## Voice 2 — SLAP (top corner)
| Param | Norm | Physical |
|---|---|---|
| Body Model | 0.2 | Plate |
| Material | 0.60 | brighter (brightness 0.8) |
| Size | 0.70 | f0 ≈ 159 Hz |
| Decay | 0.25 | short |
| Strike Position | 0.12 | corner/edge |
| PitchEnv Time | 0.0 | glide OFF |
| Mode Stretch | 0.45 | phys 1.175 |
| Decay Skew | 0.35 | −0.30 (high modes) |
| Noise Mix / Cutoff / Reso / Decay / Color | 0.40 / 0.78 / 0.30 / 0.20 / 0.85 | violet sizzle ~5.4 kHz, 45 ms |
| Click Mix / Contact / Bright | 0.85 / 0.15 / 0.82 | sharp 2.45 ms ~7.5 kHz tick |

## Defaults (acoustic box-drum policy)
Drive/Fold = 0 (no saturation); Mode Inject / Nonlinear Coupling = 0 (acoustic, linear); ToneShaper filter transparent (cutoff 1.0, env 0.5); all five Macros neutral 0.5 (recipe sets underlyings directly); Mode Scatter 0; Air Loading & Tension Mod = NO-OP on Plate; Body Damping b1/b3 sentinel (derive from Decay/Material); Morph off; Choke 0; Output Bus main; FM/Feedback/NoiseBurst/Friction params = exciter no-ops.