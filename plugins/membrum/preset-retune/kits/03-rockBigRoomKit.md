<!-- verdict: pass-with-fixes | coverageOk: false | issues fixed: 8 | IMPLEMENTED: 2026-06-09 (commit re-tune Rock Big Room) -->

# Membrum Kit Re-Design — "Rock Big Room" (Acoustic) · `rockBigRoomKit()`

A large, ambient arena rock kit, re-voiced against the **corrected post-audit DSP** (measured-strike body norm, `1/k` mode-inject, per-mode decaySkew on all bodies, free-plate Chladni cymbals, membrane-only airLoading/tension, Drive-as-flavour with unity makeup, equal-power per-pad pan, fixed FM modulation-index range).

> **Verification pass applied (pass-with-fixes).** Five classes of fix were folded in: (1) every tom + kick pitch-env Start/End norm re-encoded to the engine's `hz = 20·100^norm` map so the stored value matches the stated Hz target (the original norms decoded ~15–25 % low); (2) explicit **Strike Position** added to every pad whose recipe sets a non-default, mode-shaping value (snare, all 6 toms, closed/open hat, crash, China, splash); (3) snare **b3** raised 0.04→0.10 (real Mylar HF damping); (4) tambourine **Macro Complexity 0.65** added; (5) ride **FM Ratio** moved to defaulted (no-op under NoiseBurst).

## Layout (20 crafted of 32; GM-map preserved)

| Pad | MIDI | Drum | Body | Exciter | Bus | Pan |
|----|----|----|----|----|----|----|
| 0 | 36 | Big-room Kick | Membrane | Mallet | main | 0.50 |
| 1 | 37 | Cross-stick (dry) | Shell | Impulse | main | 0.50 |
| 2 | 38 | Crack Snare | Membrane | NoiseBurst | main | 0.50 |
| 3 | 39 | Hand Clap | NoiseBody | NoiseBurst | main | 0.50 |
| 4 | 40 | Rim Shot (loud) | Shell | Impulse | main | 0.50 |
| 5 | 41 | Tom 1 (floor) | Membrane | Mallet | main | 0.66 |
| 6 | 42 | Closed Hat | NoiseBody | NoiseBurst | main | 0.40 |
| 7 | 43 | Tom 2 | Membrane | Mallet | main | 0.60 |
| 8 | 44 | Pedal Hat | NoiseBody | NoiseBurst | main | 0.40 |
| 9 | 45 | Tom 3 | Membrane | Mallet | main | 0.55 |
| 10 | 46 | Open Hat | NoiseBody | NoiseBurst | main | 0.40 |
| 11 | 47 | Tom 4 | Membrane | Mallet | main | 0.48 |
| 12 | 48 | Tom 5 | Membrane | Mallet | main | 0.41 |
| 13 | 49 | Crash 1 (sustain) | NoiseBody | NoiseBurst | aux 1 | 0.34 |
| 14 | 50 | Tom 6 (high rack) | Membrane | Mallet | main | 0.34 |
| 15 | 51 | Ride (bell ping) | Bell | NoiseBurst | aux 1 | 0.62 |
| 16 | 52 | Crash 2 / China | NoiseBody | NoiseBurst | aux 1 | 0.30 |
| 17 | 55 | Splash | NoiseBody | NoiseBurst | aux 1 | 0.66 |
| 18 | 56 | Cowbell | Bell | FMImpulse | main | 0.58 |
| 20 | 54 | Tambourine | NoiseBody | NoiseBurst | main | 0.55 |

Pads **19, 21–31 disabled** (documented gap; re-enable is an explicit user gesture per `disableUncraftedPads`).

Kit globals retained (already well-dialed): `maxPolyphony 12`, `globalCoupling 0.30`, `snareBuzz 0.35`, `tomResonance 0.45`, `couplingDelayMs 1.2`.

## Corrected per-pad values (only the fixed params shown; everything else is as proposed)

