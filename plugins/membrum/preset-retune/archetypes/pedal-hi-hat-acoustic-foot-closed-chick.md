# Membrum Recipe — Pedal Hi-Hat (acoustic), foot-closed "chick"

**Body:** NoiseBody (plate-Chladni inharmonic modes + parallel filtered-noise sizzle)
**Exciter:** NoiseBurst (~3 ms violet-noise contact slap)
**Concept:** a clone of the acoustic closed hi-hat with (1) even shorter decay, (2) a stronger / chunkier low-mode "splat" via a raised flat-damping floor (b1), and (3) choke group 1 shared with the closed and open hats.

## Why this body + exciter
A hi-hat is a strongly inharmonic B20-bronze plate (>100 (m,n) modes, inharmonicity α≈0.3–0.8) whose closed/foot articulation is perceptually **noise-dominated broadband sizzle** with a metallic-partial colour. NoiseBody is the only Membrum body that delivers both: the corrected free-plate Chladni mode bank for the metallic partials plus an always-relevant noise layer for the dominant hiss. The foot "chick" is produced by the two cymbals **slapping shut**, a short broadband contact — modelled by NoiseBurst, not a tonal stick click (Click layer is OFF).

## The pedal-vs-closed deltas (this is what makes it a *pedal* hat)
| Delta | Value (norm) | Physical target | Why |
|---|---|---|---|
| Even shorter decay | Decay **0.05** | ~50–60 ms body ring | foot clamps both cymbals hard (808 closed-hat fixed ~50 ms) |
| Shorter sizzle | Noise Decay **0.07** | ~26 ms | clamp kills the noise tail faster than a stick-closed hat |
| Stronger/chunkier low splat | **Body Damping b1 = 0.5** (≈25 s⁻¹) | high flat-damping floor clamps the low modes into a chunky "thud-shut" | the spec's "low-mode component raised / b1 raised" — the body closes hard instead of ringing |
| Slightly bigger body | Size **0.12** (f0≈1139 Hz) | marginally more low-mode weight | the "chunkier" splat |
| Quieter | Level **0.68** | linear pre-limiter | a foot chick sits below the stick-closed hat |

## Key research-derived values
- **Noise Mix 0.85 / Color Violet / Cutoff ≈12.5 kHz** — closed-hat energy is HF-rising broadband, "crisp/air" out to 8–17 kHz, dominant 300–3000 Hz with sizzle above (musical-u, emastered, izotope).
- **Material 0.88** — near-metallic B20 bronze; lifts internal noise cutoff to ~5.9 kHz and modal brightness to 0.96.
- **b3 = 0** — metal keeps its highs (low f²-damping); the short ring is owned by b1, not b3, so the partials stay bright/metallic.
- **No pitch env, no mode inject, no coupling, no drive/fold** — a hi-hat has no pitch glide, no tonal harmonic series, no coupled shell.
- **Choke group 1** — pedal/closed/open hats mutually choke (real hi-hat behaviour; Wikipedia hi-hat).

## Implementation note (membrum_preset_generator.cpp)
This maps directly onto the existing acoustic-kit idiom `pads[8] = pads[6]` (clone the closed hat) then override: `pads[8].size = 0.12; pads[8].decay = 0.05; pads[8].level = 0.68; pads[8].material = 0.88; pads[8].noiseLayerDecay = 0.07; pads[8].bodyDampingB1 = 0.5; pads[8].bodyDampingB3 = 0.0; pads[8].chokeGroup = 1;` (the reference pedal pad currently leaves b1 at the derive default — raising it is the new, spec'd change).

## Sources
- TR-808 hat synthesis (oscillator freqs, 3440 Hz BP + HP, closed ~50 ms vs open 90–600 ms): baratatronix
- Cymbal modal structure / inharmonicity / B20 bronze: oemcymbal, arborea, ResearchGate "normal modes of cymbals" / "vibrational analysis of drum cymbals"
- Hi-hat "chick" articulation & choke: Wikipedia hi-hat, KGUmusic
- Hat spectral band / EQ: musical-u, emastered, izotope
