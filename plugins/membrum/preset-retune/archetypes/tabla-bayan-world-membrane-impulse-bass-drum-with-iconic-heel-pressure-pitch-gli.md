# Membrum Recipe — Tabla Bayan (world)

**Body:** Membrane (Bessel drumhead) · **Exciter:** Impulse
**Pad fundamental:** size 0.72 → **95.2 Hz** (measured bayan F0 80–100 Hz)
**Signature:** highest `tensionMod` in the library (0.78) for the heel-pressure pitch gliss.

## Why this body & exciter
The bayan is a single goatskin membrane, weighted off-centre by the iron-loaded **syahi**, stretched over a clay/metal pot. Only **Membrane** carries the Bessel mode shapes, the **air-loading** frequency correction, and the **Membrane-only tension-modulation pitch glide** — the defining bayan feature. **Impulse** is the canonical in-phase hand strike; a low Click layer adds the soft finger contact, a low Pink-noise layer the skin/air slap.

## Acoustic targets (researched)
- **F0 80–100 Hz** (Ashok & Tiwari 2025; UBC: ghe ~100 Hz). → size 0.72 = 95 Hz.
- **Near-HARMONIC** spectrum from the syahi composite-membrane loading (Raman 1920; Ramakrishna & Sondhi): ratios ≈1:2:3… → keep Mode Stretch **neutral** (0.333), no inharmonicity. Asymmetry → 2nd-harmonic splitting / 3rd-harmonic degeneracy → small Mode Scatter (0.10).
- **Strong air loading** over the pot depresses low modes (Tiwari & Gupta 2017) → Air Loading 0.62.
- **Heel/wrist pitch gliss** (Zakir Hussain; 'push then release'): energy-dependent upward bend that relaxes as the strike decays (Avanzini-Marogna-Bank 2012). → **tensionMod 0.78** (~1.5–2 st at hard-hit peak, clamped ≤+3 st). Plus a short 180→70 Hz / 100 ms attack thump.
- **Resonant (Ge) = sustained** single ~100 Hz harmonic; **Damped (Ka) = diffuse, fast-decaying, brighter** (researchgate / arXiv:1510.04880).

## The 'Ge' resonant open bass (canonical pad — baseline below)
Membrane+Impulse · size 0.72 · material 0.30 · **decay 0.78 / b1 0.30** (long ring) · decaySkew 0.65 (highs die first) · airLoading 0.62 · **tensionMod 0.78** · pitchEnv 180→70 Hz / 100 ms · secondary pot-shell on (size 0.45→~63 Hz) · click 0.45 (dull 2.5 ms) · noise 0.10 pink · pan 0.42 (left).

## The 'Ka' damped stroke (variant from the Ge pad)
Same body. Change: **decay 0.10–0.20**, **b1 ↑ (~0.55→tighter)**, **tensionMod 0.20** (little gliss), **decaySkew 0.30**, **Click Mix 0.85 / brightness 0.65** (brighter, more transient slap), strikePosition ~0.20 (toward edge). Result: short, diffuse, brighter damped transient — exactly the measured Ka/Ke profile.

## Deliberate defaults
Filter bypassed (cutoff 1.0), Drive/Fold off (clean low harmonic), Mode Inject off (loaded membrane already harmonic), Morph off (tension carries motion), all macros neutral, pitch-env Knee off. See defaultedParams for physical reasons.

## Sources
Ashok & Tiwari 2025 (ScienceDirect/SSRN, bayan tuning & bols); chandrakantha psychoacoustics + pitch-modulation archive; Tiwari & Gupta 2017 (air loading); arXiv:1510.04880 / researchgate (Harmonic & Timbre Analysis of Tabla Strokes); UBC Phys341 Tabla; Wikipedia/Britannica Tabla; arXiv:math-ph/0001030 (Acoustics of the Indian Drum).