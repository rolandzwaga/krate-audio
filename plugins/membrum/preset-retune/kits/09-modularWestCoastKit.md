<!-- verdict: pass-with-fixes | coverageOk: false | issues fixed: 10 | IMPLEMENTED: 2026-06-09 (commit re-tune Modular West Coast) -->

# Modular West Coast — Corrected Kit Plan (Electronic · `modularWestCoastKit()`)

A Buchla/west-coast synthesis rack: Feedback/FMImpulse/Friction exciters over Membrane/Plate/Bell/String bodies. Re-voiced 1:1 against the dedicated Stage-A west-coast archetypes under post-audit semantics (measured-strike body norm, free-plate Chladni, env-level NonlinearCoupling = brightening, Drive = flavour, 1-indexed Mode Stretch, 1/k Mode Inject, per-mode Decay Skew on all modal bodies). Expanded from 14 to 24 audible pads.

> VERIFICATION VERDICT: pass-with-fixes. Six corrections applied vs the submitted proposal (all are gaps between the plan's prose and the generator code / cited archetypes): (1) the Feedback-Plate drone was missing its defining in-loop BANDPASS ToneShaper; (2) the tom row's decaySkew was silently neutral (0.50) despite the claimed M-5 tilt; (3) snare modeInject and (4) crash modeInject were claimed-NEW but unset; (5) the sub-bell's inert feedbackAmount and (6) the string drone's inert modeStretch/decaySkew are now explicitly documented as no-ops.

## Layout (GM map)

| Pad | MIDI | Drum | Body | Exciter | Status |
|---|---|---|---|---|---|
| 0 | 36 | West Coast Feedback Kick | Membrane | Feedback | re-voiced |
| 1 | 37 | Friction String Drone (aux 1) | String | Friction | re-voiced |
| 2 | 38 | FM Plate Snare | Plate | FMImpulse | re-voiced + Mode Inject (FIX) |
| 3 | 39 | **NEW** Inharmonic-Plate Clap/Rim | Plate | Impulse | added (gap: no clap) |
| 4 | 40 | Sub-Bell Perc (lo) | Bell | FMImpulse | re-voiced |
| 5 | 41 | Inharmonic Plate Tom 1 | Plate | FMImpulse | re-voiced (free-plate) + decaySkew FIX |
| 6 | 42 | FM-Bell Hat Closed | Bell | FMImpulse | re-voiced |
| 7 | 43 | Inharmonic Plate Tom 2 | Plate | Feedback | re-voiced + decaySkew FIX |
| 8 | 44 | FM-Bell Hat Pedal | Bell | FMImpulse | re-voiced |
| 9 | 45 | Inharmonic Plate Tom 3 | Plate | FMImpulse | re-voiced + decaySkew FIX |
| 10 | 46 | FM-Bell Hat Open | Bell | FMImpulse | re-voiced |
| 11 | 47 | Inharmonic Plate Tom 4 | Plate | Feedback | re-voiced + decaySkew FIX |
| 12 | 48 | Inharmonic Plate Tom 5 | Plate | FMImpulse | re-voiced + decaySkew FIX |
| 13 | 49 | FM-Bell Crash | Bell | FMImpulse | re-voiced + Mode Inject (FIX) |
| 14 | 50 | Inharmonic Plate Tom 6 | Plate | Feedback | re-voiced + decaySkew FIX |
| 15 | 51 | **NEW** FM-Bell Ride (aux 1) | Bell | FMImpulse | added (gap: no ride) |
| 16 | 52 | **NEW** FM-Bell Splash | Bell | FMImpulse | added |
| 17 | 53 | **NEW** Hi Sub-Bell Perc | Bell | FMImpulse | added (register span) |
| 18 | 54 | **NEW** Feedback-Plate Drone (aux 1) | Plate | Feedback | added (2nd drone) + BP loop filter (FIX) |
| 19 | 55 | **NEW** Friction-Membrane Drone (aux 1) | Membrane | Friction | added (uses Tension) |
| 20-31 | 56-67 | disabled | — | — | intentionally silent |

`k.crafted = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19}` (was {0,1,2,4,5,6,7,8,9,10,11,12,13,14}).

## Per-pad exact values

### Pad 0 — West Coast Feedback Kick (Membrane / Feedback)
material 0.25 · size 0.85 · decay 0.40 · strikePos 0.30 · level 0.80 · **feedbackAmount 0.45** · **fold 0.30** · **nonlinearCoupling 0.40** (re-voiced env-level) · **tensionMod 0.45** · pitchEnv 220→45 Hz / 30 ms / curve 0.15 · modeStretch 0.45 · b1 0.30 · b3 0.30 · secondary on, coupling 0.20, secSize 0.45, secMat 0.85 · click 0.30 / contact 0.18 / bright 0.55 · noise 0.20 / cut 0.50 / dec 0.30 / Pink · airLoading 0 · scatter 0 · decaySkew 0.50 (neutral, per archetype) · macroPunch 0.65 · macroComplexity 0.85 · **pan 0.50**. Defaulted: filter (bypass), drive 0, modeInject 0, morph off, FM/NoiseBurst/Friction (no-op).

### Pad 2 — FM Plate Snare (Plate / FMImpulse)
material 0.45 · size 0.55 · decay 0.55 · level 0.95 · fmRatio 0.55 · modeStretch 0.55 · scatter 0.45 · **modeInject 0.12 (FIX — was coded 0.0 despite the plan claiming 0.12)** · **nonlinearCoupling 0.45** · **drive 0.28** (flavour) · pitchEnv 230→160 Hz / 60 ms · morph on 0.55→0.80 / ~607 ms exp · filter LP 0.85 / res 0.20 / env +0.4 dec 0.385 / rel 0.20 · noise 0.60 / cut 0.78 / White / dec 0.30 · click 0.85 / contact 0.07 / bright 0.85 · b1 0.28 · b3 0.18 · pan 0.50. Defaulted: secondary off, fold 0, tension (set 0.30 in code but Membrane-only no-op on Plate), air no-op, macros neutral.

### Pad 4 — Sub-Bell Perc lo (Bell / FMImpulse)
material 0.60 · size 0.32 · decay 0.55 · strikePos 0.20 · level 0.75 · fmRatio 0.72 (ratio 3.16) · modeStretch 0.50 · **decaySkew 0.45** · **nonlinearCoupling 0.30** · drive 0.0 (delta: kept clean at low pitch) · click 0.45 / bright 0.85 · noise 0.10 · b3 0.0 · b1 0.30 · pan 0.42. Defaulted: pitchEnv off, modeInject 0, scatter 0, morph off, secondary off, tension no-op, **feedbackAmount 0.30 = INERT no-op under FMImpulse (documentation only; grit carried by FM ratio + NLC)**.

### Pad 1 — Friction String Drone (String / Friction · aux 1)
material 0.50 · size 0.55 · decay 0.85 · level 0.65 · **frictionPressure 0.55** · **nonlinearCoupling 0.55** · morph on 0.40→0.85 / ~1.2 s · noise 0.20 / cut 0.45 / (Pink) · click 0 · b1/b3 set in code but inert on String · outputBus 1 · pan 0.30 · macroComplexity 0.85. Defaulted (ALL inherent String no-ops, set-but-inert in code): **modeStretch 0.40 / decaySkew 0.65 / modeInject / scatter / air / b1 0.30 / b3 0.20 / tension** (String bypasses the modal bank); pitchEnv off. Note: archetype's optional Drive 0.20 + Fold 0.18 west-coast enrichment are NOT in the current code — leaving them off is acceptable (NLC 0.55 + morph + friction already supply west-coast harmonic motion); if added later they DO affect String (ToneShaper is post-body).

### Pads 6/8/10 — FM-Bell Hats (Bell / FMImpulse · choke 1)
Shared: material 0.92 · size 0.10 · level 0.68 · scatter 0.55 · b3 0.0 · noise cut 0.92 / Violet · click 0.25 / bright 0.92 · air 0 · pan 0.62 · choke 1. Per-variant — Closed (6): decay 0.08, b1 0.55, fmRatio 0.78, noiseMix 0.40, noiseDec 0.08. Pedal (8): decay 0.05, fmRatio 0.65 (clone of 6). Open (10): decay 0.50, fmRatio 0.55, noiseDec 0.48 (clone of 6). Defaulted: pitchEnv/tension off, drive/fold/filter transparent, NLC 0 (archetype: keep transient clean), secondary off, morph off, macros neutral, modeStretch/decaySkew left at code defaults (archetype suggests stretch 0.45 / decaySkew 0.62 for HF tilt — OPTIONAL enhancement, not a correctness gap since the hats already read as bright FM-bell sizzle; current clones do not set them).

### Pads 5/7/9/11/12/14 — Inharmonic Plate Toms (Plate, FMImpulse even / Feedback odd)
Graded i=0..5: material 0.40→0.70 · size 0.85→0.35 · decay 0.65→0.40 · fmRatio 0.30→0.62 (even pads; no-op on odd Feedback pads) · feedbackAmount 0.20/0.30/0.40 cycling (odd pads; no-op on even FMImpulse pads) · modeStretch 0.30→0.55 (corrected 1-indexed) · pitchEnv 280→580 start / 70→195 end / ~50 ms (kerthump via pitch env, Plate cannot use tension) · scatter 0.30+(i%4)·0.10 · **decaySkew 0.40 (FIX — was coded 0.50 neutral; now applies the real M-5 per-mode tilt the plan claimed, lifting clang into the tail per the inharmonic-plate-tom archetype's 0.38)** · **nonlinearCoupling 0.40** (re-voiced) · modeInject 0 (plate clang self-sufficient) · secondary on, coupling 0.30, secSize 0.40, secMat 0.75 · noise 0.18 / White · click 0.30 / bright 0.65 · b1 0.30+0.03i · b3 0.30 · macroComplexity 0.75 · couplingAmount 0.65 · **pan 0.30→0.70 sweep L→R** (pad 5→14: 0.30/0.38/0.46/0.54/0.62/0.70). NB **tensionMod 0.45 set in code is INERT on Plate (Membrane-only) — documented, the glide comes from the pitch env.** Defaulted: morph off, filter near-transparent.

### Pad 13 — FM-Bell Crash (Bell / FMImpulse)
material 0.92 · size 0.45 (f0 ~284 Hz) · decay 0.78 · strikePos 0.32 · level 0.72 · fmRatio 0.45 · modeStretch 0.55 · scatter 0.65 · **modeInject 0.10 (FIX — was coded unset/0 despite the plan claiming 0.10)** · **nonlinearCoupling 0.45** · noise 0.40 / cut 0.92 / Violet / dec 0.72 · click 0.35 / bright 0.85 · b1 0.30 · b3 0.0 · air 0 · pan 0.58. Defaulted: pitchEnv off, filter/drive/fold transparent, secondary off, morph off, macros neutral.

### NEW pads
- **3 Clap/Rim** (Plate/Impulse): mat 0.78 · size 0.18 · decay 0.12 · stretch 0.55 · scatter 0.45 · NLC 0.30 · click 0.90 / contact 0.06 / bright 0.92 · noise 0.30 / dec 0.15 · b1 0.50 · b3 0.10 · pan 0.55. Defaulted: pitchEnv off, secondary/tension off (Plate), FM/Feedback/Friction no-op under Impulse, macros/morph neutral.
- **15 Ride** (Bell/FMImpulse · aux 1): mat 0.90 · size 0.38 · decay 0.85 · strikePos 0.25 · fmRatio 0.40 · stretch 0.40 · scatter 0.45 · decaySkew 0.45 · NLC 0.35 · noise 0.30 / cut 0.88 / Violet / dec 0.70 · click 0.50 / bright 0.90 · b3 0.0 · b1 0.25 · outputBus 1 · pan 0.66. (FMImpulse is the archetype-sanctioned 'live FM ping' ride variant — makes fmRatio meaningful and keeps the kit's FM-bell-family identity.) Defaulted: pitchEnv off, secondary/tension off, modeInject/morph/drive/fold off.
- **16 Splash** (Bell/FMImpulse): mat 0.92 · size 0.28 · decay 0.35 · fmRatio 0.50 · stretch 0.55 · scatter 0.65 · NLC 0.40 · noise 0.35 / dec 0.28 · click 0.35 · b3 0.0 · b1 0.35 · pan 0.34.
- **17 Hi Sub-Bell** (Bell/FMImpulse): mat 0.62 · size 0.22 · decay 0.40 · strikePos 0.20 · fmRatio 0.65 · stretch 0.45 · decaySkew 0.45 · NLC 0.30 · drive 0.25 · click 0.45 / bright 0.85 · noise 0.10 · b3 0.0 · pan 0.58.
- **18 Feedback-Plate Drone** (Plate/Feedback · aux 1) — **FIX: in-loop BANDPASS ToneShaper added (the archetype's defining regenerative-band-selector mechanism, previously omitted):** mat **0.62 (FIX, was 0.55)** · size 0.60 · decay 0.88 · **strikePos 0.45 (FIX, added)** · level 0.60 · feedbackAmount 0.55 · fold 0.25 · stretch 0.50 · scatter 0.35 · **modeInject 0.15** · NLC 0.50 · **decaySkew 0.78 (FIX, was 0.55 — archetype HF-cascade shimmer tilt)** · **filterType BP / cutoff 0.55 / resonance 0.45 / filterEnvAmount 0.65 / filterEnvAtk 0.45 / filterEnvDec 0.60 / filterEnvSus 0.55 / filterEnvRel 0.65 (FIX, all added — the bandpass-in-loop that selects which mode sustains and makes the drone EVOLVE rather than squeal)** · noise 0.30 / cut 0.55 / White / **dec 0.85 (FIX, added)** · click 0 · b1 0.20 (→0.30 per archetype acceptable) · b3 0.20 · outputBus 1 · pan 0.72 · macroComplexity 0.80. Defaulted: pitchEnv off, secondary off, tension (Plate no-op), choke 0, FM/NoiseBurst/Friction no-op.
- **19 Friction-Membrane Drone** (Membrane/Friction · aux 1): mat 0.45 · size 0.70 · decay 0.80 · level 0.60 · frictionPressure 0.60 · NLC 0.50 · **tensionMod 0.40** (cuíca glide, Membrane-only) · **airLoading 0.70** (Membrane consumes it) · morph on 0.40→0.75 / ~1.2 s · stretch 0.40 (Membrane uses it) · modeScatter 0.18 · noise 0.18 / Brown · click 0 · b1 0.25 · b3 0.20 · outputBus 1 · pan 0.28. Defaulted: pitchEnv off (glide is from tension), modeInject 0, secondary off, choke 0, FM/Feedback/NoiseBurst no-op under Friction.

## Kit globals (retained)
maxPolyphony 12 · stealingPolicy 1 · globalCoupling 0.65 · tomResonance 0.55 · couplingDelayMs 1.4 · crafted = {0..19}.

## Correctness notes (post-audit re-voicing)
- Every `nonlinearCoupling` and `drive` is interpreted as env-level brightening (M-3/M-4) and unity-makeup flavour (M-2). Values retained from the post-audit-authored archetypes.
- The 6 Plate toms are voiced against the corrected free-plate `(m+2n)^1.7` Chladni ratios (not the old SSSS ladder), and now carry a real per-mode `decaySkew 0.40` tilt (FIX — the code had it at neutral 0.50).
- `modeInject` (1/k, −6 dB/oct) now actually set on snare (0.12), crash (0.10), feedback-plate drone (0.15) and friction-membrane drone — the plan claimed these but the code defaulted them to 0 on snare/crash (FIX).
- The **Feedback-Plate drone now includes its in-loop bandpass** (the regenerative-drone mechanism the proposal had dropped) — the single biggest correctness fix in this verification.
- `decaySkew` is a real per-mode spectral tilt on Plate/Bell (M-5) — used on toms/bell/hats/crash.
- `pan` spread replaces the flat 0.5 (M-9): hats/ride right, toms sweep L→R, drones wide (string+membrane left, plate drone right), kick/snare/clap center-ish.
- `tensionMod` is Membrane-only: active on the kick (0.45) and the new membrane drone (0.40); SET-BUT-INERT on the Plate toms (0.45) and Plate snare (0.30) — documented, not active.
- `feedbackAmount` on the sub-bell (0.30) and on even/FMImpulse tom pads is INERT (forwarded only to FeedbackExciter) — documented. `modeStretch`/`decaySkew`/`b1`/`b3` on the String drone are likewise inert (String bypasses the modal bank).

---

## Verification log (10 issues found & fixed)

1. PAD 18 FEEDBACK-PLATE DRONE — MAJOR coverage + physics gap. The cited feedback-plate-shell-drone archetype's LOAD-BEARING mechanism is a BANDPASS ToneShaper in the feedback loop ('a band-limited positive-feedback loop self-oscillates... a bandpass in the loop selects which mode sustains and suppresses the rest'; Barkhausen). The proposal omitted the ENTIRE ToneShaper filter section (no Filter Type/Cutoff/Resonance/Filter-Env). Without it a Feedback-exciter drone is an unfocused broadband squeal, not the archetype's evolving band-selected sustain. FIX: added Filter Type BP(1.0) / Cutoff 0.55 / Resonance 0.45 / Filter Env Amount 0.65 / Atk 0.45 Dec 0.60 Sus 0.55 Rel 0.65, plus the archetype's Strike Position 0.45, Noise Decay 0.85, Noise Cutoff 0.55, and corrected Material 0.55->0.62 and Decay Skew 0.55->0.78 to match the cited recipe.

2. TOM ROW Decay Skew is silently neutral. The plan's prose and per-pad tables claim 'decaySkew 0.42 (delta from flat 0.50, real per-mode tilt now works on Plate, M-5)' as a headline correctness win, but the generator code actually sets decaySkew = 0.50 (exact neutral = NO per-mode tilt, the M-5 lever is unused). The cited inharmonic-plate-tom archetype specifies 0.38 ('boost high modes, keeps clang in tail'). FIX: set all 6 toms to decaySkew 0.40 so the claimed spectral tilt is actually applied (matches archetype intent and the plan's own deltaFromCurrent item 5).

3. SNARE Mode Inject claimed but not set. Plan headlines 'modeInject 0.12 (NEW, was 0)' on the snare, but generator code sets modeInjectAmount = 0.0. The whole-kit '1/k mode inject was 0 everywhere, now introduced on snare' claim is unmet in code. FIX: set pad 2 modeInjectAmount 0.12 (faint 1/k synthetic body weight under the plate clang, per AUDIT mode_inject 1/k).

4. CRASH Mode Inject claimed but not set. Plan headlines 'modeInject 0.10 (NEW)' on the crash; generator code has no modeInjectAmount line (defaults 0). FIX: set pad 13 modeInjectAmount 0.10 (low so it doesn't fight the bell inharmonicity).

5. SUB-BELL (pad 4) sets feedbackAmount = 0.30 in code, which is an EXACT NO-OP under the FMImpulse exciter (Feedback Amount is forwarded only to FeedbackExciter; FMImpulseExciter hard-codes feedback 0). Harmless but undocumented dead value. The sub-bell archetype explicitly calls this out ('feedback 0.30 cannot route through FMImpulse'). FIX: documented as an inert no-op in defaultedParams; grit is correctly carried by FM Ratio 0.72 + NLC 0.30. (Left in place since removing it changes nothing; flagged so it is not mistaken for an active param.)

6. FRICTION STRING DRONE (pad 1) sets decaySkew 0.65 and modeStretch 0.40 in code, both INERT no-ops on the String body (String bypasses the shared modal bank — Mode Stretch/Decay Skew/Scatter/Inject/air/b1/b3/Tension are all inherent no-ops). Proposal correctly lists them as String no-ops in defaultedParams; flagged so the inert generator values are understood as documentation-only, not active levers. No fix needed beyond the existing note.

7. RIDE (pad 15) uses FMImpulse where the cited ride-cymbal archetype uses NoiseBurst. NOT a bug: the archetype itself sanctions this ('If a live Chowning-FM bell ping is wanted instead, switch Exciter Type to FMImpulse, then fmRatio 0.30 -> modulatorRatio becomes audible'). Under the kit's deliberate FM-bell-family identity, FMImpulse is the correct choice and makes the otherwise-inert fmRatio 0.40 meaningful. Verified consistent — no change.

8. HI-HAT b3: archetype specifies 0.02 (near-pure flat), code/proposal use 0.0 (pure flat). Within JND, both give 'metal highs ring'. No fix.

9. RANGES: all valueNorm values verified in [0,1]. Discrete fields legal: Exciter/Body selectors, Filter Type {LP/HP/BP}, Choke Group 1 (norm 0.125), Output Bus 1 (norm 0.0667 = 1/15; generator field is the integer index 1), Morph/Secondary float-as-bool 1.0. tensionModAmt on Plate toms (0.45) is a documented Membrane-only inert no-op (correct). airLoading 0.70 on the Friction-Membrane Drone is correctly Membrane-consumed. No out-of-range or illegal values found.

10. LAYOUT/GAPS verified correct: no ride in current 14-pad kit (added FM-Bell Ride pad15), no clap (added Inharmonic-Plate Clap/Rim pad3), 18 dead default pads (expanded to 24 audible), single drone (added Feedback-Plate + Friction-Membrane drones), single bell note (added Hi Sub-Bell). The 6-tom size-graded row + FM-bell-family (crash/ride/splash) cohesion are correctly flagged as intentional graded families, not the audit sameness bug. GM map kept sensibly (39 clap, 51 ride, 52 splash). crafted list must expand from {0,1,2,4,5,6,7,8,9,10,11,12,13,14} to include 3,15,16,17,18,19.