### Pad 0 — Big-room Kick (Membrane / Mallet)
- **PitchEnv Start 0.4508** → decodes to **~160 Hz** (rationale corrected; the recipe's real impact-thump start; *not* 180 Hz).
- PitchEnv End 0.1818 (~46 Hz), all other kick params unchanged. Strike Position 0.30 (recipe).

### Pad 2 — Crack Snare (Membrane / NoiseBurst)
- **Strike Position 0.35** (ADDED — recipe 0.35; off-center for the (0,1)/(1,1) crack pair, was silently defaulted).
- **Body Damping b3 0.10** (raised from 0.04 — real Mylar f² HF damping so the re-emerged body reads as a snare; still a bright crack, recipe is 0.16).
- b1 0.28 retained (documented big-room roomy-tail delta off recipe 0.60). All other snare params unchanged.

### Pad 4 — Rim Shot (Shell / Impulse)
- Unchanged. Strike Position left default 0.30 (recipe omits it; Size 0.30 + b1/b3 already place the crack band).

### Pads 5,7,9,11,12,14 — Tom row (Membrane / Mallet) — pitch-env re-encoded + Strike added
All six toms: **Strike Position 0.35** (ADDED — recipe 0.35; CRITICAL — a dead-center strike yields only the pitchless (0,1)/(0,2) thump; 0.35 recovers the (1,1)/(2,1) tone that makes the tom pitched).
Pitch-env Start/End **re-encoded to the engine map** for the stated low→high Hz intent:

| Pad | Tom | Start Hz / norm | End Hz / norm |
|----|----|----|----|
| 5 | Tom 1 (floor) | 180 / **0.4771** | 70 / **0.2720** |
| 7 | Tom 2 | 220 / **0.5207** | 85 / **0.3142** |
| 9 | Tom 3 | 270 / **0.5652** | 105 / **0.3601** |
| 11 | Tom 4 | 330 / **0.6087** | 130 / **0.4065** |
| 12 | Tom 5 | 400 / **0.6505** | 165 / **0.4582** |
| 14 | Tom 6 (high rack) | 480 / **0.6901** | 215 / **0.5157** |

(Original proposal stored 0.4508/0.2718, 0.4675/0.2934, 0.49/0.3389, 0.535/0.3807, 0.5807/0.4321, 0.6155/0.49 — each ~15–25 % flat of the intended Hz.) The graded **decaySkew 0.46→0.40**, **nonlinearCoupling 0.12**, **modeScatter 0.14→0.08**, **tensionMod 0.34→0.24**, **pan 0.66→0.34**, b1 0.26→0.40, Secondary Size 0.40+0.02·i, airLoading 0.78→0.70 are all retained as proposed.

### Pad 6 — Closed Hat (NoiseBody / NoiseBurst)
- **Strike Position 0.60** (ADDED — recipe 0.60; plate mode-shape weighting for the tight chick).

### Pad 10 — Open Hat (NoiseBody / NoiseBurst)
- **Strike Position 0.45** (ADDED — recipe 0.45).

### Pad 13 — Crash 1 (NoiseBody / NoiseBurst)
- **Strike Position 0.55** (ADDED — recipe 0.55; near-edge plate strike feeds the dense bright cluster). All metallic axes (modeStretch 0.60, modeScatter 0.60, modeInject 0.25, nonlinearCoupling 0.35) unchanged.

### Pad 15 — Ride (Bell / NoiseBurst)
- **FM Ratio 0.30 moved to defaultedParams** ("no-op under NoiseBurst; kept for archetype provenance"). Strike Position 0.18 retained (recipe). decaySkew 0.62, modeStretch 0.45, modeScatter 0.55, b1 0.16 unchanged.

### Pad 16 — Crash 2 / China (NoiseBody / NoiseBurst)
- **Strike Position 0.55** (ADDED — recipe-consistent near-edge plate strike). Morph swell (Enabled 1, Start 0.80, End 0.96, Dur 0.30), modeStretch 0.66, modeScatter 0.70, modeInject 0.28, nonlinearCoupling 0.35 unchanged.

### Pad 17 — Splash (NoiseBody / NoiseBurst)
- **Strike Position 0.35** (ADDED — recipe 0.35).

### Pad 18 — Cowbell (Bell / FMImpulse)
- Unchanged. **FM Ratio 0.45 → 2.35 modulator ratio is LIVE** (FMImpulse) — the one pad exercising the FM axis (recipe uses 0.50/2.50; 0.45 is in the cited 0.42–0.55 detuned-fifth band). Strike Position left default 0.30 (recipe 0.30 = default — documented, not silent).

### Pad 20 — Tambourine (NoiseBody / NoiseBurst)
- **Macro Complexity 0.65** (ADDED — recipe; nudges coupling/nonlinear/inject for an organic jingle cloud). Secondary-shell jingle bank (Enabled 1, Size 0.30, Material 0.70, Coupling 0.45) retained. Strike Position left default 0.30 (recipe 0.30 = default — documented).

### Pads 1 (Side-stick) & 3 (Clap)
- Unchanged. Strike Position left default 0.30 (both recipes = 0.30 — documented as an intentional default, no longer a silent omission).

## Why this exercises the full param surface
- **Membrane physics:** airLoading (kick 0.85 / toms 0.70–0.78 / snare 0.42, membrane-only — 0 on every plate/bell pad), tensionMod glide (kick 0.20, snare 0.32, toms 0.24–0.34, membrane-only), secondary head↔shell coupling (kick/snare/toms enabled with size+material deltas), **Strike Position** now an active timbre axis on the membranes (snare 0.35, toms 0.35).
- **Unnatural axes (now working):** modeStretch on every metallic pad (rim 0.45, crash 0.60, China 0.66, ride 0.45, splash 0.55, cowbell 0.50, stick 0.40); decaySkew tilt on toms (0.40–0.46) + ride (0.62); modeInject bloom on crash/China (0.25/0.28, `1/k`); modeScatter 0.08→0.70.
- **ToneShaper:** snare LP filter-env crack sweep; Drive-as-flavour kick 0.30 / snare 0.42 / toms 0.18; **Morph** swell on the China (pad 16).
- **Routing/space:** full pan spread; choke group 1 on the 3-hat triad only; 4 cymbals to aux bus 1.
- **FM:** fmRatio 0.45 **live** on the cowbell (FMImpulse) — the only FM-exciter pad.

## Key deltas from the current kit
1. **Tom row** — was a pure size/material/decay/pitch/b1 sweep with decaySkew 0.5, modeScatter 0.12, nonlinearCoupling 0, pan 0.5, **Strike default** on all six. Now adds **decaySkew 0.46→0.40**, **nonlinearCoupling 0.12**, **modeScatter 0.14→0.08**, a **pan spread 0.34→0.66**, **explicit Strike 0.35** (the pitch-defining off-center strike), and **pitch-env Start/End correctly encoded** to the intended 180→70 … 480→215 Hz row.
2. **Cymbals** — crash gets `modeStretch 0.60 + modeScatter 0.60 + modeInject 0.25 + nonlinearCoupling 0.35 + Strike 0.55`; **ride is a true Bell** with `decaySkew 0.62 + modeStretch 0.45 + b1 0.16 + Strike 0.18`; splash `modeStretch 0.55 + modeScatter 0.50 + Strike 0.35`.
3. **Kick** — `b3 0.10 → 0.42` for proper woody HF roll-off; pitch-env start label corrected to ~160 Hz.
4. **Snare** — `b3 0.04 → 0.10` (real Mylar damping), `Strike 0.35` added.
5. **+5 pads** — cross-stick (1), clap (3), China (16), cowbell (18), tambourine (20) fill GM-sensible slots and bring the kit to 20 voices.

## Gaps (verified)
- **Resolved vs current:** added 2nd crash/China, cowbell, tambourine, dry cross-stick (distinct from the loud rimshot), hand clap.
- **Remaining (intentional):** no dedicated ride-bell pad (the ride carries its bell ping via the Bell body + click); 12 pads (19, 21–31) stay disabled by design.
- **No duplicate roles:** the two rim-family voices (dry stick vs loud rimshot) and the two crashes (sustain vs trashy China) are distinct articulations/colours. The 6-pad tom row is one drum across its tuning range (GM convention) and now varies 4 independent axes + Strike + correctly-graded pitch, not a pure size sweep.

---

## Verification log (8 issues found & fixed)

1. PITCH-ENV NORM vs STATED-Hz MISMATCH (toms + kick start). The pitch-env Start/End normalized values decode (via the param-dictionary mapping hz=20*100^norm) to LOWER frequencies than the rationale/recipe state. Verified with the engine's own log map: Tom1 Start 0.4508 -> 159.5 Hz but rationale & current-state say 180 Hz (correct norm 0.4771); Tom2 Start 0.4675 -> 172 Hz vs stated 220 Hz (should be 0.5207), End 0.2934 -> 77 Hz vs stated 85 Hz (should be 0.3142); Tom3 Start 0.49 -> 191 Hz vs 270 Hz (0.5652), End 0.3389 -> 95 Hz vs 105 Hz (0.3601); Tom4 Start 0.535 -> 235 Hz vs 330 Hz (0.6087), End 0.3807 -> 115 Hz vs 130 Hz (0.4065); Tom5 Start 0.5807 -> 290 Hz vs 400 Hz (0.6505), End 0.4321 -> 146 Hz vs 165 Hz (0.4582); Tom6 Start 0.6155 -> 340 Hz vs 480 Hz (0.6901), End 0.49 -> 191 Hz vs 215 Hz (0.5157). The kick Start 0.4508 actually = 160 Hz (matches the maple-kick recipe's real 160 Hz) but the rationale mislabels it '~180 Hz'. FIX: re-encoded every tom Start/End to the engine map for the stated Hz (180/70, 220/85, 270/105, 330/130, 400/165, 480/215 -> 0.4771/0.2720, 0.5207/0.3142, 0.5652/0.3601, 0.6087/0.4065, 0.6505/0.4582, 0.6901/0.5157) and corrected the kick rationale to read '~160 Hz'. Note the current-state arrays the proposal cites are themselves internally consistent at the LOWER Hz; I aligned to the explicit per-pad Hz intent (low->high tom row), which is the physically meaningful target.

