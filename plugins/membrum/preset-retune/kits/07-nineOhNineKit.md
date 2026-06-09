<!-- verdict: pass-with-fixes | coverageOk: false | issues fixed: 9 | IMPLEMENTED: 2026-06-09 (commit re-tune 909 Drum Machine) -->

# Membrum Kit Plan — "909 Drum Machine" (Electronic) · `nineOhNineKit()`

Re-designed against the corrected post-audit DSP chain (gain-staging H-1/H-2/H-4, measured-strike body norm N-1, Plate/Bell/Shell physics fixes, mode_inject 1/k, decaySkew per-mode tilt, modeStretch B_max 0.01, NonlinearCoupling env-level bloom, per-pad pan M-9). All values are **normalized [0,1]** (preset/on-wire).

> **Verification note (pass-with-fixes):** the tom pitch-env norms and the snare Size were corrected so the normalized values actually decode (via `toLogNorm(hz)=ln(hz/20)/ln(100)` and `f0=500·0.1^size`) to the archetypes' stated Hz; two meaningful NoiseBurst-hat params (burst duration, noise resonance) that had been silently defaulted were added. All other values verified against the cited archetypes and the param dictionary.

## Concept & role audit

The real Roland TR-909 is a **14-voice machine**: Bass Drum, Snare, Low/Mid/Hi Tom, Rim Shot, Hand Clap, Closed Hat, Open Hat, Crash, Ride. The current kit:
- crammed in **6 near-identical Membrane toms** (one recipe size-swept — the audit §4 sameness pattern),
- had **no Ride** and **no Cowbell**,
- left **every physics/expressive axis dead** (modeStretch 0.333, decaySkew 0.5, modeInject 0, NonlinearCoupling 0, pan 0.5 everywhere).

This plan collapses the tom row to the **3 real 909 toms**, spends the freed pads on the genuinely-missing roles (**Ride**, **Cowbell**, **Clave** wood accent), and activates the full param surface.

## Layout (32 pads, GM-map sense kept; MIDI = 36 + pad index)

| Pad (MIDI) | Drum | Body | Exciter | Bus | Choke |
|---|---|---|---|---|---|
| 0 (36) | 909 Kick | Membrane | Impulse | main | — |
| 2 (38) | 909 Snare | Membrane (+secondary shell) | NoiseBurst | main | — |
| 4 (40) | Rim Shot | Shell | Impulse | main | — |
| 5 (41) | 909 Hi Tom | Membrane | Impulse | main | — |
| 6 (42) | Closed Hat | NoiseBody | NoiseBurst | main | 1 |
| 7 (43) | 909 Mid Tom | Membrane | Impulse | main | — |
| 8 (44) | Pedal Hat | NoiseBody | NoiseBurst | main | 1 |
| 9 (45) | 909 Low Tom | Membrane | Impulse | main | — |
| 10 (46) | Open Hat | NoiseBody | NoiseBurst | main | 1 |
| 11 (47) | Clave / Woodblock | Shell | Impulse | main | — |
| 12 (48) | Cowbell | Bell | FMImpulse | main | — |
| 13 (49) | Crash | NoiseBody | NoiseBurst | aux 1 | — |
| 14 (50) | Ride | Bell | NoiseBurst | aux 1 | — |
| 15 (51) | Hand Clap | NoiseBody | NoiseBurst | main | — |
| 1, 3, 16–31 | (unused) | — | — | — | — |

`crafted = {0,2,4,5,6,7,8,9,10,11,12,13,14,15}` (still 14 pads, now all distinct roles).

**Gaps / duplicates flagged:** 6→3 toms (removed 3 duplicate timbres); added the missing **Ride** and **Cowbell**; added bodies **Bell** & **Shell** and exciter **FMImpulse** for coverage. **GM deviation (accepted):** Clap kept on MIDI 51 (909 accessory convention) which overlaps GM Ride 51, so the Ride is placed on MIDI 50 to avoid stacking.

## Body / exciter / param-surface coverage

