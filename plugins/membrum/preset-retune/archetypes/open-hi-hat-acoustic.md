# Membrum Recipe — Open Hi-Hat (acoustic)

**Body:** NoiseBody (plate Chladni 32-mode metallic bank + internal HF noise layer)
**Exciter:** NoiseBurst (violet-noise bandpass contact burst)
**Choke:** Group 1 (cut by closed/pedal hat)

## Physics it targets
A hi-hat is a pair of thin free-edged spun-bronze plates. Like all cymbals it is strongly **inharmonic** — partials follow a Chladni power law `f ∝ (m+2n)^P`, P≈1.6–1.73 (Rossing & Peterson; Fletcher & Rossing), with no single perceived pitch. Above ~3–5 kHz the modal density is so high the spectrum is effectively **band-limited high-frequency noise** (the "shimmer"; high spectral centroid). Damping is strongly frequency-dependent: high modes die in tens of ms, low/mid modes ring on; a **loosely-held OPEN hat** sustains a smooth shimmering wash for hundreds of ms to >1 s, while the **closed/pedal hat** clamps it to ~50 ms and **chokes** the open hat — the defining hi-hat gesture. There is no pitch glide. Classic analog synthesis (Roland 808/606) abandons faithful modes: ~6 inharmonically-tuned square oscillators (808: 800/540/522/370/304/205 Hz) through **bandpass filters at ~3.4 kHz and ~7.1 kHz** + resonant **highpass**, with the **AD decay** selecting closed (50 ms) / open (90–600 ms). The shared thread — inharmonic metallic core + dominant HF noise wash + decay-as-open/closed + choke — is exactly what we build.

## Mapping onto Membrum
- **NoiseBody + NoiseBurst** give the metallic plate thicket plus a stick-contact noise burst.
- **High Material (0.9)** + **b3 = 0** → maximally metallic, no wood damping → the highs sustain (shimmer).
- **Low b1 (0.30 → ~15 s⁻¹)** + **Decay 0.55** → the long open-hat ring.
- **Bright Noise layer**: Mix 0.70, Cutoff ~8 kHz, Violet color, **Decay ~500 ms** — the long bright noise tail that makes it OPEN (vs ~60 ms closed).
- **Mode Scatter 0.45** breaks the too-ordered plate ratios into the dense inharmonic Chladni thicket of a real cymbal.
- **Highpass tone shaper (~800 Hz, mild reso)** strips low mud — the 808/606 "no bass in the hat region" move.
- **Choke group 1** so the closed/pedal hat cuts it. **PitchEnv off**, **air-loading off**, **no coupling/tension** (cymbal, not a drum). **Pan slightly left** for a realistic kit image.

## Baseline (NORMALIZED, on-wire) values
| Param (offset) | norm | physical |
|---|---|---|
| Exciter Type (0) | 0.40 | NoiseBurst |
| Body Model (1) | **1.00** | NoiseBody (round(1.0·5)=5) |
| Material (2) | 0.90 | brightness 0.97, internal noise ~6 kHz |
| Size (3) | 0.18 | f0 ≈ 990 Hz |
| Decay (4) | 0.55 | ~1.2× body decay (open band) |
| Strike Position (5) | 0.45 | mid-rim, broad mode spread |
| Level (6) | 0.72 | linear |
| Filter Type (7) | 0.50 | Highpass |
| Filter Cutoff (8) | 0.534 | ~800 Hz HP |
| Filter Resonance (9) | 0.20 | Q ≈ 2.6 |
| Filter Env Amount (10) | 0.50 | 0 (no sweep) |
| Choke Group (30) | 0.125 | group 1 |
| Noise Mix (42) | 0.70 | bright wash |
| Noise Cutoff (43) | 0.867 | ~8 kHz LP |
| Noise Resonance (44) | 0.25 | Q ≈ 1.5 |
| Noise Decay (45) | 0.70 | ~500 ms tail |
| Noise Color (46) | 0.85 | Violet |
| Click Mix (47) | 0.22 | crisp tick |
| Click Contact (48) | 0.20 | 2.6 ms |
| Click Brightness (49) | 0.85 | ~6.7 kHz |
| Body Damping b1 (50) | 0.30 | ~15 s⁻¹ (long ring) |
| Body Damping b3 (51) | 0.00 | metallic, long highs |
| Air Loading (52) | 0.00 | off |
| Mode Scatter (53) | 0.45 | ~6–7% dither |
| Pan (64) | 0.42 | slightly left |

**Closed/pedal-hat sibling** (same choke group, contrast): drop Decay→~0.07, Noise Decay→~0.06, raise b1→~0.65 for a ~50–60 ms tight chick.

## Sources
- baratatronix 808 & 606 hi-hat/cymbal synthesis (oscillator freqs, 3.4/7.1 kHz bands, 90–600 ms open / 50 ms closed)
- ModeAudio "Massive Drum Design Pt.3: Hi-Hats" (HP filtering, resonance, decay = open/closed)
- oemcymbal / Rollins — cymbal vibration modes & inharmonicity; Fletcher & Rossing power-law
- Wikipedia Spectral Centroid (brightness)
- Membrum signal-path audit 2026-06-07 (corrected post-audit semantics)
