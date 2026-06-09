# Membrum Recipe — Hand Clap (archetype: "Clap")

**Body:** NoiseBody  **Exciter:** NoiseBurst
*(All values NORMALIZED [0,1]; physical target noted per param.)*

## Why these choices (physics → params)
A clap is **not a pitched/modal sound** — it is a short **burst of band-limited noise** with a resonant **formant** from the cupped-hand cavity (a Helmholtz resonator). The flat-clap spectrum is broadband out to ~10 kHz; a domed/cupped clap adds a subsidiary peak below ~1 kHz, with dominant clap energy ~1–6 kHz and centroid ~1.5–4 kHz (Fletcher 2013; MDPI Acoustics 2020; Cornell/PRR 2025). The established electronic model (TR-909/808) is **white noise → bandpass** with the envelope **re-triggered ~4× ~11 ms apart** plus a slower tail; the 909 bandpass sits near **~1140 Hz, Q≈1.95** (KVR thread; Electric Druid confirms the 909 noise source is white/flat).

In Membrum the clap is therefore **noise-led**: the always-on parallel **NoiseLayer is the primary voice** (Noise Mix 0.85), and the NoiseBody modal body is deliberately weak/high. The **defining formant is NoiseLayer Resonance 0.40 → Q≈2.18** (≈909 Q≈1.95) — and because DrumVoice forces this SVF to **Lowpass**, the resonant peak at the cutoff (0.78 → ~5400 Hz) is the clap's bright formant edge. The multi-hand flam is approximated by a **longer NoiseBurst (0.55 → ~9 ms)** + **Mode Scatter 0.40** decorrelation. A short **Click** (mix 0.45, ~2.66 ms, ~2.5 kHz) supplies the palm-contact slap, and a short **Noise Decay (~50 ms)** keeps it a fast event with **no ring and no pitch glide**.

## Baseline params (meaningful set)
| Param | Norm | Physical target |
|---|---|---|
| Exciter Type | 0.40 | NoiseBurst |
| Body Model | NoiseBody | NoiseBody (plate-ratio hash + internal noise) |
| Material | 0.85 | bright, internal noise ~5.75 kHz |
| Size | 0.18 | body f0 ~990 Hz (weak/high) |
| Decay | 0.18 | ~0.39× base ring (very short) |
| Level | 0.78 | linear pre-limiter gain |
| Filter Cutoff | 1.0 | 20 kHz = ToneShaper bypass |
| NoiseBurst Duration | 0.55 | ~9.15 ms burst (flam smear) |
| Mode Scatter | 0.40 | ~6% modal dither |
| Noise Mix | 0.85 | dominant noise voice |
| Noise Cutoff | 0.78 | ~5400 Hz resonant lowpass (formant) |
| **Noise Resonance** | **0.40** | **Q≈2.18 — the clap formant (≈909 Q≈1.95)** |
| Noise Decay | 0.20 | ~50 ms tail |
| Noise Color | 0.65 | White |
| Click Mix | 0.45 | palm-contact slap |
| Click Contact | 0.22 | ~2.66 ms |
| Click Brightness | 0.62 | ~2500 Hz tick |
| Body Damping b1 | 0.50 | 25 s⁻¹ flat damping (kill body ring) |
| Body Damping b3 | 0.0 | flat (bright highs) |
| Air Loading | 0.0 | n/a (not a membrane) |
| Macro Brightness | 0.65 | +brightness nudge |
| Macro Complexity | 0.55 | +smear nudge |
| Pan | 0.50 | center |
| Pad Enabled | 1.0 | on |

## Left at default (with reason)
Pitch-env (all) = off (Time 0) — a clap has no glide/fundamental. Drive/Fold = 0 (no added saturation). Mode Inject / Nonlinear Coupling = 0 (no synthetic tone/brightening). Morph off. Secondary shell / Coupling Strength = 0 (no shell). Tension Mod = 0 (Membrane-only, no glide). FM Ratio / Feedback / Friction = no-op for NoiseBurst. ToneShaper Filter Type/Reso/Env ADSR = no-op while filter bypassed. Choke 0 / Bus 0 / Tightness·BodySize·Punch neutral.

## Sources
- Fletcher, *Shock waves and the sound of a hand-clap*, Acoustics Australia 2013 — https://www.acoustics.asn.au/journal/2013/2013_41_2_Fletcher_paper.pdf
- *Handclap for Acoustic Measurements*, MDPI Acoustics 2020 — https://www.mdpi.com/2624-599X/2/2/15
- Cornell/PRR 2025 hand-clap acoustics — https://news.cornell.edu/stories/2025/03/dynamic-acoustics-hand-clapping-elucidated
- TR-909 clap emulation (bandpass ~1140 Hz, Q≈1.95, 4× ~11 ms) — https://www.kvraudio.com/forum/viewtopic.php?t=466450
- TR-909 noise generator (white/flat) — https://electricdruid.net/tr-909-noise-generator/