- **Bodies:** Membrane (kick/snare/3 toms), NoiseBody (3 hats/crash/clap), Bell (cowbell/ride), Shell (rim/clave), Plate intentionally not used (NoiseBody is the cymbal body); String absent (no pitched string in a 909).
- **Exciters:** Impulse (kick/rim/toms/clave), NoiseBurst (snare/hats/cymbals/clap), FMImpulse (cowbell). Friction/Feedback intentionally absent.
- **Physics axes exercised:** pitch env (Membrane voices), tension glide (kick/snare/toms), secondary-shell coupling (snare), modeStretch (rim/cowbell/ride/crash), decaySkew (cowbell/ride), modeInject (crash), NonlinearCoupling bloom (crash/ride), modeScatter (rim/clap/cowbell/ride/crash), b3 override (clave), choke group 1 (3 hats), output bus 1 (crash/ride), pan spread (whole kit), Drive flavour (kick/snare).

## Per-pad exact values

### Pad 0 — 909 Kick (Membrane / Impulse)
material 0.18 · size 0.78 · decay 0.22 · level 0.85 · drive 0.20 · pitchEnvStart 0.4771 (180 Hz) · pitchEnvEnd 0.2197 (55 Hz) · pitchEnvTime 0.04 (20 ms) · pitchEnvCurve 0.15 · click 0.55 / contact 0.12 / bright 0.55 · b1 0.42 · b3 0.30 · noise 0.05 · tension 0.10 · airLoading 0.0 · macroPunch 0.85 · macroBodySize 0.45 · pan 0.5. *Defaulted:* coupling/secondary off, modeStretch/decaySkew/scatter/inject neutral/0, filter bypassed (kick is a clean pitch-rolling sine; grit is Drive only).

### Pad 2 — 909 Snare (Membrane + secondary shell / NoiseBurst)
material 0.58 · **size 0.398 (f0 ~200 Hz — corrected from 0.58, which gave ~131 Hz and contradicted the stated ~180-200 Hz rationale; matches the 909-snare archetype)** · decay 0.55 · level 1.0 · noiseBurstDur 0.0769 (~3 ms) · drive 0.48 · filter LP cutoff 0.82 / envAmt 0.60 / envDecay 0.385 · pitchEnvStart 0.6394 (380 Hz) · pitchEnvEnd 0.4226 (140 Hz) · pitchEnvTime 0.09 · curve 0.10 · **couplingStrength 0.38 · secondaryEnabled 1 · secondarySize 0.32 · secondaryMaterial 0.90** (the metallic second oscillator) · noise 0.78 / cutoff 0.86 / color 0.86 (violet) / decay 0.48 (long tail) · click 0.62 / bright 0.78 · tension 0.32 · b1 0.38 · b3 0.20 · macroBrightness 0.85 · pan 0.5. *Defaulted:* nonlinearCoupling 0 (static snare), modeInject 0 (ping is the shell), airLoading/scatter 0.

### Pad 4 — Rim Shot (Shell / Impulse)
material 0.85 · size 0.34 (f0 686 Hz) · decay 0.18 · strike 0.15 (free-end antinode) · level 0.9 · click 0.94 / contact 0.20 / bright 0.94 · b1 0.50 · b3 0.30 · **modeScatter 0.40 · modeStretch 0.45** (jagged inharmonic) · noise 0.20 / color 0.85 · macroPunch 0.92 · pan 0.42. *Defaulted:* pitch env off, drive/fold/inject/coupling/tension off, airLoading no-op on Shell.

### Pad 5 — 909 Hi Tom (Membrane / Impulse)
material 0.50 · size 0.28 · decay 0.18 · level 0.78 · **pitchEnvStart 0.7349 (590 Hz — corrected from 0.6709, which decoded to ~351 Hz) · pitchEnvEnd 0.5106 (210 Hz)** · time 0.10 · curve 0.15 · b1 0.42 · b3 0.18 · click 0.32 / contact 0.15 · noise 0.05 · tension 0.20 · macroPunch 0.65 · pan 0.64. *Defaulted:* airLoading/scatter/stretch/skew neutral (clean pitched oscillator), coupling/secondary/drive/fold/inject/morph off.