2. COVERAGE GAP: Strike Position silently defaulted on pads where the cited recipe sets a NON-default, physically meaningful value. Strike Position re-weights mode-shape energy (Bessel diametric-mode sampling on Membrane; plate/shell mode-shape on cymbals) and is the lever that makes a tom PITCHED vs a dead center thump. FIX added explicit Strike Position: Snare 0.35 (recipe 0.35 -- off-center crack/diametric pair), all 6 Toms 0.35 (recipe 0.35 -- CRITICAL: dead-center gives only pitchless (0,1)/(0,2); off-center recovers the (1,1)/(2,1) tone), Closed Hat 0.6 (recipe 0.6), Open Hat 0.45 (recipe 0.45), Crash 1 0.55 (recipe 0.55), China 0.55, Splash 0.35 (recipe 0.35). For Cowbell/Clap/Tambourine/Side-stick/Pedal-Hat the recipe value (~0.30) equals the engine default, so leaving them defaulted is acceptable and is now documented with a one-line reason instead of silently omitted.

3. SNARE Body Damping b3 0.04 is far under the recipe's 0.16 (and would nearly remove Mylar f^2 HF damping, leaving an over-bright, almost metallic head). The roomy-tail b1 0.28 (vs recipe 0.60) is a defensible big-room delta, but b3 0.04 is physically wrong for a coated head. FIX: b3 raised to 0.10 (a documented compromise -- still bright crack, but real Mylar HF roll-off so the body re-emerged by H-2 reads as a snare, not a plate).

