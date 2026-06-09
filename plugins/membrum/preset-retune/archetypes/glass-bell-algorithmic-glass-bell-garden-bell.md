# Membrum Recipe — "Glass Bell Garden" (Bell, algorithmic)

A bright, glassy, inharmonic bell garden: **one Bell-body skeleton scaled and tilted across 16 i-indexed pads**, with Mallet / FMImpulse / Friction exciters cycled, a metallic coupled secondary (material 0.85), and cross-pad sympathetic coupling — clean, ringing, slightly-detuned glass timbres.

## Body & Exciter
- **Body: Bell** (offset 1, norm 0.8 → idx 4). The only Membrum resonator with a tuned/inharmonic bell partial series (16 Chladni partials, hum at 0.25·f0, nominal at 1.0) and a long low-b3 metallic ring. `f0_nominal = 800·0.1^size`.
- **Exciter: i-cycled** — **FMImpulse** (default; Chowning bell-FM attack, `c:m = 1.0+3.0·fmRatio`), **Mallet** (i%3==0; cleanest pure taps), **Friction** (i%4==3; scrape-shimmer onset on a minority). Exciter shapes only the attack; the pitched ring is the Bell body.

## Researched acoustics → mapping
| Acoustic fact (cited) | Membrum control |
|---|---|
| Struck glass is **inharmonic**: 2nd mode ≈ 2.3–2.6× f0 (not 2×) — wine-glass ratios 2.37–2.58 (Jundt 2006; PDX) | `Mode Stretch` mildly positive (norm ~0.42 → phys ~1.13) sharpens upper partials; **FMImpulse** non-integer `FM Ratio` reinforces inharmonic attack (Chowning c/m≈1/1.4) |
| **Higher partials decay faster**; hum (lowest) rings longest (Hibberts) | `Decay Skew` positive (norm ~0.63 → +0.26) tilts upper partials to decay faster |
| Glass/bell-bronze = **low-loss, high-Q**, long bright ring; crystal especially (Avant Acoustics) | `Material` high (norm ~0.6 → brightness 0.88), `Body Damping b3 = 0` (no f² loss, long metallic highs), long `Decay`/`b1` |
| Glass register C4–C6, singing-glass f0 ~530–700 Hz (Jundt; glass harmonica) | `Size` i-ramp 0.10→0.70 → nominal f0 ≈ 630→285 Hz, partials up to 1–3 kHz |
| **No pitch glide** (linear tuned resonator) | PitchEnv Time = 0 (disabled); faint velocity brightening via small `Nonlinear Coupling` |
| **Minimal noise**; brief bright contact "tink" | `Noise Mix` low (~0.13) + bright/white; `Click` short (~2 ms) + bright (~2.4 kHz) |
| Real glasses beat / are never identical (Perrin doublets; phase-locking wineglasses) | `Mode Scatter` ~0.46 (~7% organic detune) |

## i-indexed ramps (the "garden" — 16 pads, i = 0..15)
- `material` 0.40→0.95 (warmer→brighter glass)
- `size` 0.10→0.70 reversed (small/high→large/low)
- `decay` 0.55→0.90, `decaySkew` 0.55→0.79, `modeStretch` 0.30→0.66, `modeScatter` 0.30→0.62
- `fmRatio` 0.30→~0.95 (detunes the FM chime; FMImpulse pads only)
- `nonlinearCoupling` 0.0→0.30, `macroBrightness` 0.55→0.95, `macroComplexity` 0.55→0.79
- coupled secondary on **i%3==0** (`Secondary Enabled`, `Coupling Strength` 0.25, `Secondary Material` 0.85 metallic)
- `couplingAmount` 0.55→0.85 (sympathetic garden), kit `globalCoupling 0.65`, `maxPoly 16`

## Stages deliberately OFF (clean glass)
PitchEnv family, Tension Mod (Membrane-only no-op), ToneShaper Filter (bypassed, cutoff 1.0), Drive/Fold (0 = clean), Mode Inject (0; body already tuned), Choke (free ring), macros Tightness/BodySize/Punch (neutral 0.5), Air Loading (Membrane-only).

## Citations
PDX *Fourier Spectrum of a Singing Wine Glass*; Jundt et al. 2006 (UNSW) *Vibrational modes of partly filled wine glasses*; Harvard *Shattering Wineglass*; Avant Acoustics *Acoustics of Glass*; Perrin/Charnley/DePont 1983 JSV; Hibberts *Sound of Bells* (strike tone, partial decay); Chowning 1973 FM bell synthesis; CCRMA CLM FM tutorial; Wikipedia *Inharmonicity* / *Strike tone*.