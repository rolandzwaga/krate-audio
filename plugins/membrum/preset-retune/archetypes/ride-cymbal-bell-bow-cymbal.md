# Membrum Recipe — Ride Cymbal (bell/bow)

**Archetype:** cymbal / ride (bell-tone ping + sustained shimmer wash)
**Body:** Bell (church-bell Chladni bank, 16 tuned partials, hum at 0.25·f0)
**Exciter:** NoiseBurst (sharp filtered-noise stick contact)
**Output bus:** 1 (multi-out cymbal send)

## Why this maps the way it does

A ride is metallic and inharmonic, but — unlike a crash — it has a **defined tonal "ping"** when struck near the cup, plus a **sustained broadband shimmer wash**. Measured ride content: the bell cup gives ~280 Hz with strong components at ~700 Hz and ~2400–2500 Hz; the ride primary tone sits ~300–600 Hz; upper sheen runs 4–6 kHz and beyond 20 kHz (Musical-U; Sound on Sound). Low/mid ping partials stay near full level after 2 s while the >8 kHz air fades first, so the ping survives hard riding (Musical-U).

This is built as: **Bell body** = the tuned ping skeleton; **high modeScatter + mild modeStretch** = breaks the too-ordered bell ratios into the real ride's dense inharmonic "fog" (Fletcher & Rossing ch.20); **b3 = 0 with low b1** = metallic, multi-second ring where the highs ring as long as the lows; **Noise layer** (violet, bright, long decay) = the shimmer wash; **Click layer** (bright, ~6 kHz, short) = the stick ping tick; **mild NonlinearCoupling** = "harder = brighter" cup bloom (Fletcher's amplitude regimes).

## Key normalized params (all values on-wire [0,1])

| Param | Norm | Physical target |
|---|---|---|
| Exciter Type | 0.40 | NoiseBurst |
| Body Model | 0.80 | Bell |
| Material | 0.95 | very metallic (b3≈0, bright tails) |
| Size | 0.30 | f0_nominal ≈ 400 Hz (ping in 300–600 Hz band) |
| Decay | 0.90 | multi-second body ring |
| Strike Position | 0.18 | near soundbow antinode (full partial set) |
| Level | 0.72 | cymbal-in-mix, under bus limiter |
| Mode Stretch | 0.45 | physical ≈1.18 (mild inharmonic spread) |
| Decay Skew | 0.62 | +0.24 (lift upper-partial shimmer) |
| Nonlinear Coupling | 0.18 | velocity-driven brightening |
| Mode Scatter | 0.55 | ~8% frequency dither (inharmonic fog) |
| Body Damping b1 | 0.16 | ≈8.2 s⁻¹ (long ring) |
| Body Damping b3 | 0.00 | 0 (metallic, highs ring) |
| Noise Mix | 0.45 | shimmer wash |
| Noise Cutoff | 0.90 | ≈9.3 kHz LP (airy) |
| Noise Decay | 0.78 | ≈880 ms long tail |
| Noise Color | 0.85 | Violet (brightest) |
| Click Mix | 0.45 | stick ping tick |
| Click Contact | 0.25 | ≈2.75 ms (crisp) |
| Click Brightness | 0.82 | ≈6.3 kHz bandpass |
| Air Loading | 0.00 | off (membrane-only) |
| Output Bus | 0.0667 | aux bus 1 |
| Pan | 0.62 | slightly right of center |
| FM Ratio | 0.30 | **inert** (NoiseBurst; documented intent only) |

## Important caveat — fmRatio
The archetype brief asks for "NoiseBurst **with fmRatio** for the bell ping." Per the param dictionary, **fmRatio is a strict no-op unless the FMImpulse exciter is selected.** With NoiseBurst, the value 0.30 is written purely as documented archetype intent (matching the existing `worldMetal` bell-tree precedent of NoiseBurst+Bell+fmRatio) and does **not** affect the rendered sound — the tuned ping comes from the **Bell body**, not the exciter. If a live Chowning-FM bell ping is wanted instead, switch Exciter Type to FMImpulse (then fmRatio 0.30 → modulatorRatio ≈ 1.9 becomes audible).

## Post-audit voicing notes
Voiced against the **corrected** post-audit chain: linear per-voice output + measured-strike body norm (−6 dBFS budget) + bus −1 dBTP limiter, so Level/velocity dynamics survive; Bell ratios/strike-shape are the corrected 2-D `cos(mθ)` meridional model; decaySkew applies the per-mode tilt on Bell; NonlinearCoupling is the env-level brightening redesign (sustained, exact-bypass at 0); pan is the M-9 equal-power law.

## Sources
- Musical-U, *Percussion Frequencies Part 2 — Cymbals* (ride bell 280/700/2400–2500 Hz; 300–600 Hz primary; decay-by-band)
- Sound on Sound, *Synthesizing Realistic Cymbals* (dense enharmonic fog, 40% energy >20 kHz, ~3.7 s shimmer tail vs ~0.2 s ping)
- Fletcher (1999), *The nonlinear physics of musical instruments* (amplitude regimes: harmonics→subharmonics→chaos)
- Fletcher & Rossing, *The Physics of Musical Instruments*, ch.20–21 (cymbal/bell inharmonic partials)