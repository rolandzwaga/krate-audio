# Membrum Recipe — Metal-Bell Hi-Hat (FX)

**Archetype:** Experimental metallic hat — a morphing inharmonic-**Bell** body driven by a **NoiseBurst** contact, with a bright violet-noise **sizzle overlay**. The FX kit's aggressive metallic take on the hi-hat. Closed / pedal / open differentiated ONLY by the **Decay** knob.

> All values below are **normalized [0,1]** (the on-wire/preset representation). Each lists the physical target it denormalizes to.

## Body & Exciter

- **Body = Bell** (norm 0.8 → idx 4). Tuned church-bell Chladni bank (16 modes): hum 0.25·f0 / prime 0.5 / tierce 0.6 / quint 0.75 / nominal 1.0, then 1.5 … 12×. Gives a *tuned metallic clang* (a perceived pitch) — the "metal-bell" flavour — versus NoiseBody's pure cymbal hiss.
- **Exciter = NoiseBurst** (norm 0.4 → idx 2). Violet (+6 dB/oct) noise bandpass burst = the broadband, HF-tilted "stick-on-metal" contact that lights all Bell modes in-phase for a bright clangorous onset.

## Why this maps to the real instrument

Real hats/cymbals are strongly **inharmonic, high-entropy**: lowest energy ~160–500 Hz, dense partials 1k→>5k (the metal "ring"), perceptually dominated by HF **sizzle**. There is **no pitch glide** (that's a membrane/tom effect). Closed vs open is a **decay-length** difference: TR-808 reference is closed ~50 ms, open 90–600 ms, full cymbal 350–1200 ms. A bell's strike evolves in three phases (bright clangorous strike → settled strike-note → decay tail) — captured here by **Material Morph 0.95→0.3** over ~200 ms.

## Pitch (Size 0.1)

Bell nominal f0 = 800·0.1^0.1 ≈ **635 Hz** → hum ≈ 159 Hz, prime ≈ 318 Hz, nominal ≈ 635 Hz, upper partials to ~7.6 kHz. Small, high, bright metal body in the hat register; the noise layer extends brightness above it.

## Baseline parameters (normalized)

| Param | Norm | Physical target | Why |
|---|---|---|---|
| Exciter Type | 0.40 | NoiseBurst | metal-contact noise burst |
| Body Model | 0.80 | Bell | tuned inharmonic metal partials |
| Material | 0.95 | brightness ≈0.985 | peak metallic; = morph START |
| Size | 0.10 | f0 ≈635 Hz nominal | high small metal body |
| **Decay** | **0.08** | ~0.34× ring → **CLOSED ~50–80 ms** | **closed; pedal=0.04, open=0.60** |
| Strike Position | 0.30 | azimuth ≈27° | mild meridional partial weighting |
| Level | 0.70 | linear 0.7× | kit balance |
| Noise Mix | 0.55 | ~−18 dBFS sizzle | HF hat hiss overlay |
| Noise Cutoff | 0.90 | LP ≈9.8 kHz | bright sizzle |
| Noise Color | 0.85 | Violet | HF-rising metal noise |
| Noise Decay | 0.30 | ≈86 ms | tracks closed body |
| Noise Resonance | 0.20 | q≈1.24 | mild metallic edge |
| Mode Scatter | 0.45 | ~7% detune | organic inharmonic spread |
| Body Damping b3 | 0.00 | no f² roll-off | metal-long highs |
| Body Damping b1 | 0.40 | ≈20 s⁻¹ floor | consistent metallic decay floor |
| Morph Enabled | 1.00 | ON | strike→body evolution |
| Morph Start | 0.95 | Material 0.95 | bright clangorous attack |
| Morph End | 0.30 | Material 0.30 | settles to stronger low partials |
| Morph Duration | 0.0955 | ≈200 ms | strike-settle window |
| Choke Group | 0.125 | group 1 | closed/pedal/open mute each other |
| Click Mix | 0.00 | click off | NoiseBurst owns the attack |
| PitchEnv Time | 0.00 | disabled | hats have no glide |
| Filter Cutoff | 1.00 | 20 kHz = bypass | body+noise already shape it |
| Drive Amount | 0.00 | bypass | keep the inharmonic clang clean |
| Pan | 0.50 | center | neutral placement |

## Closed / Pedal / Open variants (change Decay + Noise Decay only)

| Variant | Decay | Noise Decay | Result |
|---|---|---|---|
| **Closed** | 0.08 | 0.30 | ~50–80 ms tick |
| **Pedal** | 0.04 | (track) | tightest chick |
| **Open** | 0.60 | ~0.55 | long sizzling ring |

All three share Choke Group 1 (an open hat is cut by a following closed/pedal hit).

## Deliberately defaulted (no-ops or neutral for this archetype)

Pitch-env sub-params (Time=0), Mode Stretch / Decay Skew (Bell ratios + scatter already inharmonic), Mode Inject & Nonlinear Coupling (exact-bypass at 0 — would add a tonal body a hat lacks), Air Loading (membrane-only no-op), all secondary/coupling params (a hat is a single thin body), Tension Mod (membrane-only, and no glide wanted), all five macros (neutral 0.5 so explicit voicing is preserved), FM Ratio / Feedback / Friction Pressure (wrong-exciter no-ops), filter-env & fold (filter bypassed).

## Sources

- Hi-hat physical modeling — [jstage AST 44(5)](https://www.jstage.jst.go.jp/article/ast/44/5/44_E2293/_pdf/-char/en)
- TR-808 hat/cymbal synthesis (osc freqs, BP/HP, decay envelopes) — [Baratatronix](https://www.baratatronix.com/blog/cascadia-808-cymbal-hi-hat-synthesis)
- Synth-hat recipe (inharmonic osc 2–5 kHz through HP + noise) — [ModeAudio](https://modeaudio.com/magazine/massive-drum-design-part-3-hi-hats)
- Hat spectral band / mixing — [Range of Sounds](https://rangeofsounds.com/blog/mixing-hi-hats/)
- Bell inharmonic partials & 3-phase decay — [Sound on Sound](https://www.soundonsound.com/techniques/synthesizing-bells)
- Church-bell partials & Chladni law — [Researchgate](https://www.researchgate.net/publication/276049531_Partial_Frequencies_and_Chladni's_Law_in_Church_Bells), [Open U](https://oro.open.ac.uk/40358/7/40358.pdf), [Hibberts](https://www.hibberts.co.uk/tubular-bells/)
- Cymbal Chladni (m+2n)^P inharmonicity — [oemcymbal](https://oemcymbal.com/understanding-cymbal-vibration-modes/)