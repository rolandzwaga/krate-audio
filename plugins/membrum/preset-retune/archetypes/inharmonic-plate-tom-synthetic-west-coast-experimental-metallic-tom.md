# Membrum Recipe — Inharmonic Plate Tom (synthetic west-coast / experimental metallic tom)

A struck **Plate** body used as a clangy, inharmonic, metallic tom — the deliberate antithesis of the near-harmonic membrane tom. FMImpulse seeds a bell-like inharmonic strike; modeStretch + nonlinearCoupling + decaySkew push the clang; a coupled metallic secondary shell adds a second inharmonic ring; the tom "kerthump" glide is delivered by the pitch envelope (because Plate cannot use tensionModAmt).

## Body & exciter
- **Body:** Plate (`Body Model` norm **0.2** → idx 1). Free-plate Chladni `f_mn = C·(m+2n)^1.7` inharmonic 2-D spectrum (post-audit corrected `kPlateRatios`: 1.00, 1.11, 1.99, 2.21, 3.25, 3.61, 3.96, 4.75…). No integer harmonics — this irregular spectrum IS the archetype.
- **Exciter:** FMImpulse (`Exciter Type` norm **0.8** → idx 4). Chowning bell-FM transient. Alt: Feedback (norm 1.0) for an aggressive self-oscillating variant; Mallet (norm 0.2) for a softer beater.

## Physics → params (all values NORMALIZED [0,1])
| Param | Norm | Denorm / physical target | Why |
|---|---|---|---|
| Material | 0.82 | brightness 0.91, plate decay base ≈1.71 s | very metallic, long HF ring |
| Size | 0.65 | f0 = 800·0.1^0.65 ≈ **179 Hz** | plate fundamental in the tom register |
| Decay | 0.45 | ≈1.4 s body decay | medium metallic ring (longer than a damped head) |
| Strike Position | 0.62 | rho≈0.71, theta≈0.97 rad (mid-edge) | excites many inharmonic modes → clang |
| FM Ratio | 0.18 | modulator ratio **1.54** (near 1.4 Chowning bell) | inharmonic metallic FM strike |
| Mode Stretch | 0.6 | physical 1.4 (B up to 0.6·0.01) | widens partials → more bell/metallic dispersion |
| Decay Skew | 0.38 | bipolar −0.24 (boost high modes) | keeps clang in the tail |
| Nonlinear Coupling | 0.5 | env-level recipSqrt drive | hit harder = brighter/clangier (struck-metal nonlinearity) |
| Drive | 0.12 | internalDrive ≈2.08 | odd-harmonic grit (flavour, M-2 makeup) |
| Fold | 0.18 | 0.57 rad sine fold | west-coast added inharmonics |
| Body Damping b1 | 0.4 | ≈20.1 s⁻¹ | RT60 floor (medium ring) |
| Body Damping b3 | 0.18 | 1.8e-4 s (low f² damping) | highs ring = metal not wood |
| Mode Scatter | 0.12 | ~1.8% dither | organic plate imperfection |

### Pitch glide ("kerthump") — via pitch envelope, NOT tensionModAmt
**tensionModAmt (offset 58) is Membrane-only and a no-op on Plate.** The tom glide is delivered by the ToneShaper pitch env (post-audit H-3 drives all bodies):
| Param | Norm | Target | Why |
|---|---|---|---|
| PitchEnv Start | 0.62 | 332 Hz | starts above settle → downward bend |
| PitchEnv End | 0.5 | 200 Hz (≈ body f0) | settle near natural |
| PitchEnv Time | 0.09 | **45 ms** | fast = high-tension tom (less glide, faster) |
| PitchEnv Curve | 0.2 | −0.6 (fast initial drop) | classic 808/tom exp pitch drop |

### Metallic secondary (coupled shell)
| Param | Norm | Target |
|---|---|---|
| Secondary Enabled | 1.0 | 24-mode shell on |
| Coupling Strength | 0.4 | feedforward shell drive |
| Secondary Size | 0.4 | shell f0 ≈ 0.70·f0 ≈ 125 Hz |
| Secondary Material | 0.75 | bright/metallic shell |

### Layers (tonal archetype — noise low, click bright)
Noise Mix 0.12 / Cutoff 0.7 (3.7 kHz LP) / Reso 0.2 / Decay 0.2 (39 ms) / Color 0.65 (White). Click Mix 0.4 / Contact 0.25 (2.75 ms) / Brightness 0.7 (3.4 kHz). Filter: LP, Cutoff 0.86 (8.9 kHz soft ceiling), FilterEnvAmount 0.42 (−0.16, slight downward sweep so highs die first).

### Left at default (coverage)
Material Morph (off), Filter Env ADSR (env amount tiny), PitchEnv Knee/Mid (single segment suffices), Feedback/NoiseBurst/Friction secondary (no-op for FMImpulse), all 5 Macros (neutral 0.5 — baked values set directly), Coupling Amount (kit-level), **Tension Mod 0 (no-op on Plate)**, Air Loading (Membrane-only no-op), Pan 0.5 (center), Pad Enabled 1.0.

## Key sources
- Chladni's law / cymbal (m+2n)^P: en.wikipedia.org/wiki/Chladni's_law; auditory.org ASA cymbal modes
- Plate HF decays faster (b3·f²): ScienceDirect "On the quality of plate reverberation"; Valhalla DSP plates
- Tom pitch glide / kerthump: JASA 150:202 (Kirby & Sandler 2021); Baratatronix 808 tom synthesis
- FM metallic ratios: synthtopia FM metallic sounds; reverbmachine DX7
- Tom tuning range: iDrumTune; DrumForum