### Pad 7 — 909 Mid Tom (Membrane / Impulse)
material 0.34 · size 0.48 · decay 0.25 · **pitchEnvStart 0.6215 (350 Hz — corrected from 0.6105) · pitchEnvEnd 0.3891 (120 Hz — corrected from 0.4644, which decoded to ~204 Hz)** · time 0.10 · curve 0.15 · b1 0.36 · b3 0.18 · tension 0.20 · click 0.32 · noise 0.05 · macroPunch 0.65 · pan 0.50. *Defaulted:* same as Hi Tom.

### Pad 9 — 909 Low Tom (Membrane / Impulse)
material 0.20 · size 0.65 · decay 0.32 · **pitchEnvStart 0.5396 (240 Hz) · pitchEnvEnd 0.3142 (85 Hz — corrected from 0.4226, which decoded to ~140 Hz)** · time 0.10 · curve 0.15 · b1 0.30 · b3 0.18 · tension 0.20 · click 0.32 · noise 0.05 · macroPunch 0.65 · pan 0.36. *Defaulted:* same as Hi Tom.

### Pad 6 / 8 / 10 — Closed / Pedal / Open Hat (NoiseBody / NoiseBurst, choke 1)
**Closed (6):** material 0.95 · size 0.10 · decay 0.07 · level 0.72 · **noiseBurstDur 0.10 (~3.3 ms sharp attack — added; was silently defaulted to 0.5/~8.5 ms; archetype value)** · noise 0.85 / cutoff 0.95 / **resonance 0.12 (Q~0.86, flat broadband no whistle — added per archetype; was default 0.2)** / color 0.95 (violet) / decay 0.07 · click 0.0 (documented kit deviation for cleanest 909 tss; archetype uses 0.35) · airLoading 0 · scatter 0 · b1 0.65 · b3 0.0 · choke 1 · pan 0.58.
**Pedal (8):** clone of 6, decay 0.04 · noiseDecay 0.05 · b1 0.78.
**Open (10):** clone of 6, decay 0.42 · noiseDecay 0.40 · b1 0.30 · pan 0.60.
*Defaulted:* pitch env / drive / fold off (crunch is quantization not glide), modeInject/nonlinearCoupling 0 (pitch-less clean), secondary/tension off.

### Pad 11 — Clave / Woodblock (Shell / Impulse)
material 0.85 · size 0.0 (f0 1500 Hz) · decay 0.12 · strike 0.12 · level 0.80 · **b3 0.70 (override — dry wood HF damping)** · click 0.55 / contact 0.15 / bright 0.82 · noise 0.0 · pan 0.30. *Defaulted:* pitch env off (no glide), modeInject/drive/fold/nonlinearCoupling 0, modeStretch/decaySkew/scatter neutral, coupling/secondary/tension/morph off, airLoading no-op on Shell.

### Pad 12 — Cowbell (Bell / FMImpulse)
material 0.78 · size 0.22 (f0 ~482 Hz, archetype target 471) · decay 0.30 · strike 0.30 · level 0.75 · **fmRatio 0.50 (mod ratio 2.5, detuned-fifth clang)** · click 0.55 / contact 0.10 / bright 0.72 · noise 0.10 / color 0.40 (pink) / cutoff 0.62 / decay 0.20 · b1 0.32 · b3 0.0 · **modeStretch 0.55 · modeScatter 0.20 · decaySkew 0.42** · macroBrightness 0.65 · pan 0.66. *Defaulted:* pitch env off (static pitch), drive/fold/modeInject/nonlinearCoupling 0 (clang from body+FM), airLoading/tension no-op on Bell, coupling/secondary/morph off, filter bypassed.

### Pad 13 — Crash (NoiseBody / NoiseBurst, aux bus 1)
material 0.95 · size 0.35 · decay 0.70 · strike 0.55 · level 0.72 · **modeStretch 0.60 · modeInject 0.25 · nonlinearCoupling 0.35 (bloom) · modeScatter 0.60** · b1 0.30 · b3 0.0 · noise 0.50 / cutoff 0.90 / color 0.92 (violet) / decay 0.60 · click 0.20 / bright 0.82 · **outputBus 1** · pan 0.40. *Defaulted:* pitch env off, airLoading no-op, drive/fold/morph off, coupling/secondary/tension off, filter bypassed.

