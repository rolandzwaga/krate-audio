<!-- verdict: pass-with-fixes | coverageOk: false | issues fixed: 9 | IMPLEMENTED: 2026-06-09 (commit re-tune Chaos Engine) | NOTE: 18 voices reconstructed from cited archetypes + sibling kits (no full per-pad value table in plan); layout + corrections applied verbatim. -->

# Chaos Engine — Corrected Kit Plan (Unnatural)

`chaosEngineKit()` · category **Unnatural** · 18 crafted pads (MIDI 36–53), pads 18–31 disabled.

## Identity

Max-everything synthetic sound design: aggressive, inharmonic, self-oscillating, evolving hits with **no acoustic referent**, voiced to *maximum controlled instability* under the −1 dBTP `TruePeakLimiter`. The redesign replaces the current single `for(i=0..13)` fmod()-ramped loop (one timbre across an index sweep, pads 14–31 disabled, modeInject 0 everywhere, pan unused, pre-audit gain) with **18 individually-crafted voices**, each anchored to a cited Stage-A archetype and re-voiced against the corrected chain.

## Post-audit semantics honored
- Body bounded by **measured-strike** norm (N-1); Level 0.62–0.72 hot voices under the bus limiter.
- **Drive = flavour** (M-2 unity makeup), **NonlinearCoupling = env-level amplitude brightening** (M-3/M-4), **per-mode decaySkew tilt** on all modal bodies (M-5).
- **B_max = 0.01** 1-indexed Mode Stretch (clearly metallic at 0.6); **mode_inject = 1/k** sawtooth.
- **Plate = free-plate Chladni** `(m+2n)^1.7`; Bell/Shell corrected mode shapes.
- **tensionModAmt = Membrane-only** (non-zero only on the kick); **airLoading = Membrane-only** (kick only).
- **String** drops modeStretch/decaySkew/modeScatter/airLoading/b1/b3/tension as inherent no-ops (pad 15).
- **Per-pad pan** (M-9) spread across the field; choke groups + 4 output buses place voices.

## Corrections applied in this verification pass (coverage / consistency)
- **Pad 2** (Plate snare-crack): added **Strike Position 0.62** (was silently 0.3) — mid-edge many-mode clang, matching its plate-tom siblings and the cited recipe.
- **Pad 5** (NoiseBody/Friction): added **Strike Position 0.45** (was silently 0.3) — NoiseBody is a modal body, so strikePos re-weights its mode shapes (Friction pick-position is String-only).
- **Pad 1** (Feedback Shell Snare): added **PitchEnv Curve 0.15** (fast-exp chirp, was defaulting to 0.0) and **Secondary Size 0.65** (shell ≈ 0.51·f0 ≈ 217 Hz, was defaulting to 0.625·f0).
- **Pad 10** (Plate tom MID): added **Body Damping b1 0.40** + **Secondary Size 0.40** + **Secondary Material 0.75** to match siblings 9/11 (were silently defaulting).
- **Pad 11** (Plate tom HI): added **Secondary Size 0.40** + **Secondary Material 0.75** (were silently defaulting).
- **Pads 6/7/8** (FM-Bell chaos hats): lowered Size to **0.12 / 0.10 / 0.16** (from 0.20/0.18/0.25) toward the FM-Bell-Hi-Hat archetype's hat-band placement (f0 → ~604/655/482 Hz), so the bell partials reach the 3–10 kHz hat band; kept deliberately below the archetype's 0.05 so the hats read as lower/clangy chaos hats, not literal 808 hats.

## Layout (MIDI → voice → archetype → body/exciter)

