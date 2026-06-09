# Membrum Recipe — "Feedback Shell Snare" (experimental metallic / self-oscillating snare)

**Body:** Shell (free-free Euler-Bernoulli bar — inharmonic, metallic)  
**Exciter:** Feedback (self-oscillating body↔exciter loop, feedbackAmount 0.40)  
**Character:** A ringing, self-oscillating metallic snare. The inharmonic bar spectrum sits up in the 2–8 kHz crack/wire band; the Feedback exciter sustains it into a metallic squeal, a strong noise layer supplies the wire "shhh", a sharp click supplies the stick crack, and a subtle 230→160 Hz chirp adds electronic-drum flavor.

## Acoustic basis (researched)
- **Body resonance:** real 14" snare ~170–215 Hz fundamental, edge overtone ~300–350 Hz ([idrumtune](https://www.idrumtune.com/mixing-drums-know-your-drum-frequencies/); [Physics-406 modal study](https://courses.physics.illinois.edu/phys406/sp2017/Student_Projects/Spring14/Matthew_Fischer_Physics_406_Final_Project_Sp14.pdf)). This preset deliberately pushes the **metallic** ring up: Shell @ Size 0.55 → f0 ≈ **423 Hz**, partials 423/1166/2286/3779/5645/7883 Hz (free-free bar ratios 1, 2.757, 5.404, 8.933…; Fletcher & Rossing).
- **Crack:** broadband 2–8 kHz stick transient → Click layer (mix 0.85, ~2.2 ms, ~7.1 kHz).
- **Wires:** high-passed white noise, 3–5 kHz with air to 7–10 kHz ([idrumtune](https://www.idrumtune.com/mixing-drums-know-your-drum-frequencies/); [TR-808 HPF white-noise generator](https://en.wikipedia.org/wiki/Roland_TR-808)) → Noise layer (mix 0.70, White, LP ~5 kHz, ~100 ms).
- **Decay:** shell modes can ring T60 ~1.5 s but perceived envelope is short (~150–300 ms) → b1 14.1 s⁻¹ + low b3 (metal = long highs).
- **Pitch:** real snares have negligible glide ([Sound On Sound](https://www.soundonsound.com/techniques/practical-snare-drum-synthesis)); the 230→160 Hz/60 ms chirp is an 808/909-style electronic stylization.
- **Synthesis precedent:** 808/909 snare = two metallic tone oscillators (~180+330 Hz) + HPF white noise — Shell+Feedback metallic ring + noise layer is the physical-model analog.

## Key normalized baselines
| Param | Norm | Physical |
|---|---|---|
| Exciter | 1.0 | Feedback |
| Body | 0.4 | Shell |
| Material | 0.55 | brightness ~0.93 |
| Size | 0.55 | f0 ≈ 423 Hz |
| Feedback Amount | 0.40 | self-osc floor (≤0.85 stable) |
| Mode Scatter | 0.42 | ~6% detune of bar ratios |
| Nonlinear Coupling | 0.22 | louder=brighter |
| Coupling Strength / Secondary | 0.65 / on | metallic secondary @ ~217 Hz, mat 0.70 |
| Drive | 0.30 | odd-harmonic flavour |
| Body Damping b1 / b3 | 0.28 / 0.04 | 14.1 s⁻¹ / 4e-5 (long metallic highs) |
| Noise Mix/Color/Cutoff/Decay | 0.70 / White / 0.80 / 0.32 | wire shhh ~5 kHz, ~100 ms |
| Click Mix/Contact/Bright | 0.85 / 0.08 / 0.85 | crack ~2.2 ms @ ~7.1 kHz |
| PitchEnv Start/End/Time/Curve | 230 Hz / 160 Hz / 60 ms / -0.7 | fast-exp chirp |
| Filter LP cutoff/res/env | 0.80 / 0.20 / +0.4, dec 114 ms | open-then-close sweep |
| Pan | 0.5 | center |

(Full per-param values, denormalizations, and rationales are in the `params` array; intentionally-defaulted params with reasons are in `defaultedParams`.)

## Notes
- **Inert-but-stored** for this archetype: Air Loading (membrane-only), Tension Mod (membrane-only), FM Ratio / NoiseBurst Duration / Friction Pressure (other-exciter-only). Carried from the FX-snare precedent; harmless.
- All values are voiced against the **post-audit corrected** chain (N-1 measured-strike body norm, M-2 Drive makeup, M-3/M-4 env-level NonlinearCoupling, M-5 per-mode skew, M-9 pan, gain-staging appendix).