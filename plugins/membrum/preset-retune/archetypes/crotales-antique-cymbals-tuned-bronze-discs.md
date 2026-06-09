# Membrum Recipe — Crotales (antique cymbals)

**Archetype:** Crotales / antique cymbals — small, thick, center-weighted tuned **bell-bronze** discs (~10 cm), struck with hard mallets, producing a clear, long-ringing high pitch with a *relatively harmonic* upper spectrum. Hi/lo pads are the **same instrument at two pitches** (only Size differs).

**Body = Bell · Exciter = Mallet**

## Why Bell + Mallet
The acoustic literature on orchestral crotales (Deutsch, Ramirez et al., *JASA* 116:2427-33, 2004; water-crotales follow-up *JASA* 131:935) identifies three dominant modes:

| Mode | Ratio vs (2,0) | Meaning |
|------|----------------|---------|
| (2,0) | 1.0 | fundamental / marked pitch |
| (3,0) | **2.0** (exact octave when ideally tuned) | strongest radiator |
| (4,0) | **3.5** (7:2); (4,0)/(3,0)=1.75 = minor 7th | bright upper partial |

That is a *relatively harmonic* upper spectrum dominated by an octave — unlike a wash cymbal. Membrum's **Bell** body (Chladni partials … nominal 1.0, then 1.5, **2.0**, 2.6, 3.2 …) is the only model that supplies a strong octave partial + long-ringing highs. Crotales are struck with hard mallets → **Mallet** exciter. Noise content is essentially nil (clean idiophone) and there is no large-amplitude pitch glide (small thick disc stays linear), so the noise layer, tension mod, and pitch envelope are all off.

> **Model caveat (physically honest):** Bell `f0_nominal = 800·0.1^size` caps at **800 Hz** (size 0). Real crotales are marked C6-C8 (≈1047-4186 Hz for the (2,0)). The literal marked fundamental therefore cannot be reached; with Size small the *audible octave/upper partials* land ~1.0-1.4 kHz, which reads convincingly as a "high bell." Keep Size small; do **not** widen it chasing pitch (that just makes it a low bell).

## Baseline (normalized) — shared except Size/Pan/FM (inert)
| Param | Norm | Denorm / physical |
|---|---|---|
| Exciter Type (0) | 0.20 | Mallet |
| Body Model (1) | 0.80 | Bell |
| Material (2) | 0.92 | very metallic (bell brightness ≈0.98) |
| **Size (3) HI** | **0.12** | nominal ≈607 Hz (octave partial ≈1.2 kHz) |
| **Size (3) LO** | **0.22** | nominal ≈482 Hz |
| Decay (4) | 0.85 | ≈2.1× → ~2.8 s ring |
| Strike Pos (5) | 0.10 | near antinode, full partial set |
| Level (6) | 0.72 | bright but single ting; bus headroom |
| Body Damping b1 (50) | 0.30 | 15.1 s⁻¹ flat floor (long RT60) |
| Body Damping b3 (51) | 0.00 | pure flat = metal, long highs |
| Click Mix (47) | 0.42 | mallet contact tick |
| Click Contact (48) | 0.20 | 2.6 ms (hard beater) |
| Click Brightness (49) | 0.85 | ≈6.5 kHz bright tick |
| Noise Mix (42) | 0.00 | layer off (clean idiophone) |
| Air Loading (52) | 0.00 | no-op on Bell |
| Mode Stretch (21) | 0.333 | unstretched (keeps the harmonic octave) |
| Decay Skew (22) | 0.58 | +0.16: highs decay a touch faster |
| Pan (64) | 0.62 HI / 0.38 LO | spread the row |
| FM Ratio (32) | 0.55 | **inert under Mallet** |

## Decay realism
Bowed crotales sustain 8-12 s; **struck** (this recipe) rings several seconds with low/principal modes 3-8 s and highs <0.5 s. `Decay 0.85 + b1 0.30 (long floor) + b3 0 (metallic highs) + Decay Skew +0.16` gives a bright metallic attack settling to a pure, long ring — the crotale signature.

## Left at default (coverage policy)
Pitch envelope (Time=0, off), Drive/Fold (0), ToneShaper filter (cutoff 1.0 bypass, env 0.5), Mode Inject (0), Nonlinear Coupling (0), Morph (off), Mode Scatter (0), Secondary/Coupling (off), all five Macros (neutral 0.5), Feedback/NoiseBurst/Friction params (inert under Mallet), Choke (0), Output Bus (0 main), Pad Enabled (1). Each is either a no-op for a clean struck bronze disc or a performance overlay that would only obscure the explicit voicing.

## Sources
- Crotales overview, material, range, playing — https://en.wikipedia.org/wiki/Crotales ; https://kolberg.com/en/Antique-cymbal-crotales-Concert/2380 ; https://timpano-percussion.com/products/kolberg-2380-2-octaves-crotales
- Mode structure ((2,0)/(3,0)/(4,0) ratios, octave + 7:4) — Deutsch & Ramirez, *JASA* 116 (2004), https://scholarship.rollins.edu/stud_fac/3/ ; water-crotales https://pubs.aip.org/asa/jasa/article-abstract/131/1/935 ; spectrum figure https://www.researchgate.net/figure/Typical-power-spectrum-of-a-crotale-The-three-acoustically-important-modes-are-clearly_fig1_8187545
- Decay/sustain — https://www.alibaba.com/product-insights/how-to-choose-the-best-crotale-cymbal-a-complete-buying-guide.html
- Cymbal/bronze inharmonicity & modes — https://en.wikipedia.org/wiki/Inharmonicity ; https://oemcymbal.com/understanding-cymbal-vibration-modes/