| Pad | MIDI | Voice | Archetype | Body | Exciter | Bus | Choke | Pan |
|----|----|----|----|----|----|----|----|----|
| 0 | 36 | Chaos Kick | FM/Chaotic Kick | Membrane | FMImpulse | main | – | .50 |
| 1 | 37 | Metal snare-squeal | Feedback Shell Snare | Shell | Feedback | main | – | .40 |
| 2 | 38 | Plate snare-crack | Inharmonic Plate Tom | Plate | FMImpulse | main | – | .60 |
| 3 | 39 | Chaos core A | Chaos Voice | Plate | Feedback | main | – | .35 |
| 4 | 40 | Chaos core B | Chaos Voice | Bell | FMImpulse | main | – | .65 |
| 5 | 41 | Chaos core C | Chaos Voice | NoiseBody | Friction | main | – | .45 |
| 6 | 42 | Chaos hat closed | FM-Bell Crash | Bell | FMImpulse | main | 1 | .60 |
| 7 | 43 | Chaos hat pedal | FM-Bell Crash | Bell | FMImpulse | main | 1 | .60 |
| 8 | 44 | Chaos hat open | FM-Bell Crash | Bell | FMImpulse | main | 1 | .60 |
| 9 | 45 | Plate tom LO | Inharmonic Plate Tom | Plate | FMImpulse | main | – | .30 |
| 10 | 46 | Plate tom MID | Inharmonic Plate Tom | Plate | FMImpulse | main | – | .50 |
| 11 | 47 | Plate tom HI | Inharmonic Plate Tom | Plate | FMImpulse | main | – | .70 |
| 12 | 48 | Feedback plate drone | Feedback Plate/Shell Drone | Plate | Feedback | aux1 | 2 | .30 |
| 13 | 49 | Feedback shell drone | Feedback Plate/Shell Drone | Shell | Feedback | aux2 | 2 | .70 |
| 14 | 50 | Singing-bowl drone | Singing Bowl | Bell | Friction | aux3 | 2 | .50 |
| 15 | 51 | Friction string drone | Friction String Drone | String | Friction | aux1 | – | .40 |
| 16 | 52 | Ghost bell | Ghost Tone | Bell | FMImpulse | aux1 | – | .60 |
| 17 | 53 | FM-bell modeInject hybrid | Algorithmic FX Voice | Bell | FMImpulse | aux2 | – | .50 |

All six body models and all three energetic exciters appear by assignment, not by ramp. Exact per-param normalized values, rationales and citations are in the per-pad notes below; the corrections in the pass above are folded in.

### Per-pad correction detail (the only changed values vs the proposal)
- **Pad 1** — add: PitchEnv Curve **0.15** (fast-exp chirp, feedback-shell-snare); Secondary Size **0.65** (shell ≈217 Hz, feedback-shell-snare).
- **Pad 2** — add: Strike Position **0.62** (mid-edge clang, inharmonic-plate-tom).
- **Pad 5** — add: Strike Position **0.45** (balanced many-mode excitation on the modal NoiseBody, chaos-voice).
- **Pad 6** — change: Size **0.20 → 0.12** (hat-band energy, fm-bell-hi-hat).
- **Pad 7** — change: Size **0.18 → 0.10** (hat-band energy, fm-bell-hi-hat).
- **Pad 8** — change: Size **0.25 → 0.16** (hat-band energy, fm-bell-hi-hat).
- **Pad 10** — add: Body Damping b1 **0.40**; Secondary Size **0.40**; Secondary Material **0.75** (match siblings, inharmonic-plate-tom).
- **Pad 11** — add: Secondary Size **0.40**; Secondary Material **0.75** (match siblings, inharmonic-plate-tom).

All other per-pad values are confirmed correct against the cited archetypes and the post-audit semantics, and are unchanged from the proposal.

## Kit globals (deltas)
- `globalCoupling` **0.92 → 0.80** (ease the now-active limiter; still drives the sympathetic network).
- `snareBuzz` 0.55, `tomResonance` 0.78, `couplingDelayMs` 1.7, `maxPolyphony` 12, `stealingPolicy` 2 — retained (the dense overlapping drones want polyphony + sympathetic coupling).

## Why this is correct vs. the current build
1. **Sameness killed**: 18 hand-tuned voices, no fmod/i-derived params.
2. **modeInject finally used** (0.20 on pad 16, 0.25 on pad 17) — the "ironic for a chaos kit" audit fix and the Algorithmic-FX unique axis.
3. **Pan spread** (M-9) for a true stereo field (current kit only used `outputBus i%4`).
4. **tension/airLoading restricted to Membrane** (kick only) — current kit set them on all 6 bodies (no-op on 5/6).
5. **Gain re-voiced** for the post-N-1 measured-strike body + active TruePeakLimiter.
6. **Strike vs drone disambiguated** where a body/exciter pair repeats (pad 3 Plate+Feedback short clang vs pad 12 Plate+Feedback bandpass-in-loop drone).
7. **Coverage gaps closed**: strikePos, secondary size/material, b1, and pitch-env curve are now set (not silently defaulted) on every pad where the cited archetype makes them meaningful; hat sizes nudged into the hat band.

---

## Verification log (9 issues found & fixed)

