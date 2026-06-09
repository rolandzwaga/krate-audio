<!-- verdict: pass-with-fixes | coverageOk: true | issues fixed: 10 | IMPLEMENTED: 2026-06-09 (commit re-tune Cajon and Frames) -->

# Membrum Kit Re-Design — "Cajon and Frames" (Percussive · cajonFramesKit())

Corrected, post-audit re-voice. 20 crafted pads across 5 bodies (Plate, Membrane, Bell, Shell, NoiseBody) and 4 exciters (Impulse, Mallet, NoiseBurst, FMImpulse, Friction). All values are NORMALIZED [0,1].

> Verification fixes applied (vs the original proposal): Pad 4 Cajon Snare-Side Size 0.58->0.27 (bright ~430 Hz slap tone, per archetype), Strike Position 0.50->0.70 (edge strike), Decay 0.55->0.34, Filter Cutoff 0.78->0.884 (~9 kHz), Filter Env Amount 0.65->0.5 (no sweep), Noise Color 0.85->0.90; Pad 5 Riq Decay Skew default->0.55 (archetype value) and Noise Color 0.85->0.90. Pads 6/8 frame-drum secondary shell kept but flagged as a deliberate divergence from the archetype (which leaves the shell off).

## Layout (GM map, MIDI 36..67)

| Pad | Drum | Body | Exciter | Family |
|----|------|------|---------|--------|
| 0 | Cajon Bass | Plate | Impulse | Cajon |
| 2 | Cajon Slap | Plate | Impulse | Cajon |
| 4 | Cajon Snare-Side | Plate | NoiseBurst | Cajon |
| 6 | Frame Drum tap | Membrane | Mallet | Skin |
| 8 | Frame Drum slap | Membrane | Mallet | Skin |
| 10 | Bodhran | Membrane | Impulse | Skin |
| 12 | Dholak HI | Membrane | Impulse | Skin |
| 14 | Dholak LO | Membrane | Impulse | Skin |
| 5 | Riq | Membrane | Impulse | Skin/Metal |
| 7 | Pandeiro shake | NoiseBody | NoiseBurst | Shaker |
| 9 | Conga (open) | Membrane | Impulse | Hand drum |
| 11 | Bongo macho | Membrane | Impulse | Hand drum |
| 13 | Agogo HI | Bell | FMImpulse | Metal |
| 15 | Agogo LO | Bell | FMImpulse | Metal |
| 16 | Clave | Shell | Impulse | Wood |
| 17 | Guiro | NoiseBody | Friction | Scrape |
| 18 | Cabasa | NoiseBody | NoiseBurst | Shaker |
| 19 | Conga Slap | Membrane | Impulse | Hand drum |
| 1,3,20-31 | (disabled) | — | — | — |

## Gaps fixed
- 3 cloned tabla fillers (current pads 9/11/13) -> Conga / Bongo / Agogo (distinct roles), fixing audit s4 "one timbre stamped with a size sweep".
- Added the kit's first Bell (agogo), Shell (clave), Friction (guiro), and membrane-slap (conga slap) voices.
- mode_inject (1/k) used once (cajon-snare 0.12); decaySkew per-mode tilt now live on Plate/Bell/NoiseBody; pan spread 0.34-0.64; choke groups 1 (pandeiro) and 2 (cabasa).

## Per-pad exact values

### Pad 0 — Cajon Bass (Plate/Impulse)
material 0.30 · size 0.80 (~126 Hz, cajon-kick band) · decay 0.42 · strikePos 0.50 · modeStretch 0.42 · decaySkew 0.62 · pitchEnv 165->85 Hz / 35 ms / curve 0.35 · couplingStrength 0.45 · secondaryEnabled 1 · secSize 0.55 · secMat 0.30 · click 0.35/contact 0.20/bright 0.45 · b1 0.30 · b3 0.10 · scatter 0.18 · level 0.85 · pan 0.50. Defaults: airLoading/tensionMod (no-op Plate), modeInject/NLC 0, drive/fold/filter off, noise 0, macros 0.5. (Size delta from archetype 0.965->0.80 is intentional: keeps body weight under the post-audit measured-strike norm while staying in the cited 80-140 Hz cajon-kick band.)

