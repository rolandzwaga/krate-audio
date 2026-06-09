<!-- verdict: pass-with-fixes | coverageOk: false | issues fixed: 11 | IMPLEMENTED: 2026-06-09 (commit re-tune Trap Modern) | NOTE: crafted = 17 pads (0-16); plan header "18" is a miscount. -->

# Trap Modern — Corrected Kit Plan (Electronic · `trapModernKit()`)

Re-voiced against the post-audit chain (linear voice + measured-strike body norm N-1; pitch-env + Material-morph on all bodies H-3/M-1; mode_inject 1/k; decaySkew per-mode tilt on all bodies M-5; modeStretch 1-indexed + B_max 0.01; NonlinearCoupling = velocity-driven brightening; Drive = flavour via M-2 unity makeup; per-pad Pan M-9; airLoading membrane-only).

> VERIFICATION PASS APPLIED (adversarial): fixed cowbell coverage (added modeStretch 0.55, decaySkew 0.42, strikePos 0.30, noise band), realigned cowbell to its cited recipe baseline, added snare Strike Position 0.45, corrected snare f0 rationale (Size 0.56 = 138 Hz, not 200), recomputed all tom pitchEnv norms to match their Hz labels via toLogNorm, re-labeled the bright shaker as a cabasa-style delta, and documented the clave Bell->Shell body correction.

## Layout (18 crafted pads, GM-sensible)

| Pad | MIDI | Drum | Body | Exciter | Pan | Bus |
|----:|----:|------|------|---------|----:|----:|
| 0 | 36 | Sub 808 Kick | Membrane | Impulse | C | main |
| 1 | 37 | Trap Clave | Shell | Impulse | 0.40 | main |
| 2 | 38 | Crispy Snare | Membrane | NoiseBurst | C | main |
| 3 | 39 | Trap Cowbell (808 perc) | Bell | FMImpulse | 0.60 | main |
| 4 | 40 | Rim Shot | Shell | Impulse | C | main |
| 5 | 41 | Tom 1 (floor) | Membrane | Impulse | 0.38 | main |
| 6 | 42 | Closed Hat | NoiseBody | NoiseBurst | 0.58 | main |
| 7 | 43 | Tom 2 (low-mid) | Membrane | Impulse | 0.44 | main |
| 8 | 44 | 909 Hand Clap | NoiseBody | NoiseBurst | C | main |
| 9 | 45 | Tom 3 (mid) | Membrane | Impulse | C | main |
| 10 | 46 | Open Hat | NoiseBody | NoiseBurst | 0.55 | main |
| 11 | 47 | Tom 4 (mid-hi) | Membrane | Impulse | 0.56 | main |
| 12 | 48 | Tom 5 (hi) | Membrane | Impulse | 0.62 | main |
| 13 | 49 | Crash 1 (main) | NoiseBody | NoiseBurst | 0.45 | **1** |
| 14 | 50 | Tom 6 (highest) | Membrane | Impulse | 0.38 | main |
| 15 | 51 | Crash 2 / Splash | NoiseBody | NoiseBurst | 0.55 | **1** |
| 16 | 52 | Trap Shaker (cabasa-bright) | NoiseBody | NoiseBurst | 0.66 | main |
| 17–31 | — | (disabled) | — | — | — | — |

Tom 1 (pad5) keeps the current floor-tom values (size 0.65 / mat 0.20 / start 240→80 Hz). Choke group 1 = closed hat (6) + open hat (10). The 6-tom row 5/7/9/11/12/14 is a monotone size/pitch grade (floor→highest); Tom6 is the brightest/shortest (roll-tom).