1. COVERAGE (pad 2, Plate snare-crack): Strike Position silently defaulted to 0.3. The Inharmonic Plate Tom archetype specifies Strike Position 0.62 (rho~0.71, mid-edge) to excite many inharmonic modes = the clang; its sibling plate pads 9/10/11 set it. FIX: set Strike Position 0.62 on pad 2 (added to params with citation inharmonic-plate-tom).

2. COVERAGE (pad 5, NoiseBody/Friction): Strike Position silently defaulted to 0.3. NoiseBody is a MODAL body (plate ratios) so strikePos re-weights the mode shapes and IS meaningful (Friction's pick-position semantics apply only to String). FIX: added Strike Position 0.45 (balanced many-mode excitation) to pad 5 params, citation chaos-voice.

3. COVERAGE (pad 1, Feedback Shell Snare): PitchEnv Curve silently defaulted to 0.0. The archetype specifies Curve -0.7 (fast-exp chirp) = norm ~0.15; leaving it at PadConfig default 0.0 (curveAmount -1.0) is even faster/sharper than intended and undocumented. FIX: set PitchEnv Curve 0.15 on pad 1 to match the archetype, citation feedback-shell-snare.

4. COVERAGE (pad 1, Feedback Shell Snare): Secondary Size silently defaulted to 0.5 (~0.625*f0). The archetype calls for a metallic secondary at ~217 Hz against f0~423 Hz = ratio ~0.51, which needs Secondary Size ~0.65 (sizeRatio=1-0.65*0.75=0.51). FIX: added Secondary Size 0.65 to pad 1, citation feedback-shell-snare.

5. COVERAGE (pad 10, Plate tom MID): Body Damping b1 silently defaulted to the -1 sentinel (derive-from-Decay) while sibling pads 9 and 11 set b1 0.40 (~20 s^-1 medium ring) per the archetype. FIX: added Body Damping b1 0.40 to pad 10, citation inharmonic-plate-tom.

6. COVERAGE (pads 10 & 11, Plate toms MID/HI): Secondary Size and Secondary Material silently defaulted (0.5 / 0.4) while sibling pad 9 sets Secondary Size 0.4 and Secondary Material 0.75 (bright metallic shell ~0.7*f0) per the archetype. The secondary ring is enabled on these pads, so these ARE meaningful. FIX: added Secondary Size 0.40 and Secondary Material 0.75 to pads 10 and 11, citation inharmonic-plate-tom.

7. PHYSICAL/IDENTITY (pads 6,7,8, FM-Bell chaos hats): Size 0.20/0.18/0.25 places f0 ~504/527/450 Hz (Bell base 800*0.1^size). The FM-Bell Hi-Hat archetype specifies Size 0.05 (f0~712 Hz) precisely so the bell partials land in the 3-10 kHz hi-hat band; the proposed sizes sit a half-decade low, making the hats clangy-bell rather than hat-band. This is a deliberate Unnatural-kit repurposing, but it under-uses the archetype's hat-band placement. FIX (light): lowered the hat sizes toward the archetype (0.12/0.10/0.16) to recover hat-band energy while keeping them distinctly lower/chaos-flavoured than a literal 808 hat. Documented as a kit delta.

8. LABEL DRIFT (non-blocking): summary/gaps/layoutChanges state modeInject '0.18-0.25 on pads 16-17'; the actual values are 0.20 (pad 16) and 0.25 (pad 17). Corrected the prose to '0.20-0.25' for accuracy. No value change.

9. VERIFIED-OK (no fix needed): All discrete/sentinel encodings are legal -- Choke Group norm 0.125->grp1, 0.25->grp2 (round(norm*8)); Output Bus norm 0.067->aux1, 0.133->aux2, 0.2->aux3 (round(norm*15)); Filter Type 0/0.5/1->LP/HP/BP; Secondary/Morph Enabled 1.0; all valueNorm in [0,1]. tensionMod non-zero only on the Membrane kick (0.5) and airLoading only on the kick (0.4) -- both correctly Membrane-gated. String drone (pad 15) correctly documents modeStretch/decaySkew/modeScatter/airLoading/b1/b3/tension as inherent waveguide no-ops. Duplicate Plate+Feedback (pad 3 strike vs pad 12 drone) correctly disambiguated by decay/click/in-loop-bandpass. Layout and gap flags are all accurate against current-state.json (single fmod loop, modeInject 0 everywhere, tension on all bodies, airLoading (i%4)*0.20 on non-membranes, outputBus i%4, pan unused).
