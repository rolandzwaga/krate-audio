# Membrum Recipe — 808 Tom (boom-glide)

**Archetype:** electronic tom (TR-808). A tuned **sine** that descends in pitch — a *boom that drops*, not a struck acoustic membrane.

## Physics / synthesis basis (cited)
- The real 808 tom is a **bridged-T network** (2R+2C) kicked into self-oscillation by a trigger pulse — VCO+VCA+ENV in one, producing a *self-dampening sine that rings briefly and fades* ([Baratatronix](https://www.baratatronix.com/blog/808-tom-synthesis)).
- The downward sweep comes from **diodes (D80–D85)** whose resistance rises as the sound decays, dragging the pitch down — the real effect is **subtle** ("less like a boing, more like a tonk"). The exaggerated **boom-glide** (e.g. 220→80 Hz over a long sweep) is the musical extension of that same mechanism (sine + decay-only pitch env; [Attack Magazine](https://www.attackmagazine.com/technique/synth-secrets/808-style-booms/): "longer decays = booms, shorter = kicks").
- **Tom = conga circuit + a subtle PINK-noise layer** ("artificial reverb") with a *slightly longer* decay than the body, plus an attack **click** ([Baratatronix](https://www.baratatronix.com/blog/808-tom-synthesis)).
- Authentic fundamentals / decays: **low 80–100 Hz / ~200 ms**, **mid 120–160 / ~130 ms**, **high 165–220 / ~100 ms**. Essentially **no inharmonic partial structure** — a single tuned sine (this is what separates the electronic 808 tom from an acoustic tom's Bessel modes).

## Membrum mapping
- **Body = Membrane**, **Exciter = Mallet** (matches the existing `808ElectronicKit` builder). The glide is driven by the **ToneShaper pitch envelope** (Start>End), which the post-audit H-3 fix made work on every body. Low **Material** + non-zero **b3** damping collapse the Bessel modes to a near-pure fundamental → the 808 sine.
- **All acoustic physics OFF:** `airLoading=0`, `modeStretch` neutral, `modeScatter=0`, `modeInject=0`, `nonlinearCoupling=0`, `secondary=off`, and crucially **`tensionModAmt=0`** — tension-mod is an *upward* energy-driven glide and would fight the descending pitch-env sweep. The brief's "physics off / secondary off" is honoured.
- Faint **pink** Noise layer (mix 0.05, longer decay) + faint **Click** (mix 0.05) reproduce the 808 tom's pink-noise "reverb" air and attack tick.

## Hero voice (low-mid tom: 220→80 Hz, 250 ms glide)
| Param (offset) | Norm | Physical |
|---|---|---|
| Exciter (0) | 0.20 | Mallet |
| Body (1) | 0.00 | Membrane |
| Material (2) | 0.22 | low brightness → near-sine |
| Size (3) | 0.80 | f0 ≈ 79 Hz |
| Decay (4) | 0.65 | ~1.7× long ring |
| Level (6) | 0.85 | linear |
| PitchEnv Start (13) | 0.5207 | **220 Hz** |
| PitchEnv End (14) | 0.3011 | **80 Hz** |
| PitchEnv Time (15) | 0.50 | **250 ms** (enables glide) |
| PitchEnv Curve (16) | 0.25 | −0.5 fast-initial drop |
| Noise Mix (42) | 0.05 | faint pink air |
| Noise Color (46) | 0.40 | **Pink** |
| Noise Decay (45) | 0.55 | ~270 ms (> body) |
| Noise Cutoff (43) | 0.40 | ~700 Hz LP |
| Click Mix (47) | 0.05 | faint tick |
| Body Damping b1 (50) | 0.10 | ~5.2 s⁻¹ (long flat decay) |
| Body Damping b3 (51) | 0.10 | 1e-4 (HF kill → sine) |
| Air Loading (52) | 0.00 | OFF |
| Coupling (54) / Secondary (55) | 0.00 / off | no shell |
| **Tension Mod (58)** | **0.00** | **OFF (glide is pitch-env)** |
| Pan (64) | 0.50 | center |

## Size-graded set (6 toms, GM pads 5,7,9,11,12,14)
Grade these across the six toms (small→large is high→low tom):

| | T1 | T2 | T3 | T4 | T5 | T6 |
|---|---|---|---|---|---|---|
| Size (3) | 0.85 | 0.75 | 0.65 | 0.55 | 0.48 | 0.40 |
| Material (2) | 0.18 | 0.25 | 0.32 | 0.40 | 0.50 | 0.60 |
| Decay (4) | 0.65 | 0.58 | 0.50 | 0.43 | 0.35 | 0.28 |
| b1 (50) | 0.10 | 0.15 | 0.20 | 0.25 | 0.32 | 0.42 |
| PitchEnv Start Hz | 220 | 260 | 310 | 370 | 430 | 500 |
| → Start norm (13) | 0.521 | 0.557 | 0.595 | 0.633 | 0.667 | 0.699 |
| PitchEnv End Hz | 80 | 95 | 115 | 140 | 175 | 220 |
| → End norm (14) | 0.301 | 0.338 | 0.380 | 0.423 | 0.471 | 0.521 |
| PitchEnv Time (15) | 0.50 | 0.42 | 0.36 | 0.30 | 0.24 | 0.18 |

(toLogNorm(hz) = ln(hz/20)/ln(100); PitchEnv Time norm × 500 ms.)

## Tweaks
- Want the authentic *subtle* circuit sweep instead of the big boom: shrink the Start→End gap (e.g. 110→90 Hz) and shorten Time toward the real ~tonk.
- For a dirtier, modern boom-glide: add a touch of **Drive (11)** ~0.2 (timbre, not level, post-audit M-2) — taste only.
- Pan the six toms across the field (offset 64) for the classic descending tom-fill sweep.