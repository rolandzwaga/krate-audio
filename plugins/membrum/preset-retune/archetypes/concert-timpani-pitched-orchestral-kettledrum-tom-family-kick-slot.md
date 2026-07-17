# Membrum Recipe — Concert Timpani (pitched orchestral kettledrum)

**Body:** Membrane (48-mode Bessel drumhead) · **Exciter:** Mallet (felt beater)

A single struck head over a kettle. The defining trick is **full air loading**, which
coaxes the diametric (m,1) modes onto Rossing's near-harmonic series so the ear fuses a
clear pitch. Single head → **secondary shell OFF**. Level trimmed for headroom.

## Acoustics (researched)
- **Perceived pitch** = (1,1) one-nodal-diameter mode (the (0,1) "thup" is under-struck).
- Orchestral range ≈ **D2–A3 (73–220 Hz)**; this voice settles ~**85–180 Hz** (26–32" kettles). Reid/SoS: large kettle principal ≈150 Hz.
- **Air-loaded (m,1) ratios** (Rossing 1982): **1 : 1.50 : 2.00 : 2.44 : 2.90** ≈ harmonics 2:3:4:5:6.
- **Attack amplitudes ≈ 5:4:3:1** (modes 1→4) → **edge strike (~1/4 radius from rim)**.
- **Decay:** principal decays *faster* (~45/73/91/84% modes 1–4) but the note rings ~1–3 s; inharmonic HF dies fast.
- **Transient:** soft felt-mallet contact = brief dark colored-noise burst (few ms), no sharp tick.
- **Noise:** low, dark head/air chuff. **Glide:** energy-driven tension pitch-up-then-settle ("kerthump"), no synthetic 808 boom.
- **Material:** Mylar/calf head over copper kettle — woody.

## Normalized baseline (per-pad)
| Param | Norm | Physical target |
|---|---|---|
| Exciter Type | 0.20 | Mallet |
| Body Model | 0.00 | Membrane |
| Material | 0.32 | woody head, ~0.36 s base decay |
| Size | 0.90 | f0≈63 Hz → natural (1,1)≈100 Hz, large kettle |
| Decay | 0.82 | ~2.3× base → long musical ring |
| Strike Position | 0.83 | r/a≈0.75, edge strike (mapper measures from CENTER: r/a = pos×0.9; the old 0.28 was a near-center strike that buried the pitch-carrying (1,1) mode −6.6 dB under the pitchless (0,1)) |
| Level | 0.68 | linear; headroom trim (N-1) |
| PitchEnv Start | 0.477 | 180 Hz |
| PitchEnv End | 0.314 | 85 Hz |
| PitchEnv Time | 0.10 | 50 ms |
| PitchEnv Curve | 0.35 | curveAmt −0.30 (fast initial drop) |
| **Air Loading** | **1.00** | (m,1) → 1:1.5:2:2.44:2.9 Rossing series |
| Tension Mod | 0.16 | ~+0.3–0.5 st transient rise (kerthump) |
| Body Damping b1 | 0.12 | ~6.2 s⁻¹, long low-mode ring |
| Body Damping b3 | 0.10 | 1e-4, kill inharmonic HF hash |
| Mode Scatter | 0.06 | ~6% dither (3rd partial slightly flat) |
| Click Mix | 0.32 | soft mallet contact |
| Click Contact | 0.40 | 3.2 ms |
| Click Brightness | 0.32 | ~660 Hz (dark) |
| Noise Mix | 0.10 | faint dark chuff |
| Noise Cutoff | 0.40 | ~850 Hz LP |
| Noise Color | 0.20 | Brown |
| Noise Decay | 0.20 | ~45 ms |
| Pan | 0.50 | center |
| Pad Enabled | 1.00 | on |

## Left at default (physical reason)
- **Secondary shell OFF** (couplingStrength 0, secondaryEnabled 0): a timpano has ONE struck head, no shell ring; enabling it doubles RMS and clips.
- **Drive 0 / Fold 0 / Mode Inject 0 / Nonlinear Coupling 0:** timpani are clean & linear; the membrane already makes the full tuned series.
- **Mode Stretch 0.333 / Decay Skew 0.5 (neutral):** air-loading table already sets correct ratios; b1/b3 already set the decay ordering.
- **Filter bypassed (cutoff 1.0), filter-env flat:** no post-body EQ or sweep on a kettledrum.
- **Morph off, Choke 0, Bus main, all macros 0.5 (neutral):** static head material, free ring, standard routing.
- **PitchEnv Knee off:** single-segment settle.
- **FM/Feedback/NoiseBurst/Friction params:** no-ops under the Mallet exciter.

## Kit note
The kick-slot timpano (pad 0) and the 5-pad tuned row are the **same instrument** at
different pitches: keep every param identical and shift only **PitchEnv Start/End** (and
optionally Size) per pad. Suggested settle pitches for a D2–A3 row: 73, 87, 98, 116, 146 Hz,
each with Start ≈ 1.5–2× the End for the energy settle.

## Citations
- Rossing, "The Physics of Kettledrums," Scientific American (1982); Fletcher & Rossing, *The Physics of Musical Instruments*, ch.18.
- The Well-Tempered Timpani — preferred modes & air loading: wtt.pauken.org/chapter-3/preferred-modes, /air-loading, /air-loading-2.
- Reid, "Practical Percussion Synthesis: Timpani," Sound on Sound (mode ratios 1:1.5:1.98:2.44, amps 5:4:3:1, decay 45/73/91/84%).
- Avanzini, Marogna & Bank, JASA 2012 (energy-dependent tension pitch modulation).
- Chaigne & Askenfelt 1994 / Chaigne & Lambourg 2001 (modal damping law).
- Membrum AUDIT-signal-path-2026-06-07 (§3-A tension, §N-1 measured-strike norm / gain-staging, M-9 pan).