4. TAMBOURINE missing Macro Complexity 0.65 from its recipe (nudges couplingAmount/nonlinearCoupling/modeInject for an organic jingle cloud). Minor, but it is a meaningful recipe axis. FIX: added Macro Complexity 0.65; the secondary-shell jingle bank (already present) plus complexity now carry the inharmonic shimmer.

5. RIDE FM Ratio 0.30 is listed as a meaningful param but is a STRICT NO-OP under the NoiseBurst exciter (FM Ratio is FMImpulse-only per the param dictionary). The proposal already flags it as provenance-only, which is honest, but listing a no-op in the active param set rather than defaultedParams is misleading. FIX: moved Ride FM Ratio to defaultedParams with the 'no-op under NoiseBurst, kept for archetype provenance' reason; behavior is unchanged.

6. LAYOUT/GM verification (no fix needed, confirmed correct): China on pad16=GM52, Tambourine pad20=GM54, Splash pad17=GM55, Cowbell pad18=GM56, Side-stick pad1=GM37, Clap pad3=GM39 all match the GM percussion map. Choke group 1 correctly spans only the 3-hat triad (closed/pedal/open) via Choke Group 0.125. Cymbals correctly routed to aux bus 1 (Output Bus 0.0667 -> idx 1). Tom pan spread 0.34(high rack)->0.66(floor) is a coherent drummer-perspective L->R image. The two rim-family voices (dry stick pad1 vs loud rimshot pad4) and two crashes (sustain pad13 vs trashy China pad16) are genuinely distinct articulations/colours, not duplicates -- gap analysis is sound.

7. RANGES: every valueNorm verified in [0,1]; discrete/sentinel params legal (Secondary Enabled 1, Morph Enabled 1, Choke Group 0.125->grp1, Output Bus 0.0667->bus1, Filter Type 0->LP). No out-of-range values found.

8. PHYSICAL-CORRECTNESS spot checks (no fix): body+exciter pairings all correct (Membrane+Mallet kick/toms, Membrane+NoiseBurst snare, Shell+Impulse rim/stick, NoiseBody+NoiseBurst hats/crash/china/splash/clap/tamb, Bell+NoiseBurst ride, Bell+FMImpulse cowbell). airLoading set only on Membrane pads and explicitly 0 on every plate/bell pad (membrane-only, correct). tensionMod only on Membrane pads (kick/snare/toms), 0/omitted elsewhere (correct). NonlinearCoupling used as amplitude-brightening on snare/toms/crash/china/ride (correct post-M3/M4 semantics). Drive treated as flavour with unity makeup on kick/snare/toms (correct post-M2). FM Ratio live ONLY on the cowbell's FMImpulse (correct). modeInject 1/k bloom on crash/china only (correct). decaySkew per-mode tilt on toms+ride (correct post-M5). Plate/free-plate Chladni via NoiseBody for the cymbals (correct).
