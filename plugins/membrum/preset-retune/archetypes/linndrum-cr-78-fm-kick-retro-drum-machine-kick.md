# Membrum Recipe — "LinnDrum/CR-78 FM Kick" (Kick)

**Body:** Membrane (Bessel drumhead, 48 modes) · **Exciter:** FMImpulse (modulatorRatio 1.90)

A retro drum-machine kick. The Membrane carries a tuned low fundamental; the ToneShaper pitch envelope rescales every mode for the iconic 808-style downward sweep (160 → 50 Hz). The FMImpulse exciter (fmRatio 0.30 → 1.90 inharmonic ratio) plus a light sine fold (0.18 → 0.565 rad) inject a gritty, sample-flavoured inharmonic edge that distinguishes it from a pure-sine 808/909. Material 0.20 keeps the body dark so that grit reads as "sampled texture", not "FM bell". All physics modelling axes are deliberately off.

> All values below are NORMALIZED [0,1] (the on-wire / preset representation), with the physical target each denormalizes to.

## Research-derived acoustic / synthesis targets
- **Fundamental:** settle ~50 Hz (TR-808 BD self-oscillates a bridged-T network at ~49.4 Hz, real units 48 Hz). Acoustic kicks 40–80 Hz; machine kicks lower.
- **Pitch glide:** 808 pushes to ~130 Hz at the attack peak then falls to ~49 Hz in ~6 ms — the defining "kerthump". Recipe: 160 → 50 Hz over ~20 ms (longer so the FM-flavoured glide is heard, not just clicked).
- **Partials:** 808 BD ≈ one decaying sine (single resonant pole); LinnDrum = a SAMPLE of an acoustic kick (35 kHz, companded → grit). FMImpulse @1.90 supplies controlled inharmonic dirt to mimic the sampled character.
- **Decay (T60):** retro kicks are tight — 808 50–800 ms (300 ms noon), CR-78 ~100–200 ms. Recipe targets a short body ring (~0.41× base) + b1≈19 s⁻¹.
- **Click/noise:** 808 click = trigger pulse + pitch overshoot; near-zero broadband noise. Recipe: short dull click (~2.4 ms, ~830 Hz) + whisper noise (mix 0.06).

## Core
| Param | Norm | Physical |
|---|---|---|
| Exciter Type | 0.80 | FMImpulse |
| Body Model | 0.00 | Membrane |
| Material | 0.20 | dark body, baseDecay ≈0.28 s |
| Size | 0.72 | natural f0 ≈95 Hz |
| Decay | 0.28 | ≈0.41× multiplier (tight) |
| Strike Position | 0.30 | r/a 0.27 |
| Level | 0.85 | linear 0.85 |

## Pitch glide (the kerthump)
| Param | Norm | Physical |
|---|---|---|
| PitchEnv Start | 0.4516 | 160 Hz |
| PitchEnv End | 0.1990 | 50 Hz |
| PitchEnv Time | 0.04 | 20 ms |
| PitchEnv Curve | 0.15 | −0.7 (fast 808-style drop) |

## Grit / exciter
| Param | Norm | Physical |
|---|---|---|
| FM Ratio | 0.30 | modulatorRatio 1.90 |
| Fold Amount | 0.18 | 0.565 rad sine fold |

## Damping & attack
| Param | Norm | Physical |
|---|---|---|
| Body Damping b1 | 0.38 | 19.1 s⁻¹ flat floor |
| Body Damping b3 | 0.30 | 0.30e-3 s (mild f² damping) |
| Click Mix | 0.40 | beater/trigger definition |
| Click Contact | 0.15 | 2.45 ms |
| Click Brightness | 0.45 | ~830 Hz (dull thud) |
| Noise Mix | 0.06 | whisper of sampler grit |
| Filter Cutoff | 1.00 | bypassed |

## Macros & placement
| Param | Norm | Physical |
|---|---|---|
| Macro Punch | 0.55 | deeper/faster attack snap |
| Macro Body Size | 0.45 | slightly tighter |
| Pan | 0.50 | centre |
| Air Loading | 0.00 | physics axis off |

## Deliberately defaulted (physics/character axes off)
Drive (0), Mode Stretch (0.333 neutral), Decay Skew (0.5 neutral), Mode Inject (0), Nonlinear Coupling (0), Mode Scatter (0), Tension Mod (0), all Secondary/Coupling (off), Morph (off), PitchEnv Knee (off), Filter Type/Res/Env (filter bypassed), Tightness/Brightness/Complexity macros (0.5 neutral), Choke/Bus (0). Each is off because this is a stylised electronic kick — the body+FM+fold+pitch-env already supply all the character, and the physics-modelling axes would either fight the deliberate pitch glide (tensionMod), muddy the sub (scatter/stretch), or add no value (coupling/morph).

**Note:** this archetype already exists in `tools/membrum_preset_generator.cpp` as `linnDrumKit()` pad[0] (line ~1997) with matching values (fmRatio 0.30, material 0.20, fold 0.18, pitchEnv 160→50, physics off) — this recipe documents and lightly rationalises that build against the corrected post-audit semantics.

## Sources
- TR-808 BD synthesis (bridged-T, 49.4 Hz, 130 Hz/6 ms sweep, 50–800 ms decay): baratatronix.com/blog/808-bd-synthesis
- Werner et al., physically-informed 808 BD circuit model (ResearchGate)
- CR-78 analog voices / decay: en.wikipedia.org/wiki/Roland_CR-78
- LinnDrum sampling (Art Wood, 35 kHz): vintagesynth.com/linn-electronics/linndrum; oramics LM-2
- Kick fundamental range: sonarworks.com; idrumtune.com