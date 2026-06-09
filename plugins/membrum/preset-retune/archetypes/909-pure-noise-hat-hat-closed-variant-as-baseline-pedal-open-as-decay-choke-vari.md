# Membrum Recipe — "909 Pure-Noise Hat" (hat)

## Archetype summary
The TR-909 hi-hat is **not** a modal cymbal: it is a 6-bit PCM recording of a real cymbal replayed through an analog **VCA envelope + VCF** — i.e. *enveloped, filtered broadband noise* with low-bit "crunch" and **no usable pitch**. Open and closed share one ROM (cannot sound simultaneously → choke). We model it with **NoiseBody + NoiseBurst**, deliberately suppressing modal structure (`airLoading 0`, `modeScatter 0`) and letting the bright, violet-tilted noise layers define the sound.

Sources: [Electric Druid — TR-909 noise generator](https://electricdruid.net/tr-909-noise-generator/), [Knobula — 909 hi-hat](https://www.knobula.com/post/why-the-tr-909-hand-clap-can-t-be-sampled-and-why-the-hi-hat-shouldn-t-have-been), [Wikipedia — TR-909](https://en.wikipedia.org/wiki/Roland_TR-909).

## Acoustic / synthesis profile
- **Pitch:** none usable — broadband, inharmonic. Cymbal vibration spans ~100 Hz to >5 kHz, energy weighted **above 1 kHz**, sizzle to **10–16 kHz+** ([Musical-U](https://www.musical-u.com/learn/percussion-frequencies-part-2-cymbals/), [audiorecording.me](https://www.audiorecording.me/drum-frequencies-of-kick-bass-drum-hi-hats-snare-and-crash-cymbals.html/2)).
- **Modes/inharmonicity:** minimal perceptual modal content; distinctive 909 crunch is 6-bit quantization noise, not tuned partials → keep modal layer clean and backgrounded.
- **Decay (T60):** closed ≈ **50 ms** (Roland family spec), pedal shorter, open **300–600 ms+** sizzle; 909 envelope is an exponential decay on the ROM address DACs ([Electric Druid](https://electricdruid.net/tr-909-noise-generator/), [Baratatronix](https://www.baratatronix.com/blog/cascadia-808-cymbal-hi-hat-synthesis)).
- **Attack:** sharp few-ms filtered-noise edge (VCA snap), no pitched click.
- **Noise content:** bright broadband, high spectral centroid → **Violet** color + near-open lowpass.
- **Pitch glide:** none. **Material:** thin bright bronze → high `Material`.

## Body & exciter
- **Body model:** `NoiseBody` (norm **1.0**)
- **Exciter:** `NoiseBurst` (norm **0.4**)

## Closed-hat baseline (every meaningful param, NORMALIZED)
| Param (offset) | Norm | Denormalized target | Why |
|---|---|---|---|
| Exciter Type (0) | 0.4 | NoiseBurst | noise envelope, not a strike |
| Body Model (1) | 1.0 | NoiseBody | noise+plate hybrid, noise dominant |
| Material (2) | 0.95 | brightness 0.985; internal noise cutoff 6.25 kHz | bright cymbal bronze |
| Size (3) | 0.5 | f0≈474 Hz | pitch irrelevant; keep high |
| Decay (4) | 0.07 | ~0.33× mult; internal noise ~20 ms | tight closed tick (~50 ms) |
| Strike Pos (5) | 0.3 | off-center | near-inaudible on noise hat |
| Level (6) | 0.72 | linear 0.72 | hat sits under kick/snare |
| Filter Type (7) | 0.0 | Lowpass | shape only the ultra-top |
| Filter Cutoff (8) | 0.95 | ~14.3 kHz LP | keep full sizzle band |
| Noise Mix (42) | 0.9 | strong parallel noise | this IS the 909 hat |
| Noise Cutoff (43) | 0.95 | ~14.8 kHz LP | bright/airy |
| Noise Reso (44) | 0.12 | Q≈0.86 | flat broadband, no whistle |
| Noise Decay (45) | 0.18 | ~47 ms | matches ~50 ms closed spec |
| Noise Color (46) | 0.95 | **Violet** | rising sizzle tilt |
| Click Mix (47) | 0.35 | bright tick | sharpens attack edge |
| Click Contact (48) | 0.1 | 2.3 ms | crisp tick |
| Click Brightness (49) | 0.9 | ~8.7 kHz BP | bright "tss" |
| NoiseBurst Dur (34) | 0.1 | ~3.3 ms | sharp noise attack |
| Air Loading (52) | 0.0 | pure ratios (no-op) | deliberate pure-noise |
| Mode Scatter (53) | 0.0 | pure ratios | deliberate clean hat |
| Mode Stretch (21) | 0.333 | unstretched | modal layer backgrounded |
| Decay Skew (22) | 0.5 | no skew | brightness from noise, not skew |
| Choke Group (30) | 0.125 | group 1 | closed/pedal/open mutual choke |
| Pan (64) | 0.5 | center | default placement |
| Pad Enabled (59) | 1.0 | on | — |

## Variants (change only these)
- **Closed** (baseline): Decay **0.07**, Noise Decay **0.18** (~47 ms), choke group 1.
- **Pedal (foot):** Decay **0.04**, Noise Decay ~**0.10** (~33 ms) — shortest, choke group 1.
- **Open:** Decay **0.42**, Noise Decay ~**0.62** (~430 ms long sizzle), choke group 1 (a closed/pedal hit chokes it).

## Deliberate defaults
Drive/Fold = 0 (909 crunch is quantization, not saturation), all PitchEnv off (no glide), Mode Inject / Nonlinear Coupling = 0 (keep it pitch-less and clean), Secondary shell off, Tension Mod off (Membrane-only), Damping b1/b3 left at sentinel (Decay + Material drive them), Macros neutral 0.5.