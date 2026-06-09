# Membrum Recipe — "808 Snare" (snare)

**Body:** Membrane (lower 808 tone ~238 Hz) + Secondary shell bank (metallic upper ~470 Hz)
**Exciter:** NoiseBurst (trigger-pulse kick + stick snap)

## Physics / synthesis basis
- **TR-808 snare = two bridged-T oscillators an OCTAVE apart + HP white noise** ("Snappy"). Measured oscillator frequencies **238 Hz / 476 Hz** (Roland Service Notes; Baratatronix; N8 Synthesizers; QUB 808 modelling). Bridged-T oscillators are self-damping pings (no VCA) — short, naturally-decaying two-tone ring.
- **Snappy = the white-noise generator's decay/level** — the loudest perceptual element. HP/bright noise in the snare-wire band (~3-5 kHz).
- **No pitch glide** in the original (fixed oscillators). The prompt's 400->110 Hz is a stylistic fast body thwack (applied via the pitch envelope, flagged).
- **Real acoustic snare** (for the noise/decay targets): body 150-250 Hz, smack 0.9-2 kHz, wire buzz 3-5 kHz; snares-on roughly triples damping (head at ~23% of peak by 80 ms — Madsen, UIUC Physics 406).

## How it maps in Membrum
- **Primary Membrane f0 = 238 Hz** via Size = 0.322 (500*0.1^0.322).
- **Secondary shell bank (enabled, coupling 0.5)** at f0 ~170 Hz -> its 2nd free-free partial (2.757x) ~ **469 Hz ≈ the 476 Hz upper tone**. Secondary Size 0.38 / Material 0.85 (bright metallic shimmer) — this is the two-tone dual resonance.
- **Noise Layer = the Snappy generator:** Mix 0.62, Violet colour, ~4.3 kHz LP, Q 1.48, ~145 ms decay.
- **Attack:** NoiseBurst exciter (5.9 ms) + Click (2.6 ms, ~5.2 kHz) = trigger pulse + stick tick.
- **Drive 0.48** (prompt) — post-M-2 a flavour saturator that grits the body, not a level boost.

## Key normalized values (denormalized target)
| Param | Norm | Physical |
|---|---|---|
| Exciter Type | 0.40 | NoiseBurst |
| Body Model | 0.00 | Membrane |
| Material | 0.55 | brightness 0.55 |
| Size | 0.322 | **238 Hz** body f0 |
| Decay | 0.28 | body t60 ~0.21 s |
| Strike Position | 0.45 | r/a 0.405 (off-centre) |
| Drive Amount | 0.48 | adds odd harmonics (M-2 unity makeup) |
| PitchEnv Start/End/Time/Curve | 0.6505 / 0.3702 / 0.05 / 0.15 | 400 Hz -> 110 Hz, 25 ms, exp drop |
| Mode Stretch | 0.42 | 1.13 (mild metallic) |
| Decay Skew | 0.62 | +0.24 (faster highs) |
| **Noise Mix** | **0.62** | the Snappy noise |
| Noise Cutoff/Reso/Decay/Color | 0.78 / 0.25 / 0.45 / 0.85 | ~4.3 kHz LP, Q 1.48, 145 ms, Violet |
| Click Mix/Contact/Bright | 0.45 / 0.20 / 0.80 | tick, 2.6 ms, ~5.2 kHz |
| NoiseBurst Duration | 0.30 | 5.9 ms burst |
| Body Damping b3 | 0.30 | 3e-4 (metallic-tinged) |
| Air Loading | 0.60 | Rossing membrane correction |
| **Secondary Enabled / Size / Material** | 1.0 / **0.38** / **0.85** | shell ~170 Hz, 2nd partial ~469 Hz, bright |
| Coupling Strength | 0.50 | feedforward into shell |
| Pan / Pad Enabled | 0.50 / 1.0 | centre / on |

## Deliberate defaults
Filter (bypassed: cutoff 1.0, type LP, reso/env neutral) · Fold 0 · Morph off · Choke 0 · Bus main · FM/Feedback/Friction params (wrong exciter) · all 5 Macros neutral 0.5 · Body Damping b1 mid-default · Mode Scatter 0 · **Tension Mod 0** (tom behaviour; not the 808 snare) · PitchEnv Knee/Mid off (single segment suffices) · Coupling Amount left (global cross-pad coupling off; per-pad head<->shell coupling IS used).

## Sources
N8 Synthesizers 808 snare DIY · Baratatronix Simplified 808 Snare · Sound on Sound Practical Snare Synthesis · Gearspace Snappy-is-noise thread · iDrumTune drum frequencies · Sonicbids snare guide · Madsen UIUC Physics 406 (snare-wire damping) · QUB TR-808 modelling · kurtjameswerner ChucK 808.