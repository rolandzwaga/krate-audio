# Membrum Recipe — Acoustic Rack/Floor Tom (tunable, 6-pad size-graded row)

**Archetype:** One tunable acoustic tom rendered as a 6-pad row (size 0.8→0.4), i.e. a single drum re-tensioned across its range — low floor tom (~200→110 Hz) up to high rack tom (~470→290 Hz). Body family is identical across the row; only **Size**, **pitchEnv (Start/End/Time)**, **b1/b3**, **airLoading**, **secondarySize**, and **pan** are graded per pad.

**Body:** Membrane (Bessel circular drumhead, 48 modes) · **Exciter:** Mallet (soft beater) · **Coupled secondary:** ON (bottom-head/shell)

---

## Why these choices (physics → params)

A tom is a **tension-tuned circular membrane, not a timpani**: no fixed kettle pins the modes to a near-harmonic series, so the partials stay close to the **ideal-membrane Bessel ratios** — (0,1)=1.000, (1,1)=1.593, (2,1)=2.136, (0,2)=2.296, (3,1)=2.653, (1,2)=2.918, (0,3)=3.600 (Dan Russell/PSU; Fletcher & Rossing ch.18). The audible pitch is carried by the **(1,1)/(2,1) diametric modes**, which is why **Strike Position is off-center (~1/3 radius)** — a dead-center strike gives only the pitchless (0,1)/(0,2) thump.

- **Membrane body + Mallet exciter** — only physically-correct combo; Membrane is the *only* body that consumes the audit-corrected `airLoading` and `tensionMod`.
- **pitchEnv (290→180 Hz, 65 ms, exp drop) = the deterministic "tom drop"/kerthump**; **tensionMod 0.22** layers the nonlinear, velocity-driven upward "crack-and-relax" on top (Avanzini–Marogna–Bank JASA 2012). Together they make the iconic downward tom glide.
- **airLoading 0.55 (moderate, not full)** — a two-headed open tom is air-loaded but not kettle-pinned, so we deepen the low modes without forcing the timpani 1:1.5:2 harmonic series (Rossing; Well-Tempered Timpani).
- **b1 ≈17 s⁻¹ / b3 1e-4** — musical body RT60 (~0.5–0.7 s) with woody high-mode roll-off (Chaigne–Askenfelt). b1 is the real ring-length control; Decay seeds the legacy/cache.
- **Secondary shell ON, coupling 0.4** — models the bottom head + shell air ("woof").
- **Click layer (0.5, ~3 ms, ~2.4 kHz)** = beater contact thwack. **Noise layer low (0.16, pink, ~900 Hz)** = slight shell/air breathiness — no wires.
- **No Drive / Fold / Mode Inject / Morph / tone filter** — a clean acoustic tom; all motion comes from pitch + tension envelopes.

## Per-pad grading across the 6-pad row (low → high)

| Pad | Size | pitchEnv Start→End (Hz) | Time (ms) | b1 (norm) | airLoading | role |
|-----|------|--------------------------|-----------|-----------|-----------|------|
| 1 (low floor) | 0.80 | 200 → 110 | ~90 | 0.30 | 0.65 | floor tom |
| 2 | 0.70 | 250 → 130 | ~80 | 0.32 | 0.60 | |
| 3 | 0.60 | 300 → 150 | ~70 | 0.34 | 0.55 | |
| 4 (baseline) | 0.55 | 290 → 180 | 65 | 0.34 | 0.55 | mid rack (canonical) |
| 5 | 0.45 | 380 → 230 | ~50 | 0.37 | 0.48 | |
| 6 (high rack) | 0.40 | 470 → 290 | ~40 | 0.40 | 0.45 | high rack |

(Pan: spread ~0.38→0.62 across the row. Higher/smaller toms slightly tighter b1, less air loading, faster glide.)

## Per-kit voicing (same instrument family)

- **Jazz (softer):** Material ~0.50 (brighter/open), b3 ~0.08, b1 tighter (shorter), Noise ~0.10, Click Brightness ~0.6, tensionMod ~0.18.
- **Rock (bigger/longer):** b1 looser (longer ring), Material ~0.36, NonlinearCoupling ~0.18, tensionMod ~0.30, slightly deeper pitchEnv, Level up.
- **Wood (woodier/darker):** Material ~0.33, b3 ~0.14 (darker), Secondary Material up (more shell voice), Click Brightness ~0.5, Noise ~0.20.

## Baseline normalized values (mid-tom, pad 4)

Body Model 0.0 · Exciter 0.2 · Material 0.40 · Size 0.55 · Decay 0.50 · Strike 0.35 · Level 0.80 · Filter Cutoff 1.0 (bypass) · Drive 0.0 · Fold 0.0 · PitchEnv Start 0.5807 (290 Hz) · End 0.4771 (180 Hz) · Time 0.13 (65 ms) · Curve 0.15 (exp) · Mode Stretch 0.333 · Decay Skew 0.42 · Mode Inject 0.0 · Nonlinear Coupling 0.12 · Noise Mix 0.16 / Cutoff 0.45 / Reso 0.2 / Decay 0.3 / Color 0.35 (pink) · Click Mix 0.5 / Contact 0.32 / Brightness 0.55 · b1 0.34 (~17 s⁻¹) · b3 0.10 (1e-4) · Air Loading 0.55 · Mode Scatter 0.08 · Coupling Strength 0.40 · Secondary Enabled 1.0 · Secondary Size 0.32 · Secondary Material 0.55 · Tension Mod 0.22 · Pan 0.5 · Pad Enabled 1.0. (All macros, Morph, FM/Feedback/NoiseBurst/Friction, PitchEnv Knee/Mid left at defaults.)

> Implementation target: `tools/membrum_preset_generator.cpp` `acousticKit()` tom loop (pads {5,7,9,11,12,14}) — this recipe replaces/refines that loop with the audit-corrected semantics (note the existing loop uses `decay=0.5` + `bodyDampingB1=0.30+0.02i`; this recipe drives RT60 via b1 and adds the explicit pitchEnv drop the current acoustic toms omit).