# Membrum Recipe — Güiro (perc)

**Body:** NoiseBody  ·  **Exciter:** Friction  ·  **Character:** unpitched broadband scrape / rasping gourd, no click, short contact ring.

## Why this body + exciter

The güiro is a **scraped idiophone** (Hornbostel-Sachs 112.23): a hollow gourd with parallel notches dragged with a stick, producing a "zipper-like ratchet" rasp with **no definite pitch** ([Wikipedia](https://en.wikipedia.org/wiki/G%C3%BCiro)). In the synthesis literature a scrape is a **source–filter** process: a roughness-driven micro-impact / stick-slip impulse train (speed- and force-controlled) through a resonant body filter ([arXiv 2112.08984](https://arxiv.org/abs/2112.08984); Avanzini/Serafin/Rocchesso elasto-plastic friction, DAFx-02). The gourd body itself contributes only a few low-Q resonances over broadband content ([Iwami, JSDD 2008](https://ui.adsabs.harvard.edu/abs/2008JSDD....2..596I/abstract)).

- **Friction exciter** = the continuous stick-slip scrape (BowExciter rosin-jitter junction), not a single impact. `frictionPressure 0.55` deepens the rasp.
- **NoiseBody** = a loose plate-ratio modal bank (color filter) **plus** an internal noise bed = the broadband gourd.
- The **parallel noise layer** (mix 0.65, lowpass ~2.25 kHz) carries the primary rasp; the modal body just tints it.

## Baseline (NORMALIZED values)

| Param | Norm | Physical target |
|---|---|---|
| Exciter Type | 0.60 | Friction |
| Body Model | 1.00 | NoiseBody |
| Material | 0.65 | int. noise cut 4750 Hz, brightness 0.895 |
| Size | 0.18 | f0 ≈ 991 Hz (small/high) |
| Decay | 0.30 | ~0.37 s modal / ~31 ms internal-noise env |
| Strike Position | 0.30 | off-center modal weighting |
| Level | 0.70 | linear pre-limiter gain |
| Friction Pressure | 0.55 | bow pressure → 0.775 at full velocity |
| Noise Mix | 0.65 | primary rasp voice |
| Noise Cutoff | 0.62 | lowpass ≈ 2256 Hz (darker rasp) |
| Noise Color | 0.55 | White (then LP-darkened) |
| Noise Decay | 0.30 | ~83 ms env |
| Noise Resonance | 0.20 | q ≈ 1.24 (low-Q) |
| Decay Skew | 0.55 | +0.10 (slight high-mode lift) |
| Body Damping b1 | 0.42 | 21.1 s⁻¹ flat damping |
| Body Damping b3 | 0.00 | pure flat (keep bright highs) |
| Mode Scatter | 0.30 | ~4.5% dither (carved-ridge irregularity) |
| Click Mix | 0.00 | **no click** (scrape has no beater transient) |
| Air Loading | 0.00 | n/a on NoiseBody |
| Pan | 0.50 | center |
| Pad Enabled | 1.00 | on |

## Deliberate defaults
Pitch-env OFF (unpitched, no glide), Mode Inject / Nonlinear Coupling / Drive / Fold OFF (no imposed pitch or extra saturation), ToneShaper filter bypassed (cutoff 1.0 — the noise LP already shapes it), Morph OFF, Secondary shell OFF (single loose resonator), Tension Mod 0 (Membrane-only). FM Ratio / Feedback / NoiseBurst Duration are no-ops under the Friction exciter. Macros neutral 0.5.

> Matches the existing `tools/membrum_preset_generator.cpp` Pad 9 "Guiro" builder (lines 2750–2761), voiced against the post-audit corrected semantics (measured-strike body norm, per-mode decaySkew tilt, click-free retrigger).