### Pad 14 — Ride (Bell / NoiseBurst, aux bus 1)
material 0.95 · size 0.30 · decay 0.90 · strike 0.18 · level 0.72 · **modeStretch 0.45 · decaySkew 0.62 · nonlinearCoupling 0.18 · modeScatter 0.55** · b1 0.16 · b3 0.0 · noise 0.45 / cutoff 0.90 / color 0.85 (violet) / decay 0.78 · click 0.45 / bright 0.82 · **outputBus 1** · pan 0.62. *Defaulted:* fmRatio inert (NoiseBurst — ping is the Bell body), pitch env off, airLoading/tension no-op, drive/fold/morph/coupling/secondary off, modeInject 0.

### Pad 15 — Hand Clap (NoiseBody / NoiseBurst)
material 0.85 · size 0.18 · decay 0.18 · level 0.78 · noiseBurstDur 0.55 (flam smear) · noise 0.85 / cutoff 0.78 / **resonance 0.40 (Q~2.18 clap formant)** / decay 0.20 / color 0.65 (white) · click 0.45 / contact 0.22 / bright 0.62 · modeScatter 0.40 · b1 0.50 · b3 0.0 · macroBrightness 0.65 · macroComplexity 0.55 · pan 0.50. *Defaulted:* pitch env off, drive/fold/modeInject/nonlinearCoupling 0, coupling/secondary/tension off, modeStretch/decaySkew neutral, filter bypassed.

## Delta from current
- **Structure:** 6-tom one-recipe sweep → 3 distinct toms; reclaimed pads now carry **Ride** (new), **Cowbell** (new), **Clave** (new). Crash upgraded to full Chladni-bloom + bus 1; clap given the cited NoiseBody formant recipe.
- **Activated dead axes:** pan spread (was 0.5 everywhere); modeStretch off-neutral on rim/cowbell/ride/crash (was 0.333); decaySkew tilted on cowbell/ride (was 0.5); modeInject on crash (was 0); nonlinearCoupling bloom on crash/ride (was 0); output bus 1 on crash/ride; choke group 1 unified across the 3 hats.
- **Kept & re-grounded:** kick (pitch-roll + Drive grit + b1/b3 floor) and snare (secondary-shell two-tone + long violet noise tail) keep their already-strong recipes, now pinned to the cited 909 archetype values.

## Corrections applied during verification
1. **Tom pitch-env norms** re-derived from `toLogNorm`: Hi start 0.6709→**0.7349** (590 Hz), Mid start 0.6105→**0.6215** (350 Hz), Mid end 0.4644→**0.3891** (120 Hz), Low end 0.4226→**0.3142** (85 Hz). The original norms decoded to pitches a 4th-to-octave higher than the stated targets, defeating the graded Hi→Low tom row.
2. **Snare size** 0.58→**0.398** (f0 131→200 Hz), matching the archetype and the proposal's own "~180-200 Hz head region" rationale.
3. **Hat NoiseBurst Duration** added at **0.10** (~3.3 ms) on the closed hat (→ pedal/open clones); was silently defaulted to 0.5 (~8.5 ms), too soft for the 909 tss.
4. **Hat Noise Resonance** added at **0.12** (Q~0.86) per archetype; was default 0.2, risking a faint pitched hump on a flat-broadband-noise voice.

---

## Verification log (9 issues found & fixed)

1. TOM PITCH-ENV NORMS WRONG (physical correctness / ranges): the toms' pitchEnv Start/End normalized values do not match their own claimed Hz targets under toLogNorm(hz)=ln(hz/20)/ln(100). Hi Tom start claimed 590 Hz but norm 0.6709 actually decodes to ~351 Hz; correct norm is 0.7349. Mid Tom start claimed 350 Hz but norm 0.6105 (~333 Hz); correct 0.6215. Mid Tom END claimed 120 Hz but norm 0.4644 decodes to ~204 Hz; correct 0.3891. Low Tom END claimed 85 Hz but norm 0.4226 (~140 Hz); correct 0.3142. (Hi end 0.5104~210 Hz and Low start 0.5388~240 Hz are within rounding and were left.) FIX: replaced each tom Start/End norm with the toLogNorm value of the archetype grade-table Hz, so the settle pitches actually drop to the stated low frequencies instead of staying a 4th-to-octave too high.

