# Membrum Recipe — FM-Bell Hi-Hat (synthesized metallic hat)

A ringing, pitched-metallic west-coast hat built from a **Bell** body excited by **FMImpulse**, with an inharmonic FM-bell spectrum supplying the shimmer instead of filtered noise. Three variants (closed/pedal/open) differ **only in decay** (and the b1 damping floor that gates it), exactly like the TR-808 closed/open hat pair. Choke group 1 makes them cut each other.

All values below are **normalized [0,1]** (the preset/on-wire representation). Physical targets use the post-audit (corrected) Membrum semantics.

## Body & Exciter
- **Body Model:** Bell (offset 1, norm 0.8). 16-mode church-bell Chladni bank, ratios 0.25…12.0 × f0_nominal. The only Membrum body with a *tuned-inharmonic metal* ratio set — gives a pitched, ringing metal timbre rather than a plate/cymbal fog or a noise hat.
- **Exciter:** FMImpulse (offset 0, norm 0.8). Chowning bell-FM; modIndex (0.5–3.0 rad) decays faster (30 ms) than amplitude (80 ms) → bright→pure struck-metal envelope. A non-integer modulator ratio injects an inharmonic sideband cluster into the bell body.

## Core voicing (shared)
| Param | Norm | Physical target | Why |
|---|---|---|---|
| Material | 0.92 | brightness ≈ 0.98 (very metallic) | long-ringing upper partials = metal |
| Size | 0.05 | f0_nominal ≈ 712 Hz; partials to ≈ 8.5 kHz | lands metallic energy in the 3–10 kHz hat band (≈808 3440/7100 Hz centres) |
| Strike Position | 0.45 | strike azimuth ≈ 0.71 rad | de-emphasises low hum, favours bright partials |
| Level | 0.72 | linear 0.72 | hats sit under kick/snare |
| FM Ratio | 0.65 | modulator ratio = **2.95** (non-integer) | dense inharmonic FM sidebands (Chowning / 808 "avoid even multiples") |
| Mode Scatter | 0.40 | ≈ 6% partial dither | cymbal "dense fog" beating |
| Mode Stretch | 0.45 | physical 1.175 (mild stretch) | extra clangy inharmonicity |
| Decay Skew | 0.62 | +0.24 tilt → lift upper partials | HF-dominant hat spectrum |
| Body Damping b3 | 0.02 | ≈ 0.02e-3 s (near-pure flat) | metal highs ring, not wood thump |
| Click Mix / Contact / Brightness | 0.40 / 0.15 / 0.85 | tick, ≈2.45 ms, ≈6.4 kHz | sharp metallic stick attack |
| Noise Mix / Cutoff / Color / Reso | 0.22 / 0.90 / 0.85 / 0.20 | floor under body, ≈11 kHz LP, Violet, Q≈1.24 | broadband HF sizzle floor, under the bell ring |
| Choke Group | 1 (norm ≈ 0.125) | mute group 1 | open/closed/pedal cut each other |
| Pan | 0.62 | slightly right | kit stereo image |

## Per-variant (the closed/pedal/open axis)
| Variant | Decay | Body Damping b1 | Noise Decay | Target T60 |
|---|---|---|---|---|
| **Closed** | 0.08 | 0.78 (≈39 s⁻¹) | 0.25 (≈67 ms) | ~50 ms |
| **Pedal** | 0.22 | 0.45 | 0.30 | ~120 ms |
| **Open** | 0.62 | 0.18 | 0.50 (≈330 ms) | ~450 ms |

The TR-808 reference: closed = fixed ~50 ms, open = 90–600 ms (pedal openness), cymbal = 350–1200 ms.

## Deliberately defaulted (physical reasons)
- **Pitch env (all 8 params), Tension Mod = 0** — hats don't pitch-glide / kerthump.
- **Air Loading** — Membrane-only, inert on Bell.
- **Drive / Fold / ToneShaper filter + filter env** — FM exciter already supplies inharmonic harmonics; SVF left transparent (cutoff 1.0 = bypass).
- **Nonlinear Coupling = 0** — brightness already carried by FM + scatter + low b3; keep the transient clean (exact bypass).
- **Secondary/head-shell coupling off** — a hat wants no added low body.
- **Material Morph off** — static metallic hit is correct.
- **Macros neutral (0.5)** — preserve the explicitly-voiced params.
- **Feedback Amt / NoiseBurst Dur / Friction Pressure** — no-ops under FMImpulse.

## Why this is distinct from the noise hats
Every factory hat uses NoiseBody (a plate/cymbal noise fog). This recipe uses **Bell + FMImpulse**: the shimmer is an *inharmonic FM-bell ring with a beating partial fog*, with noise only as a thin under-body floor — a pitched, ringing, west-coast metallic hat.

## Sources
- TR-808 hat/cymbal circuit (6 inharmonic square osc 205–800 Hz, bandpass 3440/7100 Hz, closed 50 ms / open 90–600 ms / cymbal 350–1200 ms): Baratatronix.
- Cymbal inharmonic fog, HF energy >20 kHz, FM/multi-osc metallic synthesis: Sound on Sound; Musical-U.
- FM bell (non-integer c:m → inharmonic; index decays faster than amplitude): Chowning 1973; CCRMA CLM tutorial; ADSR; Wikipedia FM synthesis.
- Hi-hat physical modeling / frequency-dependent damping: Acoustical Science & Technology hi-hat paper.