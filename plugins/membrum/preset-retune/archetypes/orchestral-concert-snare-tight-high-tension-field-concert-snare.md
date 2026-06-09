# Membrum Recipe — Orchestral Concert Snare

**Body:** Membrane (Bessel drumhead) · **Exciter:** NoiseBurst (violet-noise burst)
**Philosophy:** *The snare IS the noise.* A light, tightly-damped ~250 Hz head provides the "tat" weight; a near-max, bright, dry broadband noise layer provides the dominant wire buzz; a sharp click transient provides the orchestral crack. **No pitch glide** (fixed high-tension head).

## Acoustic profile (researched)
- **Fundamental:** high-tension 14" concert head, perceived/(0,1) pitch ~200-400 Hz; we target **~250 Hz** ("thump" 200-300 Hz). [head-fi]
- **Modes:** inharmonic, shifted from ideal Bessel; (0,1) heavily head-coupled/damped → short tonal ring. [ippt.pan.pl / Rossing]
- **Noise:** broadband source; steel/cable wires rattle sympathetically against the reso head. Concert wires are **drier, brighter, more articulate**. Landmarks: thwack 1.5-2.5 kHz, thump 200-300 Hz, sizzle 7-10 kHz→15 kHz+. Harder hits = more HF/noise. [recordingmag / BlackSwamp / head-fi]
- **Transient:** ~1-2 ms stick contact, upper-mid/high.
- **Decay:** controlled/quick, dry tail (a few hundred ms wire buzz). [percussionsource]
- **Pitch glide:** NONE — fixed-tension head (contrast with tom "kerthump").

## Key parameters (normalized → physical)
| Param | Norm | Physical |
|---|---|---|
| Exciter | 0.40 | NoiseBurst |
| Body | 0.00 | Membrane |
| Material | 0.62 | bright/articulate head |
| Size | 0.30 | f0 ≈ 250 Hz |
| Decay | 0.30 | short body ring |
| Strike Pos | 0.35 | r/a ≈ 0.32 |
| Body b1 | 0.62 | 31 s⁻¹ tight flat damping |
| Body b3 | 0.30 | 3e-4, highs ring |
| NoiseBurst Dur | 0.08 | ~3 ms burst |
| **Noise Mix** | **0.98** | wire dominates |
| Noise Cutoff | 0.90 | ~10.5 kHz |
| Noise Color | 0.85 | Violet (bright) |
| Noise Decay | 0.55 | ~265 ms dry |
| Noise Reso | 0.12 | ~Q0.86 flat |
| **Click Mix** | **0.95** | sharp crack |
| Click Contact | 0.18 | ~2.5 ms |
| Click Bright | 0.93 | ~9.2 kHz |
| Drive | 0.06 | ~unity, slight edge |
| Nonlinear Coup | 0.12 | louder=brighter |
| Mode Stretch | 0.40 | mild inharmonic |
| Decay Skew | 0.58 | +0.16 high-mode tilt |
| Mode Scatter | 0.18 | organic detune |
| Air Loading | 0.60 | Rossing default |
| PitchEnv Time | 0.00 | **glide OFF** |
| Tension Mod | 0.00 | **OFF (fixed pitch)** |
| Pan | 0.50 | center |

## Deliberately defaulted
Pitch-env family, Mode Inject, Morph, secondary shell coupling, tension mod, FM/Feedback/Friction exciter params, macros, routing — see defaultedParams. The two big "off" choices are **no pitch glide** and **no secondary shell** (keeps the voice wire-dominated and tom-free).