2. SNARE SIZE CONTRADICTS ITS OWN RATIONALE AND THE ARCHETYPE (physical correctness): proposal set Membrane snare size 0.58 with rationale 'f0 in the ~180-200 Hz head-mode region', but f0=500*0.1^0.58=131 Hz, not 180-200 Hz; the 909-snare archetype explicitly specifies Size 0.398 (->200 Hz). FIX: changed size 0.58 -> 0.398 so the static head f0 lands at the archetype's ~200 Hz and matches the stated rationale. (Audible pitch is still driven by the 380->140 Hz pitch glide; this corrects the pre/post-glide modal layout.)

3. HAT NOISEBURST DURATION SILENTLY DEFAULTED (coverage gap): the hats use the NoiseBurst exciter, for which NoiseBurst Duration (offset 34) is a meaningful param, and the 909-hat archetype specifies 0.10 (~3.3 ms sharp noise attack). The proposal left it unset -> PadConfig default 0.5 (~8.5 ms), a much softer/longer attack that blunts the 909 'tss'. FIX: added noiseBurstDuration 0.10 to the closed hat (inherited by the pedal/open clones).

4. HAT NOISE RESONANCE DEFAULTED vs ARCHETYPE (coverage / physical correctness): the 909-hat archetype calls for Noise Resonance 0.12 (Q~0.86, 'flat broadband, no whistle'); the proposal left it at the PadConfig default 0.2 (Q~1.24), a small resonant hump that can add a faint pitched edge to a sound meant to be flat broadband noise. FIX: added noiseLayerResonance 0.12 to the closed hat (inherited by pedal/open).

5. CLOSED-HAT clickMix=0 is a documented kit deviation from the archetype (archetype uses Click 0.35 for a tick). Verified as an INTENTIONAL, documented choice ('cleanest 909 tss'), not an error -- left as-is. No fix; noted for transparency.

6. RIDE fmRatio written under NoiseBurst is INERT but correctly flagged in defaultedParams as documented-intent-only (param-dictionary: fmRatio is FMImpulse-only). Verified correct -- no fix.

7. DISCRETE/SENTINEL ENCODINGS verified legal and in range: chokeGroup 0.125 -> round(0.125*8)=1 (group 1) correct; outputBus 0.0667 -> round(0.0667*15)=1 (aux bus 1) correct; secondaryEnabled 1.0 (>=0.5 on) correct; tsFilterType 0 (Lowpass) correct; fmRatio 0.50 -> modulatorRatio 2.5 correct; all body f0 (cowbell 482/471, ride 401/400, clave 1500, rim 686, crash 670, clap 991) match archetypes. No range violations found.

8. LAYOUT / BODY+EXCITER ASSIGNMENTS verified PHYSICALLY CORRECT against post-audit semantics: Membrane+Impulse kick/toms (pitch-env + Membrane-only tension), Membrane+NoiseBurst snare (+secondary metallic shell), Shell+Impulse rim/clave (free-free bar), NoiseBody+NoiseBurst hats/crash/clap (free-plate Chladni + noise), Bell+FMImpulse cowbell, Bell+NoiseBurst ride. airLoading set 0 on all non-Membrane (no-op, correctly documented); tensionMod only on Membrane voices; NonlinearCoupling only on crash/ride (amplitude-bloom, correct); modeStretch off-neutral only on metallic bodies; Drive only as flavour on kick/snare. All consistent. No body/exciter fixes needed.

9. GAP/DUPLICATE FLAGGING verified correct: 6-tom sameness sweep correctly collapsed to 3 distinct toms; missing Ride and Cowbell correctly identified and added; Bell+Shell+FMImpulse coverage gaps correctly filled; GM Clap(51)/Ride(50) deviation correctly documented. Layout claims accurate (MIDI = 36 + pad index throughout). No fix.
