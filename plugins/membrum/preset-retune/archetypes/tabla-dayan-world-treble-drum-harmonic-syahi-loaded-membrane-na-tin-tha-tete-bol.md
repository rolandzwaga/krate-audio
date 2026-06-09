# Membrum Recipe — Tabla Dayan (world / harmonic treble drum)

**Body:** Membrane · **Exciter:** Impulse (Mallet for the soft Tha) · covers Na / Tin / Tha / Tete as strike-position + articulation variants of one harmonic treble drum.

## Why this drum is special (acoustics)
The dayan (right/treble drum) is unique among membranophones: it is **near-harmonic and pitched**. The central black tuning paste, **syahi** (flour + water + iron filings), mass-loads the centre of the goatskin and pulls the normally inharmonic Bessel partials (j01:j11:j21 ≈ 1:1.59:2.14) toward **integer ratios** — Raman's classic five-harmonic model (fundamental + 4 overtones ≈ 1:2:3:4:5), refined by modern analysis to ~7 near-integer partials. The player damps the fundamental with a resting finger, so the perceived pitch is often a *missing fundamental* extrapolated from the 2nd/3rd harmonics ([chandrakantha](https://chandrakantha.com/music-and-dance/instrumental-music/indian-instruments/tabla/psychoaoustics/)).

- **Pitch:** small, high-tuned. Tin/Tun pitched stroke ~300 Hz with peaks ~550 & ~810 Hz ([UBC Phys341](https://wiki.ubc.ca/Course:Phys341_2020/Tabla)); the bright-dayan target here is 420→380 Hz.
- **Inharmonicity:** LOW — prized for tonal clarity. Keep modes unstretched.
- **Decay:** stroke-dependent — open (Na/Tin/Tun) ring; damped (Te/Tete, closed Tha) choked ([arXiv 1510.04880](https://arxiv.org/pdf/1510.04880)). High partials decay faster than the fundamental (+decaySkew).
- **Transient:** sharp, bright fingertip contact tick (1–3 ms) on edge bols; softer on the palm Tha.
- **Noise:** minimal — tonal drum, only faint skin/contact hiss.
- **Pitch glide:** small upward tension nudge on hard strikes (Avanzini–Marogna–Bank 2012), far subtler than a tom.

## Mapping onto Membrum (post-audit semantics)
The membrane bank uses true Bessel ratios, so the *syahi harmonic effect* is reproduced by two corrected stages: **airLoading** (0.45) pulls the low (m,1) modes toward Rossing's measured near-harmonic series, and **ModeInject** (0.30, now a clean 1/k integer series after audit §3-B) overlays a true harmonic series at f0 — the controllable stand-in for the syahi's pitch. The **pitchEnv Time>0** both seeds the exact 420 Hz strike pitch (audit H-3 made this work on all bodies) and gives a crisp 20 ms settle. **modeStretch stays at unity** (1.0) — never stretch this drum. **tensionMod** is kept small (0.25, Membrane-only). The wooden shell is a light head↔shell coupling (0.35) tuned just below the head.

### Baseline (open Dayan, normalized values)
| Param (offset) | Norm | Physical |
|---|---|---|
| Exciter (0) | 0.0 | Impulse |
| Body (1) | 0.0 | Membrane |
| Material (2) | 0.50 | mid skin brightness |
| Size (3) | 0.42 | f0 base ~190 Hz (rings to ~420 region) |
| Decay (4) | 0.58 | moderate open ring |
| StrikePos (5) | 0.40 | r/a 0.36 (open) |
| Level (6) | 0.80 | linear |
| PitchEnv Start (13) | 0.651 | 420 Hz |
| PitchEnv End (14) | 0.640 | 380 Hz |
| PitchEnv Time (15) | 0.04 | 20 ms (enables env) |
| PitchEnv Curve (16) | 0.30 | −0.4 fast drop |
| ModeStretch (21) | 0.3333 | unstretched (tonal) |
| DecaySkew (22) | 0.60 | +0.20 (highs damp faster) |
| ModeInject (23) | 0.30 | 1/k harmonic series = syahi tone |
| NonlinearCoupling (24) | 0.20 | light louder=brighter |
| Noise Mix (42) | 0.08 | faint skin hiss |
| Click Mix (47) | 0.55 | bright fingertip tick |
| Click Contact (48) | 0.12 | 2.4 ms |
| Click Brightness (49) | 0.75 | ~3.9 kHz |
| Body Damping b1 (50) | 0.30 | ~15 s⁻¹ |
| Body Damping b3 (51) | 0.05 | 5e-5 (keeps pitched highs) |
| AirLoading (52) | 0.45 | toward near-harmonic series |
| Mode Scatter (53) | 0.05 | tiny organic detune |
| Coupling Strength (54) | 0.35 | wooden shell |
| Secondary Enabled (55) | 1.0 | on |
| Secondary Size (56) | 0.35 | ~0.74× head |
| Secondary Material (57) | 0.45 | woody shell |
| TensionMod (58) | 0.25 | small upward nudge |

### Bol variants (deltas from baseline — strike position IS the bol)
- **Na** (open rim, ringing pitched edge): StrikePos **0.18**, Click Mix **0.55** / Brightness **0.78**, DecaySkew **0.65**, NonlinearCoupling **0.32**, TensionMod **0.18**; optional Material Morph (start 0.45→end 0.65) for a tap→bloom.
- **Tin** (edge, sharpest, near-pure tone): StrikePos **0.10**, Decay **0.22** (shorter), Click Mix **0.85** / Brightness **0.85**, DecaySkew **0.40**, TensionMod **0.15**.
- **Tha** (open palm, full): **Mallet** exciter, StrikePos **0.50**, Decay **0.45**, Click Mix **0.40** / Contact **0.30** (2.9 ms) / Brightness **0.45**.
- **Tete** (damped centre tap): StrikePos **0.10**, Decay **0.10** (choked), DecaySkew **0.30**, Click bright.

## Left at default (coverage policy)
ToneShaper filter (bypassed — body defines timbre), Drive/Fold (0 — clean tonal drum), PitchEnv knee/mid (single segment is enough), Material Morph (static hit, except Na), all 5 macros (neutral handles), noise filter shape (layer near-muted), FM/Feedback/Friction/NoiseBurst params (wrong exciter), Choke 0, Output Bus 0 (main), Pan 0.5 (centred; bayan carries the left), Coupling Amount 0.5 (kit-level), Pad Enabled 1.

**Sources:** [UBC Phys341 Tabla](https://wiki.ubc.ca/Course:Phys341_2020/Tabla) · [chandrakantha psychoacoustics](https://chandrakantha.com/music-and-dance/instrumental-music/indian-instruments/tabla/psychoaoustics/) · [Harmonic & Timbre Analysis of Tabla Strokes (arXiv 1510.04880)](https://arxiv.org/pdf/1510.04880) · [Categorization of Tablas by Wavelet Analysis (arXiv 1601.02489)](https://arxiv.org/pdf/1601.02489) · [Tabla Sound Quality (sangeetgalaxy)](https://sangeetgalaxy.co.in/paper/tabla-sound-quality-analyzing-acoustic-properties-material-selection-and-environmental-influences/)