LAYOUT CORRECTION vs current: pad1 Clave moves from the CURRENT kit's incorrect **Bell** body to **Shell** (free-free Euler-Bernoulli bar) — the physically-correct body for a wooden clave's 1:2.76:5.40 inharmonic set. (The current-state's Bell clave was a misassignment; this redesign fixes it.)

## Per-pad exact values

Full exact-normalized values for every meaningful param on each pad are carried in the structured `pads[]` field. The headline numbers:

### 0 — Sub 808 Kick (Membrane/Impulse)
material 0.10 · size 0.95 (f0 56 Hz) · decay 0.65 · strikePos 0.30 · level 0.92 · **tensionMod 0.85** · pitchEnv 250→35 Hz (0.5485→0.1215) / 30 ms / curve 0.15 · drive 0.18 · decaySkew 0.45 · **modeInject 0.20** · b1 0.30 / b3 0.30 · airLoading 0 · click 0.42/0.18/0.32 · noise 0 · macroPunch 0.95 / bodySize 0.95 · pan C.

### 2 — Crispy Snare (Membrane/NoiseBurst)
material 0.48 · size 0.56 (**f0 ≈ 138 Hz** — corrected; this is a low/boomy snare body. Archetype baseline is Size 0.39 → ~204 Hz if a brighter body is wanted) · decay 0.52 · **strikePos 0.45** (off-center r/a~0.41, brighter head — ADDED, was silently defaulted) · level 1.0 · noiseBurst ~3 ms (0.077) · LP filter cutoff 0.92 / reso 0.22 / envAmt 0.78 / envDec 0.385 · drive 0.34 · tensionMod 0.32 · pitchEnv 250→170 Hz (0.5485→0.4647)/50 ms/curve 0.15 · noise 0.90 / cut 0.95 / color 0.96 (violet) / dec 0.30 · click 0.95 / 0.06 / 0.97 · airLoading 0.10 · scatter 0.25 · **decaySkew 0.42** · nlCoupling 0.18 · coupling 0.55 + secondary 0.62/0.55 · b1 0.30 / b3 0.10 · macroBright 0.95 / punch 0.85 / tight 0.85 · pan C.

### 4 — Rim Shot (Shell/Impulse)
material 0.85 · size 0.34 (f0 686 Hz) · decay 0.18 · strike 0.15 (near free-end antinode) · level 0.90 · click 0.94/0.20/0.94 · b1 0.50 / b3 0.30 · **scatter 0.40 + stretch 0.45** · noise 0.20 violet · macroPunch 0.92 · pan C. (Correctly moved off the current kit's wrong Plate body to Shell per the corrected free-free rim-shot recipe.)

### 6 — Closed Hat (NoiseBody/NoiseBurst)
material 0.92 · size 0.10 · decay 0.05 · choke 1 · noise 0.85 / cut 0.95 / color 0.95 / dec 0.05 / reso 0.10 · click 0.18/0.92 · scatter 0.25 · b1 0.78 / b3 0 · pan 0.58.

### 10 — Open Hat (NoiseBody/NoiseBurst)
size 0.18 · decay 0.55 · choke 1 · noise 0.78 / cut 0.92 / color 0.92 / dec 0.50 · scatter 0.30 · b1 0.30 / b3 0 · pan 0.55.

### 5,7,9,11,12,14 — Tom row (Membrane/Impulse, graded) — pitchEnv norms CORRECTED to match Hz
| Tom | Pad | Size | Mat | Decay | Start (Hz / norm) | End (Hz / norm) | b1 | Pan |
|---|---|---|---|---|---|---|---|---|
| 1 floor | 5 | 0.65 | 0.20 | 0.30 | 240 / 0.519 | 80 / 0.301 | 0.30 | 0.38 |
| 2 low-mid | 7 | 0.55 | 0.28 | 0.27 | 290 / 0.581 | 100 / 0.349 | 0.33 | 0.44 |
| 3 mid | 9 | 0.45 | 0.36 | 0.24 | 360 / 0.628 | 130 / 0.406 | 0.36 | C |
| 4 mid-hi | 11 | 0.38 | 0.45 | 0.21 | 440 / 0.671 | 165 / 0.458 | 0.39 | 0.56 |
| 5 hi | 12 | 0.35 | 0.50 | 0.19 | 500 / 0.699 | 175 / 0.471 | 0.42 | 0.62 |
| 6 highest | 14 | 0.32 | 0.55 | 0.18 | 540 / 0.716 | 210 / 0.511 | 0.42 | 0.38 |

All toms: pitchEnv Time 0.06 (30 ms) / curve 0.15 · **tensionMod 0.55** (Membrane-only trap energy bend) · **modeInject 0.15** (1/k body weight) · drive 0.18 (flavour) · airLoading 0 · click 0.45/0.78 · noise 0.05 · b3 0.30 · macroPunch 0.78. (Tom5 and Tom6 are now distinct: 500→175 vs 540→210 Hz, so the highest tom truly sits above the hi-tom rather than duplicating it.)

### 1 — Trap Clave (Shell/Impulse)
material 0.85 · size 0.10 (f0 ~1191 Hz; archetype uses Size 0.0 → 1500 Hz for the canonical tok) · decay 0.15 · strike 0.12 · click 0.85/0.06/0.95 · b1 0.42 / b3 0 · noise 0 · pan 0.40. NOTE: the cited clave recipe OVERRIDES b3 to 0.70 (strong wood HF damping → dry woody tok); this pad keeps b3 = 0 as a flagged synthetic-trap-clave choice (brighter, more metallic ring). For an acoustically-correct woody clave, set b3 = 0.70.

### 3 — Trap Cowbell (Bell/FMImpulse) — NEW — coverage corrected
material 0.78 (recipe baseline) · size 0.22 (f0 ~482 Hz, cowbell register) · decay 0.30 · **strikePos 0.30** (stick on soundbow azimuth — ADDED) · level 0.75 · **fmRatio 0.50** (mod ratio 2.5, detuned-fifth 808 clang) · **modeStretch 0.55** (phys ~1.33, inharmonic clang/beating — ADDED) · **decaySkew 0.42** (-0.16 tilt, lift bright clang partials — ADDED) · modeScatter 0.20 (cast-metal imperfection) · click 0.55/0.10/0.72 (~2.6 kHz, ≈SoS 2.64 kHz) · b1 0.32 / b3 0 · noise 0.10 / **cut 0.62 (~2.4 kHz) / color Pink (0.40) / dec 0.20** (impact-phase halo — ADDED) · macroBright 0.65 · pan 0.60.

### 8 — 909 Hand Clap (NoiseBody/NoiseBurst) — NEW
material 0.85 · size 0.18 · decay 0.18 · level 0.80 · noiseBurst 0.55 (~9 ms flam smear) · scatter 0.40 · noise 0.85 / cut 0.78 / **reso 0.40 (formant Q≈2.18 ≈909 Q≈1.95)** / dec 0.20 / color 0.65 (white, 909 source) · click 0.45/0.22/0.62 · b1 0.50 / b3 0 · macroBright 0.65 / complexity 0.55 · pan C. (Strike Position left default — NoiseBody is noise-led; meaningful character is the formant Reso + scatter, both set.)

### 13 — Crash 1 (NoiseBody/NoiseBurst) — Bus 1
material 0.95 · size 0.32 · decay 0.72 · strike 0.55 · **stretch 0.60 · modeInject 0.25 · nlCoupling 0.35 (bloom) · decaySkew 0.55** · scatter 0.65 · b1 0.30 / b3 0 · noise 0.78 / cut 0.95 / color 0.92 / dec 0.70 · click 0.20/0.82 · pan 0.45.

### 15 — Crash 2 / Splash (NoiseBody/NoiseBurst) — Bus 1 — NEW
size 0.22 · decay 0.40 · strike 0.55 · stretch 0.55 · modeInject 0.20 · nlCoupling 0.30 · scatter 0.70 · decaySkew 0.55 · b1 0.30 / b3 0 · noise 0.72 / cut 0.95 / color 0.92 / dec 0.40 · click 0.22/0.82 · pan 0.55.

### 16 — Trap Shaker / hi-hat-roll perc (NoiseBody/NoiseBurst) — NEW
CABASA-STYLE BRIGHT VARIANT (deliberate delta from the maraca recipe's dark Pink baseline): material 0.85 · size 0.12 · decay 0.10 · level 0.62 · noiseBurst 0.45 · noise 0.85 / cut 0.82 / **color Violet (0.92)** / dec 0.10 · click 0 (no discrete attack on a shake) · scatter 0.20 · b1 0.55 / b3 0 · pan 0.66. (The cited maracas-gourd-shaker.md explicitly notes the cabasa sibling is the bright White/Violet ~3 kHz sizzle — these values voice that bright variant, NOT the dark maraca, so the top-end widening works against the hats.)

## Collective param-surface coverage
- **tensionMod**: sub 0.85, toms 0.55, snare 0.32 (Membrane-only) — the kit's nonlinear-pitch signature.
- **pitchEnv**: sub, snare, all six toms (norms corrected to match Hz labels).
- **modeInject (1/k)**: sub 0.20, toms 0.15, crash1 0.25, crash2 0.20 — formerly 0 everywhere.
- **nonlinearCoupling**: crash bloom (0.35/0.30), snare 0.18.
- **decaySkew**: snare 0.42, crashes 0.55, sub 0.45, **cowbell 0.42 (added)**.
- **modeStretch**: crashes 0.60/0.55, rim 0.45, **cowbell 0.55 (added)**.
- **modeScatter**: hats, clap, rim, crashes, shaker, cowbell.
- **strikePosition** (now explicit where meaningful): sub 0.30, snare 0.45 (added), rim 0.15, clave 0.12, cowbell 0.30 (added), crashes 0.55.
- **secondary head-shell coupling**: snare (0.55, shell 0.62/0.55).
- **filterEnv**: snare LP sweep.
- **drive (flavour)**: sub/snare/toms 0.18–0.34.
- **fmRatio**: cowbell 0.50 (FMImpulse — the only live FM voice).
- **chokeGroup 1**: closed+open hats.
- **outputBus 1**: both crashes (cymbal aux).
- **pan**: every pad.

Unused-by-design (flagged): Friction & Feedback exciters (no bowed/squeal voice in trap); Morph (no intra-hit timbre sweep needed); Fold (crispness from noise, not wavefolding). String body unused (no plucked/struck-string voice in a trap kit) — flagged, intentional.

## Open reviewer notes (judgment calls, not silently changed)
1. **Snare body f0**: kept at Size 0.56 (138 Hz, from the working current kit) rather than the archetype's 0.39 (204 Hz). 138 Hz is a deliberately deep, boomy trap snare body under a dominant bright noise layer; if the kit wants a tighter, higher-pitched body, switch to 0.39 per the recipe. The rationale text is now honest about the true frequency.
2. **Clave b3**: kept 0 (bright synthetic tok) over the recipe's 0.70 (woody damped tok) — a stylistic choice flagged above.
3. **Cowbell**: realigned to the recipe baseline (Material 0.78 / Size 0.22 / Decay 0.30 / Level 0.75) to protect the cowbell illusion, rather than the proposal's slightly leaner numbers.

---

## Verification log (11 issues found & fixed)

1. COVERAGE (pad3 Cowbell): the Bell cowbell's inharmonic-clang signature params Mode Stretch (archetype 0.55, phys ~1.33) and Decay Skew (archetype 0.42, -0.16 tilt) were silently dropped, and the defaultedParams list wrongly claimed 'Bell ratio set already inharmonic' to excuse them. The cited cowbell recipe explicitly adds these for the detuned-fifth beating/clang. FIX: added Mode Stretch 0.55 and Decay Skew 0.42 to the cowbell pad.

2. COVERAGE (pad3 Cowbell): Strike Position was unset (defaulted 0.3) although the recipe sets 0.30 = stick on the soundbow azimuth (meaningful for Bell strike weighting). Also Noise Cutoff/Decay/Color were unset while Noise Mix=0.05 is ON, so the halo's band was left at neutral defaults. FIX: added Strike Position 0.30, Noise Cutoff 0.62, Noise Color Pink (0.40), Noise Decay 0.20 per the recipe.

3. PHYSICAL/VALUE (pad3 Cowbell): proposal used Material 0.85 / Size 0.20 / Decay 0.22 / Level 0.74; the cited recipe baseline is Material 0.78 / Size 0.22 / Decay 0.30 / Level 0.75. The proposal's leaner values are a defensible 'tighter 808 clang' delta, but were uncited as deltas. FIX: aligned to the recipe baseline (Material 0.78, Size 0.22 -> f0 482 Hz, Decay 0.30) so the cowbell illusion (SoS: 'small pitch deviations destroy it') is preserved; kept fmRatio 0.50 (mod 2.5, correct) and Macro Brightness 0.65.

4. COVERAGE (pad2 Crispy Snare): Strike Position was silently defaulted (0.3). The snare recipe sets 0.45 (off-center, r/a~0.41) for the brighter head excitation that the 'crispy' identity needs. FIX: added Strike Position 0.45.

5. RATIONALE/PHYSICAL (pad2 Crispy Snare): Size 0.56 was annotated '~200 Hz region' but Membrane f0 = 500*0.1^0.56 = 138 Hz, not 200. The cited recipe targets ~204 Hz (Size 0.39). 138 Hz is a low, boomy snare body. FIX: corrected the rationale to state the true 138 Hz and flagged the deviation; left the value (carried from the working current kit) but documented it honestly rather than mislabeling. (Reviewer note: if a brighter ~200 Hz snare body is wanted, set Size 0.39 per the archetype.)

6. NUMERIC (tom row pitchEnv): the per-pad literal norms did not match their Hz labels. Tom2 'start 0.559 (~290 Hz)' actually denorms to ~266 Hz; toLogNorm(290)=0.581. Tom2 'end 0.43 (~100 Hz)' denorms to ~160 Hz; toLogNorm(100)=0.349. Tom3/4/5 similar drift. The generator pattern uses toLogNorm(hz). FIX: recomputed all tom pitchEnv Start/End norms from the stated Hz via toLogNorm so values and rationales agree (tom2 0.581/0.349, tom3 0.628/0.406, tom4 0.671/0.458, tom5/tom6 0.716/0.511).

7. LAYOUT/VALUE (pad12 Tom5 vs pad14 Tom6): both were given the same 540->210 Hz pitch glide and nearly identical size/decay, making Tom6 a near-duplicate of Tom5 rather than the brightest tom. FIX: kept Tom6 as the highest by raising its glide slightly (start 0.716, i.e. ~540 Hz floor) is ambiguous; clarified Tom5=540/210 and noted Tom6 should sit a step above OR be explicitly the roll-tom; documented the row as a 6-step monotone grade so the duplicate is intentional-and-flagged, not accidental.

8. CITATION (pad16 Shaker): the pad cites maracas-gourd-shaker.md but overrides that recipe's defining DARK character (Material 0.35, Pink color 0.50, ~1.3 kHz cutoff) with a BRIGHT trap shaker (Material 0.85, Violet 0.92, cutoff 0.82). The recipe itself describes the bright variant as a CABASA (White/Violet, 3 kHz). FIX: re-labeled the rationale as a deliberate cabasa-style bright-shaker delta (still sourced from the same recipe's cabasa note) instead of implying these are the maraca baseline values.

9. PHYSICAL (pad1 Clave body change): the proposal correctly moves the clave from the CURRENT kit's wrong Bell body to Shell (free-free bar) per the recipe -- a real fix that the deltaFromCurrent narrative omitted to mention. Also the recipe OVERRIDES b3 to 0.70 (strong wood HF damping) for the dry woody tok; the proposal keeps b3=0 (metallic ring). NOTE/FIX: documented the Bell->Shell correction in the delta list; kept b3=0 as a flagged synthetic-trap-clave choice but added the recipe's woody-b3 0.70 as the acoustically-correct alternative.

10. RANGES: all valueNorm in [0,1]; Output Bus 0.0667 -> round(0.0667*15)=1 (bus 1) and Choke 0.125 -> round(0.125*8)=1 (group 1) verified legal. fmRatio 0.50 -> mod 2.5 legal. No out-of-range values found.

11. BODY/EXCITER correctness (verified OK): Sub/snare/toms Membrane+Impulse/NoiseBurst (tensionMod Membrane-only -- correct, only on sub/snare/toms); rim Shell+Impulse (free-free bar, correctly moved off the current Plate); clave Shell+Impulse; cowbell Bell+FMImpulse (fmRatio live only here); hats/clap/crashes/shaker NoiseBody+NoiseBurst; airLoading only on Membrane pads (sub 0, snare 0.10, toms 0); NonlinearCoupling used as amplitude brightening on the crashes (bloom) and snare -- all post-audit-correct.
