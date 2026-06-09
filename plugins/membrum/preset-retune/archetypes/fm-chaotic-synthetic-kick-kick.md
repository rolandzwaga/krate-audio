# Membrum Recipe — "FM/Chaotic Synthetic Kick" (kick)

**Body:** Membrane (48-mode Bessel) · **Exciter:** FMImpulse (c:m = 1:2.8) · **Pan:** center

An aggressive, slightly inharmonic synthetic kick: a non-integer FM burst strikes a low membrane sub, with a 300→30 Hz pitch glide, modeScatter detune, an energy-driven tension kerthump, and a coupled metallic secondary shell. Distinct from the clean 808/909 (raised-cosine click + bridged-T sine) and the Buchla feedback kick (self-oscillating Feedback exciter + wavefold).

## Physics & synthesis basis
- **Kick fundamental / glide.** Acoustic kicks ~40–80 Hz; machine kicks <50 Hz. The TR-808 bass drum is a bridged-T resonator at ~49–56 Hz with a ~6 ms attack pitch-rise to ~130 Hz then a slow downward drift over a 50–800 ms decay (center ~300 ms). The pitch drop is the perceptual signature of a kick. → voiced as a **300 Hz → 30 Hz** glide over **75 ms** with a fast (exp) curve. [baratatronix 808-bd-synthesis, sonarworks]
- **Membrane inharmonicity.** Circular-drumhead modes sit at Bessel-zero ratios — (0,1)=1.00, (1,1)=1.59, (2,1)=2.14, (0,2)=2.30, (3,1)=2.65 … i.e. inharmonic. Low airLoading + **modeScatter** keep this slightly detuned/"chaotic". [euphonics, psu Russell, illinois Sorensen]
- **FM excitation.** Non-integer c:m ratios produce inharmonic sidebands (Chowning); ratios past ~1:3 give clangorous metallic percussion. **fmRatio 0.6 → c:m 1:2.8** = a metallic, aggressive strike; velocity drives the FM index 0.5→3.0 rad (louder = brighter clang). [wikipedia FM synthesis, CCRMA drums, nailthemix]
- **Metallic secondary body.** A coupled secondary SHELL (size 0.60 → ~0.55×f0, material 0.85 → bright, long inharmonic ring) adds the "metallic coupling" on top of the membrane sub. [Membrum audit Phase 8D]
- **Attack click / noise.** Kick click ~3–8 kHz (beater); noise bed is dark low-mid "air", not hiss. [gear4music, idrumtune]

## Key denormalized targets
| Param | Norm | Physical |
|---|---|---|
| Exciter | 0.80 | FMImpulse |
| Body | 0.00 | Membrane |
| Size | 0.85 | f0 ≈ 71 Hz (pre-glide) |
| FM Ratio | 0.60 | c:m = 1:2.8 (inharmonic) |
| PitchEnv Start→End | 0.588 → 0.088 | 300 Hz → 30 Hz |
| PitchEnv Time / Curve | 0.15 / 0.12 | 75 ms / fast-drop exp |
| Mode Scatter | 0.30 | ~4.5% freq dither |
| Tension Mod | 0.50 | energy glide up to +3 st |
| Secondary Size / Material | 0.60 / 0.85 | shell ≈0.55×f0, metallic |
| Coupling Strength / Enabled | 0.45 / 1.0 | shell driven, on |
| Noise Mix / Color / Cutoff | 0.25 / 0.15 / 0.40 | brown, ~580 Hz LP |
| Drive / Nonlinear Coupling | 0.30 / 0.35 | grit + velocity bite |
| Decay / b1 / b3 | 0.42 / 0.18 / 0.25 | ~150–250 ms, weighty sub, tamed highs |

Full per-param values, physical targets, and rationale are in the `params` array; deliberate defaults are in `defaultedParams`.