# Membrum Recipe — Suspended Cymbal / Orchestral Hat (cymbal)

**Body:** NoiseBody (plate-Chladni 32-mode bank + internal noise field)  •  **Exciter:** NoiseBurst
All values NORMALIZED [0,1] (on-wire / preset representation).

## Why this body + exciter
A freely-suspended orchestral cymbal is a thin bronze plate with **no definite pitch**: a sparse set of strongly **inharmonic** low partials (fundamental ~150-400 Hz) following a free-plate Chladni law `f ∝ (m+2n)^1.7`, whose **mode density grows ∝ f²** until, above ~2 kHz, the discrete partials merge into a **dense, noise-like sizzle** (Fletcher & Rossing ch.20; oemcymbal/Arborea modal data). Membrum's **NoiseBody** is the only body that pairs the corrected inharmonic plate-Chladni ratio table (audit §3-B `plate_modes.h`) with a built-in metallic noise layer — exactly this hybrid. It is excited broadband, so **NoiseBurst** (violet-noise burst) plus the always-on parallel **Noise layer** (which carries the real sizzle/swell) is the right driver, not a clean click.

Decay is band-dependent and time-variant: low modes ring 3-8 s (edge-strike T60 2-4 s) while HF modes die in <0.5 s, so the cymbal **darkens over its tail** — captured by `decaySkew` (+tilt to low modes) with `b3 = 0` (metal, no wood HF damping) and the noise envelope. Louder hits push energy **up** into a denser HF spectrum (chaotic energy cascade; Touzé & Chaigne, Rossing & Fletcher) — modelled by **NonlinearCoupling** (amplitude-driven brightening) and, on the roll, by the **Material Morph** swell. **No pitch glide** (PitchEnv Time = 0, tensionMod = 0).

## Three pad variants (hat slots, choke group 1)

| Param | Closed (short) | Pedal | Bowed/Rolled swell |
|---|---|---|---|
| Exciter / Body | NoiseBurst / NoiseBody | same | same |
| Material (2) | 0.95 | 0.92 | 0.95 |
| Size (3) | 0.30 | 0.30 | 0.42 |
| **Decay (4)** | **0.15** | 0.10 | **0.95** |
| Strike Pos (5) | 0.85 | 0.85 | 0.90 |
| Level (6) | 0.62 | 0.60 | 0.58 |
| Filter Type/Cutoff (7,8) | HP / 0.62 | HP / 0.62 | HP / 0.58 |
| Mode Stretch (21) | 0.70 | 0.70 | 0.70 |
| **Decay Skew (22)** | 0.60 | 0.60 | **0.55** |
| Nonlinear Coupling (24) | 0.35 | 0.30 | 0.45 |
| **Morph Enabled (25)** | 0 | 0 | **1** (0.55→0.95, 1.7 s) |
| **Choke Group (30)** | **1** | **1** | **1** |
| **Output Bus (31)** | main | main | **1 (aux)** |
| NoiseBurst Dur (34) | 0.20 (~4.6 ms) | 0.20 | 0.60 (~9.8 ms) |
| Noise Mix (42) | 0.62 | 0.58 | 0.70 |
| Noise Cutoff (43) | 0.85 | 0.85 | 0.82 |
| **Noise Decay (45)** | 0.20 (~36 ms) | 0.10 (~25 ms) | **0.95 (~1.7 s)** |
| Noise Color (46) | 0.85 (Violet) | 0.85 | 0.85 |
| Click Mix (47) | 0.15 | 0.12 | 0.0 |
| Body Damp b1 (50) | 0.18 | 0.22 | 0.15 |
| **Body Damp b3 (51)** | **0.0** | 0.0 | 0.0 |
| Mode Scatter (53) | 0.60 | 0.60 | 0.65 |
| Pan (64) | 0.62 | 0.62 | 0.40 |

### Closed / short
The "closed (decay 0.15)" hit: short modal ring + short bright violet-noise sizzle (~36 ms), soft contact tick. Choke 1 so a fresh hit or the open roll cuts it.

### Pedal
A tighter, even shorter chick: lowest Decay (0.10) and Noise Decay (0.10, ~25 ms), no swell, minimal click. Same choke group → mutes the open roll like a hat pedal.

### Bowed / rolled swell (the cymbal roll)
`Decay 0.95` long low-mode ring; **Morph 0.55→0.95 over ~1.7 s** brightens the timbre as the roll builds (energy cascade); longer NoiseBurst + **Noise Decay 0.95 (~1.7 s)** sustained sizzle tail; **decaySkew 0.55** + b3=0 keep the low modes ringing while the bowed edge keeps it bright; **outputBus 1** for separate reverb/placement. Choke 1 lets a closed/pedal hit damp the roll.

## Physics → parameter rationale (key levers)
- **Inharmonicity / fingerprint:** Mode Stretch 0.70 (+55% off physical ratios) + Mode Scatter 0.60 (~9% dither) turn the ordered Chladni grid into an irregular inharmonic cloud (Rossing, *Normal modes of cymbals*).
- **Bright sizzle:** Material 0.95 (noise cutoff ~6.25 kHz, brightness ~0.985) + Violet noise + b3=0 (metal, long HF).
- **Time-variant darkening:** decaySkew +0.32 phys tilts energy to low modes; HF dies first via the noise envelope.
- **Energy cascade (louder=brighter, chaotic regime):** NonlinearCoupling 0.35-0.45 (amplitude-driven waveshaping, audit M-3/M-4).
- **No pitch glide:** PitchEnv Time 0, tensionMod 0 — cymbals don't kerthump.
- **No head/shell:** Secondary disabled — a single freely-suspended plate.

## Deliberate defaults (no-op for this archetype)
Pitch-env breakpoints (Time=0), Drive/Fold (0, flavour not needed), FM/Feedback/Friction secondary-exciter params (NoiseBurst selected), Air Loading (Membrane-only), Secondary shell (single plate), all 5 macros (neutral 0.5 — underlying params set directly), Filter Env ADSR (amount 0).

## Sources
- oemcymbal / Arborea — cymbal vibration modes (f0 150-400 Hz, ρ(f)∝f², τ low 3-8 s / HF <0.5 s, edge T60 2-4 s, inharmonic 0.3-0.8)
- Fletcher & Rossing, *The Physics of Musical Instruments*, ch.20
- Rossing & Fletcher / Touzé & Chaigne — *Nonlinear vibrations and chaos in gongs and cymbals* (energy cascade)
- Wilbur & Rossing — *The normal modes of cymbals*
- Membrum signal-path audit (corrected plate Chladni, NonlinearCoupling redesign, gain-staging, M-9 pan)