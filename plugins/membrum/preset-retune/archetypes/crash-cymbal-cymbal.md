# Membrum Recipe — Crash Cymbal (cymbal)

**Body:** NoiseBody (plate-ratio modal bank + internal noise) · **Exciter:** NoiseBurst

## Acoustic profile (why this voicing)
A crash cymbal is a large, thin, free-edge bronze plate with an **extremely dense, strongly inharmonic** mode spectrum governed by a *modified Chladni power law* `f(m,n) ∝ (m+2n)^P`, with measured exponents `P ≈ 1.6–1.73` (Rossing). Electronic-holography studies resolve ~300 modes in a 16″ cymbal, >100 in crashes. There is **no definite pitch** — the lowest plate modes sit in the low hundreds of Hz, and the audible "wash" is the dense unresolved upper cluster (~1–12 kHz, out to 20 kHz).

The signature crash **bloom** is the cymbal's *nonlinearity* (von Kármán thin-shell): a hard strike pushes energy from the initially-excited low modes up into the highs over a fraction of a second (delayed-HF-onset / "bandwidth buildup"), so brightness **builds, then decays**. Decay is strongly frequency-dependent: ~50 ms (HF modes) up to several seconds (LF modes); practical synthesis targets ~0.75 s + a long sizzle tail. There is **no pitch glide** (that's the 808 kick / tom, not a crash).

## Mapping to Membrum
- **NoiseBody** supplies the dense inharmonic plate skeleton (post-audit free-plate Chladni `(m+2n)^1.7` table, per-family degeneracy split) **plus** an internal noise layer — the canonical cymbal body.
- **NoiseBurst** launches all modes with a broadband contact burst and seeds the wash.
- **Mode Scatter (0.60)** + **Mode Stretch (1.4×)** turn the regular table into an organic, strongly-inharmonic cloud.
- **b3 = 0 / b1 ≈ 15 s⁻¹**: pure flat damping = metallic long-ringing highs (the shimmer), with a controlled overall ring length.
- **The bloom** is modelled by **NonlinearCoupling (0.35)** — amplitude-driven, velocity-sensitive HF brightening that rides the decay (Stowell *tap = bell / hard = broadband*) — plus a touch of **ModeInject (0.25)** for harmonic fill.
- **Bright filtered-noise layer** (cutoff ≈ 6.5 kHz, violet, long decay ≈ 310 ms) is the broadband sizzle/wash; **Click (0.20, ~2.9 ms, bright)** is the modest stick contact.
- **Output Bus 1** sends the crash to its own pre-master bus for overhead/reverb treatment.

## Key normalized baseline
| Param | Norm | Physical |
|---|---|---|
| Exciter | NoiseBurst | violet-noise contact burst |
| Body | NoiseBody | plate Chladni modes + noise |
| Material | 0.93 | brightness ≈0.98, very metallic |
| Size | 0.35 | f0 ≈ 670 Hz (modes stack above) |
| Decay | 0.70 | body ring ≈1.5 s |
| Strike Pos | 0.55 | off-centre edge, broad mode set |
| Mode Stretch | 0.60 | 1.4× (inharmonic) |
| Mode Inject | 0.25 | harmonic fill |
| Nonlinear Coupling | 0.35 | crash bloom (vel-sensitive) |
| Mode Scatter | 0.60 | ~9% dither, dense cloud |
| Body b1 / b3 | 0.30 / 0.0 | ~15 s⁻¹ flat / no f² damping (metallic) |
| Air Loading | 0.0 | (membrane-only; off) |
| Noise Mix / Cutoff / Color / Decay | 0.50 / 0.85 / 0.85 / 0.60 | bright violet wash ≈6.5 kHz, ~310 ms |
| Click Mix / Contact / Bright | 0.20 / 0.3 / 0.82 | short bright low-level tick |
| Filter Cutoff | 1.0 | bypassed (keep all top) |
| PitchEnv Time | 0.0 | no glide |
| Output Bus | →1 | cymbal aux send |
| Level / Pan | 0.72 / 0.5 | hot but under limiter / center |

## Per-kit variation (same instrument)
- **Acoustic / warmer:** Material ~0.90, Noise Color ~0.62 (white), Noise Cutoff ~0.78, slightly longer Decay; outputBus may stay 0.
- **Electronic / brighter (808/909):** Material ~0.95, Noise Color ~0.92 (violet), Noise Cutoff ~0.90, Mode Scatter up to ~0.85, outputBus 1.

All values are NORMALIZED [0,1] (preset/on-wire); physical targets and rationale per the params table.