### Pad 2 — Cajon Slap (Plate/Impulse)
material 0.42 · size 0.65 (~179 Hz edge tone) · decay 0.22 · strikePos 0.10 · modeStretch 0.42 · decaySkew 0.35 · pitchEnv 280->220 / 20 ms · click 0.85/contact 0.10/bright 0.78 · b1 0.30 · b3 0.10 · scatter 0.18 · macroPunch 0.85 · level 0.85 · pan 0.58.

### Pad 4 — Cajon Snare-Side (Plate/NoiseBurst)  [CORRECTED]
material 0.50 · **size 0.27 (~430 Hz bright slap tone, per archetype)** · **decay 0.34** · **strikePos 0.70 (edge strike)** · noiseBurstDur 0.231 (~5 ms) · modeStretch 0.42 · decaySkew 0.40 · **modeInject 0.12** · nonlinearCoupling 0.22 · drive 0.25 (flavour) · **filter LP / cutoff 0.884 (~9 kHz) / envAmt 0.5 (no sweep)** · noise 0.75/cut 0.82/dec 0.40/**color 0.90 violet** · click 0.72/contact 0.167/bright 0.786 · pitchEnv 210->140 / 70 ms / curve 0.15 · b1 0.55 · b3 0.18 · scatter 0.30 · couplingStr 0.62 · sec 1/0.55/0.45 · couplingAmt 0.62 · level 0.85 · pan 0.50. Defaults: airLoading/tensionMod (no-op Plate), Fold (Drive carries the grit), Morph/PitchEnv-knee, macros 0.5.

### Pad 6 — Frame Drum tap (Membrane/Mallet)
material 0.36 · size 0.78 · decay 0.50 · strikePos 0.35 · **airLoading 0.85** · decaySkew 0.62 · b3 0.55 · tensionMod 0.20 · click 0.40/contact 0.30/bright 0.40 · noise 0.18/color 0.40 · scatter 0.18 · couplingStr 0.30 · sec 1/0.40/0.32 · b1 0.30 · level 0.80 · pan 0.46. NOTE: the secondary frame-shell (couplingStr 0.30 + sec on) is a DELIBERATE kit-character divergence from the frame-drum archetype, which leaves the shell off ("a frame drum has a rim, not a resonant shell"); kept light as a thin wooden-frame undertone inherited from the current kit. Defaults: modeStretch/modeInject/NLC neutral, pitchEnv off (Time 0), drive/fold/filter off, macros 0.5.

### Pad 8 — Frame Drum slap (Membrane/Mallet)
as pad 6 + decay 0.22 · strikePos 0.08 (edge) · decaySkew 0.45 · b3 0.40 · click 0.85/bright 0.65/contact 0.12 · pan 0.54. (Same frame-shell divergence note as pad 6.)

### Pad 10 — Bodhran (Membrane/Impulse)
material 0.45 · size 0.72 (~95 Hz) · decay 0.40 · strikePos 0.40 · **airLoading 0.78** · decaySkew 0.45 · b1 0.30 · b3 0.10 · **tensionMod 0.30** · click 0.65/contact 0.18/bright 0.62 · noise 0.18/color 0.40/cut 0.45 · couplingStr 0.32 · sec 1/0.42/0.30 · scatter 0.20 · level 0.80 · pan 0.50.

### Pad 12 — Dholak HI (Membrane/Impulse)
material 0.42 · size 0.55 (~141 Hz) · decay 0.30 · strikePos 0.62 (edge) · pitchEnv 320->220 / 25 ms / curve 0.15 · decaySkew 0.58 · NLC 0.12 · filter LP bypass (cutoff 1.0) · noise 0.14/color 0.45 · click 0.42/bright 0.55 · b1 0.34 · b3 0.12 · airLoading 0.45 · scatter 0.10 · couplingStr 0.30 · sec 1/0.45/0.35 · tensionMod 0.16 · level 0.80 · pan 0.58.

### Pad 14 — Dholak LO (Membrane/Impulse)
material 0.33 · size 0.65 (~112 Hz) · decay 0.40 · strikePos 0.30 · pitchEnv 220->140 / 80 ms / curve 0.15 · decaySkew 0.66 · NLC 0.18 · filter LP / 0.82 (~6.4 kHz) · noise 0.10/color 0.20 · click 0.32/bright 0.30 · b1 0.28 · b3 0.18 · airLoading 0.60 · scatter 0.10 · couplingStr 0.40 · sec 1/0.55/0.35 · tensionMod 0.34 · level 0.85 · pan 0.42.

### Pad 5 — Riq (Membrane/Impulse)  [CORRECTED]
material 0.40 · size 0.52 (~151 Hz doum) · decay 0.34 · strikePos 0.60 · **decaySkew 0.55 (+0.10 high-mode tilt, per archetype)** · scatter 0.20 · airLoading 0.45 · b1 0.32 · b3 0.12 · noise 0.60/cut 0.82/reso 0.30/dec 0.50/**color 0.90 violet** · click 0.65/contact 0.30/bright 0.85 · couplingStr 0.40 · **sec 1/0.20/0.85 (metallic jingle)** · macroComplexity 0.62 · level 0.78 · pan 0.50. Defaults: modeStretch neutral (true Bessel), modeInject/NLC/tensionMod 0, pitchEnv off, drive/fold/filter off.

### Pad 7 — Pandeiro shake (NoiseBody/NoiseBurst)
material 0.90 · size 0.12 · decay 0.10 · strikePos 0.30 · noiseBurstDur 0.40 · noise 0.80/cut 0.92/reso 0.20/dec 0.18/color 0.88 · click 0.20/bright 0.85 · scatter 0.45 · b1 0.32 · b3 0.0 · **choke 0.125 (group 1)** · airLoading 0 · level 0.72 · pan 0.62. Defaults: decaySkew neutral, pitchEnv/tension/modeInject/NLC/coupling/secondary off.

### Pad 9 — Conga open (Membrane/Impulse)
material 0.45 · size 0.62 (~120 Hz) · decay 0.40 · strikePos 0.40 · pitchEnv 200->150 / 20 ms / curve 0.15 · airLoading 0.50 · b1 0.30 · b3 0.10 · tensionMod 0.20 · click 0.50/contact 0.20/bright 0.55 · noise 0.10/cut 0.40/color 0.40 · scatter 0.10 · couplingStr 0.30 · sec 1/0.45/0.40 · level 0.80 · pan 0.40.

### Pad 11 — Bongo macho (Membrane/Impulse)
material 0.55 · size 0.32 (~239 Hz natural) · decay 0.28 · strikePos 0.30 · pitchEnv 420->350 / 20 ms / curve 0.15 · tensionMod 0.22 · airLoading 0.42 · b1 0.30 · b3 0.10 · click 0.55/contact 0.15/bright 0.72 · noise 0.10 · scatter 0.10 · couplingStr 0.25 · sec 1/0.30/0.40 · level 0.80 · pan 0.60.

### Pad 13 — Agogo HI (Bell/FMImpulse)
**fmRatio 0.72 (mod ratio 3.16)** · size 0.14 (~580 Hz) · material 0.85 · decay 0.28 · b1 0.30 · **b3 0.0** · strikePos 0.30 · modeStretch 0.45 · decaySkew 0.40 · scatter 0.12 · click 0.55/contact 0.30/bright 0.85 · macroBrightness 0.65 · level 0.75 · pan 0.42. Defaults: airLoading/tensionMod/pitchEnv off (no-op on Bell / Time 0), modeInject/NLC/noise/coupling/secondary 0, FM-only exciter so Feedback/NoiseBurst/Friction inert.

### Pad 15 — Agogo LO (Bell/FMImpulse)
fmRatio 0.55 (mod ratio 2.65) · size 0.22 (~480 Hz, ~major-third below HI) · material 0.85 · decay 0.35 · b1 0.30 · b3 0.0 · strikePos 0.30 · modeStretch 0.45 · decaySkew 0.40 · scatter 0.12 · click 0.55/bright 0.85/contact 0.30 · macroBrightness 0.65 · level 0.75 · pan 0.58. Defaults as pad 13.

### Pad 16 — Clave (Shell/Impulse)
material 0.85 · size 0.0 (f0 1500 Hz, 2.757x ~4.1 kHz) · decay 0.12 · **b3 0.70 override** · strikePos 0.12 (near free-end antinode, post-audit free-free shape) · click 0.55/contact 0.15/bright 0.82 · modeStretch 0.333 (preserve free-free 1:2.76:5.40 ratios) · level 0.80 · pan 0.36. Defaults: b1 sentinel (ring set by Decay, b3 dominates HF), decaySkew/scatter neutral/0, pitchEnv/modeInject/NLC/noise/coupling/secondary/airLoading off, filter bypass, macros 0.5.

### Pad 17 — Guiro (NoiseBody/Friction)
material 0.65 · size 0.18 (~991 Hz) · decay 0.30 · strikePos 0.30 · **frictionPressure 0.55** · noise 0.65/cut 0.62 (~2.25 kHz)/color 0.55/dec 0.30/reso 0.20 · decaySkew 0.55 · b1 0.42 · b3 0.0 · scatter 0.30 · **click 0.0** (scrape has no beater) · airLoading 0 · level 0.70 · pan 0.64. Defaults: pitchEnv/tension/modeInject/NLC off, drive/fold/filter off, coupling/secondary off, FM/Feedback/NoiseBurst inert under Friction.

### Pad 18 — Cabasa (NoiseBody/NoiseBurst)
material 0.85 · size 0.08 (~1.2 kHz) · decay 0.08 · noiseBurstDur 0.20 (~4.6 ms) · noise 0.85/cut 0.73 (~3.4 kHz)/reso 0.16/dec 0.12/color 0.75 · click 0.0 · b1 0.55 · b3 0.0 · **choke 0.25 (group 2)** · scatter 0.30 · airLoading 0 · level 0.62 · pan 0.34. Defaults: pitchEnv/tension/modeInject/NLC off, coupling/secondary off, drive/fold/filter off, FM/Feedback/Friction inert.

### Pad 19 — Conga Slap (Membrane/Impulse)
material 0.55 · size 0.50 (~158 Hz, edge pushes centroid up) · decay 0.18 · strikePos 0.10 (edge) · click 0.85/contact 0.10/bright 0.85 · noise 0.15/cut 0.70/dec 0.18/color 0.78 · b1 0.45 · b3 0.10 · airLoading 0.40 · scatter 0.30 · couplingStr 0.20 · sec 1/0.40/0.40 · macroPunch 0.85 · tensionMod 0 (choked slap, no kerthump) · level 0.85 · pan 0.40. Defaults: pitchEnv off, modeStretch/decaySkew neutral (short Decay does the work), modeInject/NLC off, drive/fold/filter off.

## Kit globals (keep current, lightly raised coupling for the buzz network)
maxPoly 12 · globalCoupling 0.18 · snareBuzz 0.20 · tomResonance 0.30 · couplingDelayMs 1.0. The cajon-snare (couplingAmount 0.62) and riq are the main participants in the sympathetic-buzz network.

---

## Verification log (10 issues found & fixed)

1. PHYSICAL CORRECTNESS / archetype mismatch — Pad 4 Cajon Snare-Side Size: proposal used Size 0.58 (f0 = 800*0.1^0.58 ~= 210 Hz, a low/boxy tone) with the rationale 'kept current 0.58'. The cajon-snare-side archetype explicitly sets Size 0.27 -> ~430 Hz because the snare-SIDE is the bright EDGE/SLAP tone, 'deliberately above the ~80-130 Hz bass zone'. Keeping the un-revoiced current value defeats the bright-slap identity. FIX: Size 0.27 (~430 Hz).

2. PHYSICAL CORRECTNESS — Pad 4 Cajon Snare-Side Strike Position: proposal used 0.50 (center) 'slap zone, balanced'. A snare-side slap is an EDGE strike; the archetype sets Strike Position 0.70 (edge). Center strike weights the (low) axisymmetric plate modes — wrong for the bright wire-buzz slap. FIX: Strike Position 0.70.

3. ARCHETYPE CONSISTENCY — Pad 4 Cajon Snare-Side Decay: proposal used 0.55; archetype uses 0.34 (the b1=0.55 override sets the actual flat-damping floor, so a long Decay knob just inflates the legacy-fallback decay and lengthens an already tight, body-damped hand-played tail). FIX: Decay 0.34.

4. ARCHETYPE CONSISTENCY — Pad 4 Cajon Snare-Side filter: proposal listed Filter Cutoff 0.78 (~4.8 kHz) and Filter Env Amount 0.65. Archetype sets LP cutoff 0.884 (~9 kHz) and leaves the filter ENV at default 0.5. Env amount 0.65 denormalizes to bipolar +0.30 = an UPWARD sweep, but the rationale said 'downward sweep tightens tail' — contradictory, and the archetype intentionally uses no env sweep (the LP corner alone tames burst fizz). FIX: Filter Cutoff 0.884 (~9 kHz), Filter Env Amount 0.5 (no sweep, per archetype).

5. ARCHETYPE CONSISTENCY — Pad 4 Cajon Snare-Side Noise Color: proposal 0.85; archetype 0.90. Both decode to the Violet band (>0.80) so functionally identical, but aligned to the archetype 0.90 for exactness.

6. COVERAGE (meaningful param silently defaulted) — Pad 5 Riq Decay Skew: proposal left Decay Skew at neutral default claiming 'b3 carries the tilt'. The riq archetype explicitly sets Decay Skew 0.55 (+0.10 mild high-mode lift) — it is a meaningful, archetype-specified Membrane param and per-mode tilt is live post-M-5. FIX: Decay Skew 0.55. Also aligned Riq Noise Color 0.85 -> 0.90 (Violet, per archetype).

7. DOCUMENTED DIVERGENCE (kept, flagged) — Pads 6 & 8 Frame Drum: proposal adds a secondary (frame) shell (couplingStrength 0.30, secondaryEnabled 1, secSize 0.40, secMat 0.32). The frame-drum archetype explicitly leaves the secondary shell OFF ('a frame drum has a rim, not a resonant shell'). This is a defensible kit-character delta inherited from the current pad 6 (a light wooden-frame undertone), so it is KEPT, but it is now explicitly flagged as a deliberate divergence from the archetype rather than an archetype-derived value.

8. VERIFIED OK (no change) — Pad 0 Cajon Bass Size 0.80 (~126 Hz) vs archetype 0.965 (~87 Hz): the proposal's documented delta is physically justified (126 Hz sits inside the cited 80-140 Hz cajon-kick band and preserves body weight under the post-audit measured-strike norm). Kept.

9. VERIFIED OK (no change) — Bodies/exciters all correct per archetype: Cajon trio Plate (Impulse/NoiseBurst), skin drums Membrane (Mallet for frame/soft, Impulse for bodhran/dholak/conga/bongo/riq), agogos Bell+FMImpulse (fmRatio 0.72/0.55 -> mod ratio 3.16/2.65, correct), clave Shell+Impulse with b3 0.70 override, guiro NoiseBody+Friction (frictionPressure 0.55), pandeiro/cabasa NoiseBody+NoiseBurst with choke groups 1/2 (norm 0.125/0.25 decode to groups 1/2 correctly). airLoading/tensionMod only on Membrane pads; mode_inject used once (cajon-snare 0.12); Drive 0.25 as flavour on the snare-side — all post-audit-correct.

10. VERIFIED OK — Layout/gaps: the 3 tabla-style clones (current pads 9/11/13) and the disabled pads 1,15-31 are confirmed against current-state.json; re-purposing 9/11/13 to Conga/Bongo/Agogo-HI and filling 15-19 with Agogo-LO/Clave/Guiro/Cabasa/Conga-Slap correctly fixes the audit s4 'one timbre stamped with a size sweep' duplication and the missing Bell/Shell/Friction/membrane-slap roles. All valueNorm in [0,1]; sentinels legal (clave Size 0.0, b3 0.0, choke groups).
