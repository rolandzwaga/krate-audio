## Membrum Recipe — Trap Hat (multi-decay)

**Archetype:** hat · **Body:** NoiseBody · **Exciter:** NoiseBurst

Four pads on **choke group 1**: three closed variants with graded decays (for 1/16–1/32 trap rolls/triplets) and one open. The brightest synthetic hats in the kit. All values below are **normalized [0,1]** (preset/on-wire); physical targets noted.

### Physics / synthesis basis
Real hi-hats are two thin bronze cymbals: densely **inharmonic** spectrum, fundamental ~150–400 Hz, noise-like hash 2–20 kHz, "sparkle" 5–10 kHz, higher modes decay first, and **no pitch glide**. The established synthetic method (TR-808/909, web-audio, Massive) is **bright noise + inharmonic oscillator hash → BP/HP filter → fast AD envelope**, with closed decay ≈50 ms vs open ≈350–1200 ms (808 service manual). Membrum reproduces this with NoiseBody (plate-ratio modal hash) + the always-on Violet NoiseLayer carrying the bulk.

### Shared voicing (all four hats)
| Param | Norm | Physical |
|---|---|---|
| Exciter Type | 0.40 | NoiseBurst (violet bandpass burst) |
| Body Model | 1.00 | NoiseBody |
| Material | 0.92 | brightness 0.976, internal-noise cut 6.1 kHz |
| Level | 0.72 | linear pre-limiter |
| Filter Cutoff | 1.00 | 20 kHz → ToneShaper SVF bypassed (no dulling) |
| Noise Mix | 0.85 (open 0.78) | the hat is mostly this layer |
| Noise Cutoff | 0.95 (open 0.92) | ~13 kHz LP (open ~11 kHz) |
| Noise Resonance | 0.10 | broadband hash, no pitched peak |
| Noise Color | 0.95 (open 0.92) | Violet (+6 dB/oct) |
| Click Mix / Bright | 0.18 / 0.92 | faint ~9.4 kHz tick |
| NoiseBurst Duration | 0.50 | 8.5 ms contact |
| Mode Scatter | 0.25 | de-pitches the metallic ring |
| Body Damping b3 | 0.00 | metallic, highs ring (no f² roll-off) |
| Air Loading | 0.00 | off (membrane-only anyway) |
| Choke Group | 0.125 | group 1 (open↔closed mute) |
| PitchEnv Time | 0.00 | no glide (feature disabled) |

### Per-variant
| Variant | Size | Decay | Noise Decay | Body Damp b1 |
|---|---|---|---|---|
| Closed A | 0.10 | 0.05 (~0.34×) | 0.05 (~25 ms) | 0.78 (~39 s⁻¹) |
| Closed B | 0.10 | 0.08 (~0.40×) | 0.08 (~29 ms) | 0.68 (~34 s⁻¹) |
| Closed C | 0.10 | 0.12 (~0.49×) | 0.12 (~35 ms) | 0.58 (~29 s⁻¹) |
| Open | 0.18 | 0.55 (~1.06×) | 0.50 (~230 ms) | 0.30 (~15 s⁻¹) |

The graded short noise-envelope decays + tight b1 on the closed voices are exactly what enables fast trap hi-hat rolls; the open voice rings ~3–9× longer.

### Left at default (with reason)
ToneShaper Filter Type/Res/Env (bypassed); Drive/Fold (0=bypass, brightness comes from noise); all PitchEnv params (no glide); Mode Stretch/Decay Skew (neutral — ratios already inharmonic, noise dominates); Mode Inject/Nonlinear Coupling (0=bypass, would add unwanted tonal body); Morph (off); Secondary shell + Coupling (off — adds low weight wrong for a hat); Tension Mod (off, Membrane-only); FM/Feedback/Friction (no-op, wrong exciter); macros (neutral 0.5); Pan 0.5 (center baseline).

### Sources
808 circuit/decays — baratatronix; synthesis method — joesul.li, ModeAudio; cymbal modal acoustics — Rollins normal-modes study, oemcymbal; mix bands — Music Guy Mixing, Musical-U, eMastered.