# Membrum Recipe — "Algorithmic FX Voice (multi-body cycle)" (synthetic-fx)

> A fully algorithmic experimental archetype. There is **no real-world drum** to model — the design goal is a broad palette of inharmonic synthetic hits and textures for sound design. The existing `chaosEngineKit` (membrum_preset_generator.cpp:3387) is the canonical implementation; this recipe formalizes it and adds the one thing that makes it unique: **modeInject = 0.2**.

## Body & Exciter
- **Body model:** i-indexed cycle `bodyCycle[i%6]` = Bell → String → Shell → Plate → Membrane → NoiseBody. A different resonator topology per pad.
- **Exciter:** i-indexed `Feedback (i%3==0) / FMImpulse (i%3==1) / Friction (i%3==2)`. Alternating excitation backends are a primary variety axis.

## What makes this archetype unique
1. **The ONLY archetype using `modeInject` (0.2).** Lays a clean **1/k** (−6 dB/oct sawtooth) 8-partial harmonic series at body f0 under the *inharmonic* modal/FM body → an overtly synthetic hybrid spectrum with no acoustic referent. (Default fixed from 1/k² to 1/k in the audit.)
2. **i-indexed parametric ramps** over modeStretch, nonlinearCoupling, fmRatio/feedbackAmount, modeScatter, tension, material, size, decay — so the 14 crafted pads span a palette rather than one sound.
3. **Inharmonicity stacked from three independent mechanisms:** FM non-integer ratios (1.6–3.5), stretched modal partials (`B` up to 0.01, corrected `(k+1)` index), and heavy modeScatter (~13% dither).

## Synthesis basis (cited)
- **FM bell inharmonicity** — sidebands at C±nM; irrational C:M → metallic/inharmonic; index sets brightness. Chowning 1973. ([CCRMA FM2](https://ccrma.stanford.edu/software/clm/compmus/clm-tutorials/fm2.html), [Wikipedia](https://en.wikipedia.org/wiki/Frequency_modulation_synthesis))
- **Feedback / self-oscillation percussion** — resonant element on the verge of self-oscillation rings/squeals; how classic drum synths made FX hits. ([lfusionmodular](https://www.lfusionmodular.com/module-combinations/analog-drums-percussive-synthesis/))
- **Modal inharmonicity & non-physical FX** — `f_k ∝ k[1+(k−1)B]`, `R_k = b1+b3·f_k²`, `sin(πkx)` excitation, 909-style downward sweeps, artificial sum/difference partials. ([Nathan Ho — Exploring Modal Synthesis](https://nathan.ho.name/posts/exploring-modal-synthesis/))

## Post-audit semantics honored
- Drive = flavour (M-2 unity makeup), NonlinearCoupling = amplitude-driven brightening (M-3/M-4 redesign), pitch-env active on ALL bodies (H-3), per-mode decaySkew tilt on all bodies (M-5), mode_inject = 1/k (§3-B), tensionMod = Membrane-only energy glide (N-1), per-pad pan (M-9), body bounded by measured-strike norm (N-1). All FX ratios in §3-A are correct and left untouched.

## Baseline (normalized) — representative mid-of-ramp values
See the `params` array for every meaningful param with its physical target, i-ramp formula, and citation. Headline values:

| Param | Norm | Physical | Note |
|---|---|---|---|
| Exciter | i-cycle | Feedback/FMImpulse/Friction | alternation |
| Body | i-cycle | Bell/String/Shell/Plate/Membrane/NoiseBody | all six |
| **Mode Inject** | **0.2** | 1/k series at 20% | **only archetype using it** |
| Mode Stretch | 0.40–0.88 | phys 1.1–1.82 (inharmonic) | stretched |
| Mode Scatter | 0.55–0.87 | ~13% dither | chaotic detune |
| Nonlinear Coupling | 0.65–0.85 | amplitude brightening | von Kármán-style |
| Fold | 0.20–0.56 | 0.63–1.76 rad | west-coast harmonics |
| FM Ratio | 0.20–0.83 | C:M 1.6–3.5 | FM pads only |
| Feedback | 0.40–0.76 | drive·0.85 | Feedback pads only |
| Friction Pressure | 0.30–0.60 | bow pressure | Friction pads only |
| Tension Mod | 0.78–0.93 | up to +3 st | Membrane pads only |
| PitchEnv | 180→40 Hz / 20–100 ms | downward chirp | all bodies |
| Filter | LP/HP/BP cycle, Q≈6–8.5 | resonant SVF | engaged |
| Macro Complexity | 0.85 | +coupling/nonlin/inject | reinforces density |

## Defaulted (with reason)
See `defaultedParams`: Strike Position, Morph (opt-in), PitchEnv knee/mid, Pan (center), and the non-Complexity macros — each left at default because the body/exciter cycle + explicit ramps already supply the variety, or the param is a no-op for most pads in the cycle.