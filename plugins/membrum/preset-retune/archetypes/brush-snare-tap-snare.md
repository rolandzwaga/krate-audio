# Membrum Recipe — "Brush Snare (tap)" (snare)

**Body:** Membrane (Bessel circular drumhead, 48 modes) · **Exciter:** NoiseBurst (~5 ms soft contact)

The tapped companion to the brush-sweep snare: a soft filtered-noise contact (brush strands spread the hit) drives a woody, shell-coupled membrane, with moderate wire-noise (~3.3 kHz, white, short decay) and a soft mid-bright click. A shallow 210→150 Hz pitch-env plus light tension-mod give the housing "thump" that separates a snare from a hat.

## Physics → why these choices
- **Pitch / body band:** snare "pulse/body" lives at 200–400 Hz; "smack/crack" 0.9–2 kHz; **snare-wire buzz 3–5 kHz**; head-contact 6–10 kHz (Recording Magazine; Sonicbids). Size 0.60 (head f0 ≈126 Hz) + shell coupling + 210→150 Hz pitch-env place the perceived body at the low end of that band.
- **Inharmonic modes:** circular-membrane ratios are 1.000, **1.593**, 2.136, 2.296, 2.653, 2.918, 3.598 — NOT integer multiples (Penn State / D. Russell; UBC PHYS341). Membrane body + airLoading (0.45 → toward Rossing 1:1.5:2:2.4) + modeScatter (0.38) + coupled shell carry this. **Mode Inject is deliberately OFF** — a synthetic 1/k harmonic series would harmonicize an inharmonic head.
- **Brush tap vs stick:** brushes are "softer, warmer, more subdued" with a reduced HF transient (Andertons; DrumRadar; Yamaha). → NoiseBurst exciter (diffuse contact, not impulse), Click Mix only 0.62 (vs ~0.92 stick), click brightness ~2.4 kHz (rolled off vs 6–10 kHz stick tick), Noise Mix 0.65 (vs ~0.82 stick).
- **Wires:** modeled as filtered noise with its own short envelope (Nord Modular ch.5 / TR-909). Cutoff ~3.3 kHz (3–5 kHz band), white, decay ~113 ms — "snapped sound that dies quickly on contact" (DrumRadar).
- **Kerthump:** energy-driven upward membrane stiffening (Avanzini–Marogna–Bank 2012) via tensionModAmt 0.25, plus a programmed 210→150 Hz / 60 ms exp drop.

## Key normalized baseline (all values normalized [0,1])
| Param | Norm | Physical |
|---|---|---|
| Exciter Type | 0.40 | NoiseBurst |
| Body Model | 0.00 | Membrane |
| Material | 0.40 | woody, brightness 0.40 |
| Size | 0.60 | head f0 ≈126 Hz |
| Decay | 0.58 | medium body ring |
| Strike Position | 0.30 | r/a≈0.27 (center-ish) |
| Level | 0.95 | linear make-up |
| Filter Type / Cutoff / Res | 0 / 0.78 / 0.15 | LP ≈5.5 kHz, Q≈2.1 |
| Filter Env Amt / Dec | 0.65 / 0.385 | +0.3 (≈+0.9 oct) / ≈114 ms |
| Drive | 0.22 | ≈3× recipSqrt, warmth |
| PitchEnv Start/End/Time/Curve | 0.5106 / 0.4375 / 0.12 / 0.15 | 210→150 Hz / 60 ms / fast-drop |
| Nonlinear Coupling | 0.18 | louder=brighter |
| Noise Mix/Cutoff/Res/Decay/Color | 0.65 / 0.72 / 0.20 / 0.28 / 0.60 | wires ≈3.3 kHz, ~113 ms, white |
| Click Mix/Contact/Bright | 0.62 / 0.14 / 0.60 | soft tap, 2.42 ms, ≈2.4 kHz |
| Body Damping b1 / b3 | 0.28 / 0.04 | 14.1 s⁻¹ / 4.0e-5 s |
| Air Loading / Mode Scatter | 0.45 / 0.38 | Rossing morph / dither |
| Coupling Str / Sec En / Sec Size / Sec Mat | 0.62 / 1.0 / 0.68 / 0.48 | shell ≈0.49·head f0 |
| Tension Mod | 0.25 | ≈+2 st energy glide |
| Coupling Amount | 0.65 | sympathetic snare buzz |
| Pan | 0.50 | center |

This matches the corrected post-audit semantics (measured-strike body norm / N-1, per-mode decaySkew, mode_inject 1/k left unused, env-level NonlinearCoupling, Drive=flavour, M-9 pan) and is the same voicing family as the existing `jazzBrushesKit()` pad 4 in `tools/membrum_preset_generator.cpp`.

## Sources
Penn State circular membrane modes · UBC PHYS341 snare · Recording Magazine snare guide · Sonicbids snare guide · Nord Modular percussion ch.5 · Andertons / DrumRadar brush guides · Yamaha tonewoods · Madsen (Illinois PHYS406) bottom-head snare report.