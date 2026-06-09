<!-- verdict: pass-with-fixes | coverageOk: false | issues fixed: 8 | IMPLEMENTED: 2026-06-09 (commit re-tune 808 Electronic Kit) -->

# 808 Electronic Kit — Corrected Re-Design (electronicKit())  [VERIFIED + FIXED]

**Category:** Electronic · **Builder:** `electronicKit()` · Voiced against the post-audit DSP (measured-strike body norm / linear Level + -1 dBTP bus limiter; Plate free-plate Chladni; airLoading membrane-only; modeInject 1/k; per-mode decaySkew tilt; stretch B_max 0.01; NonlinearCoupling = amplitude bloom; Drive = flavour; per-pad pan; Membrane-only tensionMod).

> Adversarial verification applied 7 corrections (see "Corrections applied" below). The strong kick/snare/tom/hat skeleton is preserved; three physics contradictions and two coverage gaps were fixed against the cited archetypes.

## Identity
A clean, deep TR-808 machine: sub-sine boom kick, boom-glide toms (the best pitch-env motion in the factory library), two-tone snappy snares with a metallic secondary shell, bright atonal filtered-noise hats, the iconic detuned-fifth cowbell, the 808 clap, and dense inharmonic crashes/ride. The bright/clean counterpart to the Acoustic kits; the warm/dry sibling of the 909 kit.

## Corrections applied (vs the proposed plan)
1. **Kick PitchEnv Start 0.6505 -> 0.5000.** 0.6505 = 400 Hz; the kick archetype + every summary line say 200 Hz, which is norm 0.5.
2. **Cowbell Mode Inject 0.18 -> 0 (defaulted).** The cited cowbell archetype deliberately defaults modeInject (integer harmonics fight the inharmonic detuned-fifth clang).
3. **All six toms Tension Mod 0.30 -> 0 (defaulted).** The 808-tom archetype is explicit: tensionMod is an upward glide that fights the descending boom-glide. (Kick keeps 0.30 — archetype-endorsed.)
4. **Pad 4 (snare sister) coverage:** added Strike Position 0.45, Click Contact 0.08, Click Brightness 0.74, Noise Resonance 0.12, NoiseBurst Duration 0.1538 (were silently defaulted; meaningful for a Membrane snare).
5. **Pad 1 (rim) coverage:** added Body Damping b3 0.06 (meaningful for Shell; was sentinel/derived).
6. **Ride (15) NoiseBody:** kept but explicitly flagged as a documented delta from the Bell-body ride archetype (808 has no real ride; NoiseBody differentiates it from the cowbell/FM-bell Bell voices).
7. **Summary prose:** crashes use the NonlinearCoupling bloom for HF rise, NOT decaySkew (crash skew is neutral, per the crash archetype) — reconciled.

## Layout (GM map, pad = MIDI-36)
| Pad | MIDI | Drum | Body | Exciter | Status |
|----|----|------|------|---------|--------|
| 0 | 36 | 808 Kick | Membrane | Impulse | kept (PitchEnv Start fixed 0.5) |
| 1 | 37 | Side Stick / Rim | Shell | Impulse | **NEW** (was disabled FM-bell) +b3 |
| 2 | 38 | 808 Snare (deep) | Membrane | NoiseBurst | kept |
| 3 | 39 | 808 Clap | NoiseBody | NoiseBurst | **NEW** (was disabled FM-bell) |
| 4 | 40 | 808 Snare (bright) | Membrane | NoiseBurst | kept (coverage filled) |
| 5 | 41 | Tom 1 (lowest) | Membrane | Mallet | kept (tensionMod -> 0) |
| 6 | 42 | Closed Hat | NoiseBody | NoiseBurst | kept (+scatter/stretch/skew) |
| 7 | 43 | Tom 2 | Membrane | Mallet | kept (tensionMod -> 0) |
| 8 | 44 | Pedal Hat | NoiseBody | NoiseBurst | kept |
| 9 | 45 | Tom 3 | Membrane | Mallet | kept (tensionMod -> 0) |
| 10 | 46 | Open Hat | NoiseBody | NoiseBurst | kept (+HP filter/stretch/skew) |
| 11 | 47 | Tom 4 | Membrane | Mallet | kept (tensionMod -> 0) |
| 12 | 48 | Tom 5 | Membrane | Mallet | kept (tensionMod -> 0) |
| 13 | 49 | Crash 1 | NoiseBody | NoiseBurst | kept (+bloom/scatter/inject/aux) |
| 14 | 50 | Tom 6 (highest) | Membrane | Mallet | kept (tensionMod -> 0) |
| 15 | 51 | Ride | NoiseBody | NoiseBurst | **NEW** (delta from Bell ride archetype) |
| 20 | 56 | Cowbell | Bell | FMImpulse | **NEW** (modeInject -> 0) |
| 21 | 57 | Crash 2 (bright) | NoiseBody | NoiseBurst | **NEW** |
| 23 | 59 | FM-Bell Perc | Bell | FMImpulse | promoted (was disabled) |
| 16-19,22,24-31 | 52-55,58,60-67 | — | Bell | FMImpulse | **disabled (gap)** |

25 sounding pads (was 13). Crafted list: `{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,20,21,23}`.

## Gaps / duplicates
- **Fixed gaps:** ride (15+23), clap (3), side-stick (1), cowbell (20) — all were disabled FM-bell clones (a kit with no ride/clap/cowbell).
- **Removed duplicate:** 19 byte-identical FM-bell perc clones; each retained perc slot now has a distinct role.
- **Intentional remaining gaps (disabled):** pads 16-19, 22, 24-31 (GM 52-55,58,60-67) — the 808 has no canonical voices there; honest empty Bell defaults, not filler.
- **Body-model note (ride):** GM 51 Ride is NoiseBody (not the archetype's Bell). Deliberate: the TR-808 has no ride, so an electronic NoiseBody ride is a valid synthetic voice that differentiates it from the kit's two Bell voices (cowbell 20, FM-bell 23). Documented delta, not a silent swap.

## Full param-surface usage (the audit §4 dead axes, now exercised)
- **PitchEnv** (hero): kick **200->40** (FIXED: Start now 0.5), six tom glides 220->80 … 500->220 (long times), snares 400/480->110/160.
- **modeStretch** (B_max 0.01): hats 0.45, crash 0.60, crash2 0.65, ride 0.52, cowbell 0.55, snares 0.42; neutral on kick/toms.
- **decaySkew** tilt (M-5): snares 0.62, closed/open hats 0.55/0.60, ride 0.58, cowbell 0.42; neutral on kick/toms; **crashes neutral (NLC bloom drives their HF instead)**.
- **modeScatter:** hats 0.35, crash 0.60, crash2 0.70, ride 0.45, clap 0.40, cowbell 0.20, rim 0.30; zero on kick/toms.
- **modeInject 1/k:** crash/crash2 0.25 only. **(Cowbell modeInject removed — integer harmonics fight the inharmonic clang.)**
- **NonlinearCoupling bloom** (M-3/M-4): crash/crash2 0.35, ride 0.25.
- **tensionModAmt** (Membrane-only): **kick 0.30, snares 0.35 only.** **(Toms set to 0 — upward tension fights the descending boom-glide, per 808-tom archetype.)**
- **Secondary shell:** snare metallic upper tone (coupling 0.35 / size 0.38 / mat 0.85).
- **Drive** (flavour): snares 0.48.
- **ToneShaper filter:** snare LP+env sweep; open-hat HP ~800 Hz.
- **Pan + Output Bus:** tom L->R fill spread 0.30->0.70, hats 0.55, crash1/crash2 0.40/0.62, ride 0.60, cowbell 0.42, perc 0.58; cymbals -> aux bus 1.
- **Choke group 1:** closed/pedal/open hats.
- **Bodies:** Membrane, NoiseBody, Shell, Bell (String unused — no waveguide voice in an 808). **Exciters:** Impulse, NoiseBurst, Mallet, FMImpulse.

## Per-pad exact values (CORRECTED)
Pitch-env norms use `toLogNorm(hz)=ln(hz/20)/ln(100)`; PitchEnv Time norm × 500 ms; choke 0.125->group 1; output-bus 0.0667->bus 1.

### Pad 0 — 808 Kick (Membrane / Impulse)
Material 0.15, Size 0.90 (f0 ~63 Hz), Decay 0.35, Level 0.85, **PitchEnv Start 0.5000 (200 Hz) [FIXED from 0.6505]**, PitchEnv End 0.1505 (40 Hz), PitchEnv Time 0.06 (30 ms), PitchEnv Curve 0.15, Tension Mod 0.30 (snap, archetype-endorsed), Click Mix 0.35 / Contact 0.20 / Brightness 0.30, Noise Mix 0, Air Loading 0, Mode Scatter 0, Pan 0.50. Defaulted: modeStretch/decaySkew/modeInject neutral (pure sub-sine), filter/drive/fold off, secondary/coupling off, FM/Feedback/Friction/NoiseBurst no-ops, macros 0.5.

### Pad 1 — Side Stick / Rim (Shell / Impulse)
Material 0.55, Size 0.22, Decay 0.14, Level 0.78, Strike Position 0.70 (edge -> free-free end antinode), Click Mix 0.85 / Contact 0.08 / Brightness 0.85, Mode Scatter 0.30, Body Damping b1 0.50, **Body Damping b3 0.06 [ADDED]** (electronic bright rim, highs ring slightly), Noise Mix 0, Pan 0.55. Defaulted: PitchEnv off (rigid bar), Air/Tension no-op (Shell), secondary/coupling off, drive/fold/filter bypassed, modeInject/NLC 0, FM/Feedback/Friction/NoiseBurst no-ops.

### Pad 2 — 808 Snare deep (Membrane / NoiseBurst)
Material 0.58, Size 0.68, Decay 0.85, Strike Position 0.45, Drive 0.48, PitchEnv 400->110 (Start 0.6505 / End 0.3702) / Time 0.13 / Curve 0.10, Mode Stretch 0.42, Decay Skew 0.62, Noise Mix 0.62 / Cutoff 0.82 / Color 0.78 / Decay 0.40 / Resonance 0.12, Click Mix 0.55 / Contact 0.08 / Brightness 0.72, NoiseBurst Duration 0.1538, Body Damping b1 0.18 / b3 0.05, Secondary Enabled 1 / Size 0.38 / Material 0.85, Coupling Strength 0.35, Tension Mod 0.35, Filter Cutoff 0.80 / Env Amount 0.60, Pan 0.50. Defaulted: Air 0 (electronic), Mode Scatter 0 (secondary+noise carry spread), modeInject 0, Fold/Morph off, FM/Feedback/Friction no-ops, macros 0.5.

### Pad 3 — 808 Clap (NoiseBody / NoiseBurst)
Material 0.85, Size 0.18, Decay 0.18, Level 0.78, NoiseBurst Duration 0.55 (~9 ms flam smear), Mode Scatter 0.40, Noise Mix 0.85 / Cutoff 0.78 / Resonance 0.40 (Q~2.18, the 909 formant) / Decay 0.20 / Color 0.65, Click Mix 0.45 / Contact 0.22 / Brightness 0.62, Body Damping b1 0.50 / b3 0.0, Macro Brightness 0.65, Macro Complexity 0.55, Pan 0.50. Defaulted: PitchEnv off, modeStretch/decaySkew neutral, modeInject/NLC 0 (noise clap), secondary/coupling/tension off, air no-op, FM/Feedback/Friction no-ops.

### Pad 4 — 808 Snare bright sister (Membrane / NoiseBurst)  [COVERAGE FILLED]
Material 0.66, Size 0.60, Decay 0.72, **Strike Position 0.45 [ADDED]**, Drive 0.48, PitchEnv 480->160 (Start 0.6901 / End 0.4515) / Time 0.10, Mode Stretch 0.42, Decay Skew 0.62, Noise Mix 0.62 / Cutoff 0.86 / Color 0.78 / Decay 0.32 / **Resonance 0.12 [ADDED]**, Click Mix 0.55 / **Contact 0.08 [ADDED]** / **Brightness 0.74 [ADDED]**, **NoiseBurst Duration 0.1538 [ADDED]**, Secondary Enabled 1 / Size 0.38 / Material 0.85, Coupling Strength 0.35, Tension Mod 0.35, Pan 0.50. Defaulted: Air 0, Mode Scatter 0, modeInject 0, Fold/Morph off, FM/Feedback/Friction no-ops, macros 0.5.

### Pads 5,7,9,11,12,14 — 808 Toms (Membrane / Mallet)  [TENSION MOD FIXED -> 0]
Size-graded boom-glide (small=high). T1(5): Mat 0.18 Size 0.85 Dec 0.65 b1 0.10, PitchEnv 220->80 Time 0.50, Pan 0.30. T2(7): Mat 0.25 Size 0.75 Dec 0.58 b1 0.15, 260->95 Time 0.42, Pan 0.40. T3(9): Mat 0.32 Size 0.65 Dec 0.50 b1 0.20, 310->115 Time 0.36, Pan 0.48. T4(11): Mat 0.40 Size 0.55 Dec 0.43 b1 0.25, 370->140 Time 0.30, Pan 0.55. T5(12): Mat 0.50 Size 0.48 Dec 0.35 b1 0.32, 430->175 Time 0.24, Pan 0.62. T6(14): Mat 0.60 Size 0.40 Dec 0.28 b1 0.42, 500->220 Time 0.18, Pan 0.70. All: b3 0.10, Level 0.85, PitchEnv Curve 0.50, Noise Mix 0.05 / Color 0.40 / Decay 0.55, Click Mix 0.05, Air 0, **Tension Mod 0 [FIXED from 0.30] (upward glide fights the descending boom — per 808-tom archetype)**. Defaulted: modeStretch/decaySkew/modeInject/scatter neutral/0 (tuned sine), secondary/drive/fold off, FM/Feedback/Friction/NoiseBurst no-ops.

### Pad 6 — 808 Closed Hat (NoiseBody / NoiseBurst)
Material 0.92, Size 0.10, Decay 0.08, Level 0.75, Choke 0.125 (group 1), NoiseBurst Duration 0.0769 (~3 ms), Noise Mix 0.85 / Cutoff 0.92 / Color 0.85 / Decay 0.10 / Resonance 0.20, Mode Scatter 0.35, Mode Stretch 0.45, Decay Skew 0.55, Click Mix 0, Body Damping b1 0.65 / b3 0.0, Air 0, Pan 0.55. Defaulted: PitchEnv off, modeInject/NLC 0 (static atonal), secondary/coupling/tension no-op, drive/fold/filter bypassed, FM/Feedback/Friction no-ops.

### Pad 8 — 808 Pedal Hat (NoiseBody / NoiseBurst)
Material 0.88, Size 0.12, Decay 0.06, Level 0.70, Choke 0.125, Noise Decay 0.07, Body Damping b1 0.72 / b3 0.0, Mode Scatter 0.35, Mode Stretch 0.45, Click Mix 0, Pan 0.55. Defaulted: PitchEnv/modeInject/NLC/decaySkew neutral, secondary/air/tension no-op, FM/Feedback/Friction no-ops.

### Pad 10 — 808 Open Hat (NoiseBody / NoiseBurst)
Material 0.90, Size 0.20, Decay 0.50, Level 0.72, Strike Position 0.60, Filter Type 0.50 (HP) / Cutoff 0.534 (~800 Hz HP), Choke 0.125, NoiseBurst Duration 0.15, Noise Mix 0.80 / Cutoff 0.82 / Resonance 0.20 / Decay 0.55 / Color 0.90, Mode Stretch 0.45, Decay Skew 0.60, Mode Scatter 0.35, Click Mix 0, Pan 0.55. Defaulted: PitchEnv off, modeInject/NLC 0, b1/b3 sentinel (derive longer open-hat ring), secondary/air/tension no-op, FM/Feedback/Friction no-ops.

### Pad 13 — 808 Crash 1 (NoiseBody / NoiseBurst)
Material 0.95, Size 0.35, Decay 0.70, Level 0.70, Strike Position 0.55, Mode Stretch 0.60, Mode Inject 0.25, Nonlinear Coupling 0.35 (bloom), Mode Scatter 0.60, Body Damping b1 0.30 / b3 0.0, Noise Mix 0.55 / Cutoff 0.92 / Color 0.82 / Decay 0.65, Click Mix 0.20 / Brightness 0.82, Output Bus 0.0667 (aux 1), Pan 0.40. Defaulted: PitchEnv off, **Decay Skew neutral (the NLC bloom drives the HF rise, per crash archetype)**, secondary/coupling/tension no-op, air no-op, drive/fold/filter bypassed, FM/Feedback/Friction no-ops.

### Pad 15 — 808 Ride (NoiseBody / NoiseBurst)  [body-model delta documented]
Material 0.93, Size 0.30, Decay 0.55, Level 0.68, Strike Position 0.40 (focused ping), Mode Stretch 0.52, Mode Scatter 0.45, Decay Skew 0.58, Nonlinear Coupling 0.25 (milder bloom), Body Damping b1 0.35 / b3 0.0, Noise Mix 0.40 / Cutoff 0.88 / Color 0.82 / Decay 0.40, Click Mix 0.30 / Brightness 0.85 (ping onset), Output Bus 0.0667 (aux 1), Pan 0.60. Defaulted: PitchEnv off, Mode Inject 0 (ping carried by click+body), secondary/coupling/tension/air no-op, drive/fold/filter bypassed, FM/Feedback/Friction no-ops. NOTE: NoiseBody (not the Bell-body ride archetype) — a deliberate electronic-808 interpretation; differentiates from the cowbell/FM-bell Bell voices.

### Pad 20 — 808 Cowbell (Bell / FMImpulse)  [MODE INJECT FIXED -> 0]
Material 0.78, Size 0.22 (f0_nom ~471 Hz), Decay 0.30, Level 0.75, Strike Position 0.30, FM Ratio 0.50 (mod ratio 2.5, detuned-fifth), Click Mix 0.55 / Contact 0.10 / Brightness 0.72, Noise Mix 0.10 / Color 0.40 / Cutoff 0.62 / Decay 0.20, Body Damping b1 0.32 / b3 0.0, Mode Stretch 0.55, Mode Scatter 0.20, Decay Skew 0.42, **Mode Inject 0 [FIXED from 0.18] (integer harmonics fight the inharmonic clang — per cowbell archetype)**, Macro Brightness 0.65, Pan 0.42. Defaulted: PitchEnv off, NonlinearCoupling 0 (preserve pure clang), Air/Tension no-op (Bell), secondary/coupling off, drive/fold/filter bypassed, Feedback/NoiseBurst/Friction no-ops.

### Pad 21 — 808 Crash 2 bright (NoiseBody / NoiseBurst)
Material 0.96, Size 0.28, Decay 0.55, Level 0.68, Strike Position 0.60, Mode Stretch 0.65, Mode Scatter 0.70, Nonlinear Coupling 0.35, Mode Inject 0.25, Body Damping b1 0.34 / b3 0.0, Noise Mix 0.55 / Cutoff 0.94 / Color 0.85 / Decay 0.55, Click Mix 0.20, Output Bus 0.0667 (aux 1), Pan 0.62. Defaulted: PitchEnv off, **Decay Skew neutral (NLC bloom drives HF)**, secondary/coupling/tension/air no-op, drive/fold/filter bypassed, FM/Feedback/Friction no-ops.

### Pad 23 — FM-Bell Perc / Tuned Bell (Bell / FMImpulse)
Material 0.70, Size 0.25 (f0 ~450 Hz), Decay 0.20, Level 0.75, Strike Position 0.30, FM Ratio 0.40 (mod ratio 2.2), Body Damping b3 0.0, Click Mix 0.15 / Brightness 0.70, Noise Mix 0, Pan 0.58. Defaulted: PitchEnv off, modeStretch neutral (Bell already inharmonic), decaySkew/modeInject/NLC/scatter 0 (clean tonal ping), air/tension/secondary/coupling no-op, drive/fold/filter bypassed, Feedback/NoiseBurst/Friction no-ops.

### Quick-reference value table (corrected; sounding pads, key params)
| Pad | Drum | Mat | Size | Dec | Stretch | Skew | Scatter | Inject | NLC | Tens | Pan | Bus |
|----|------|-----|------|-----|---------|------|---------|--------|-----|------|-----|-----|
| 0 | Kick | .15 | .90 | .35 | — | — | 0 | 0 | 0 | .30 | .50 | 0 |
| 1 | Rim | .55 | .22 | .14 | — | — | .30 | 0 | 0 | — | .55 | 0 |
| 2 | Snare deep | .58 | .68 | .85 | .42 | .62 | 0 | 0 | 0 | .35 | .50 | 0 |
| 3 | Clap | .85 | .18 | .18 | — | — | .40 | 0 | 0 | — | .50 | 0 |
| 4 | Snare bright | .66 | .60 | .72 | .42 | .62 | 0 | 0 | 0 | .35 | .50 | 0 |
| 5 | Tom 1 | .18 | .85 | .65 | — | — | 0 | 0 | 0 | **0** | .30 | 0 |
| 6 | Closed Hat | .92 | .10 | .08 | .45 | .55 | .35 | 0 | 0 | — | .55 | 0 |
| 7 | Tom 2 | .25 | .75 | .58 | — | — | 0 | 0 | 0 | **0** | .40 | 0 |
| 8 | Pedal Hat | .88 | .12 | .06 | .45 | — | .35 | 0 | 0 | — | .55 | 0 |
| 9 | Tom 3 | .32 | .65 | .50 | — | — | 0 | 0 | 0 | **0** | .48 | 0 |
| 10 | Open Hat | .90 | .20 | .50 | .45 | .60 | .35 | 0 | 0 | — | .55 | 0 |
| 11 | Tom 4 | .40 | .55 | .43 | — | — | 0 | 0 | 0 | **0** | .55 | 0 |
| 12 | Tom 5 | .50 | .48 | .35 | — | — | 0 | 0 | 0 | **0** | .62 | 0 |
| 13 | Crash 1 | .95 | .35 | .70 | .60 | — | .60 | .25 | .35 | — | .40 | 1 |
| 14 | Tom 6 | .60 | .40 | .28 | — | — | 0 | 0 | 0 | **0** | .70 | 0 |
| 15 | Ride | .93 | .30 | .55 | .52 | .58 | .45 | 0 | .25 | — | .60 | 1 |
| 20 | Cowbell | .78 | .22 | .30 | .55 | .42 | .20 | **0** | 0 | — | .42 | 0 |
| 21 | Crash 2 | .96 | .28 | .55 | .65 | — | .70 | .25 | .35 | — | .62 | 1 |
| 23 | FM-Bell | .70 | .25 | .20 | — | — | 0 | 0 | 0 | — | .58 | 0 |

("—" = documented neutral/sentinel default. Kick PitchEnv Start = 0.5000 (200 Hz). Toms Tension = 0. Cowbell Inject = 0.)

## Delta from current
13 -> 25 crafted pads; fills clap/rim/ride/cowbell/crash2/FM-bell into 6 previously-disabled FM-bell clones (removes the 19-way duplicate). Activates modeScatter, modeStretch, decaySkew, modeInject, NonlinearCoupling and pan, all of which the current kit leaves at default on every pad. Adds explicit hat b1/b3 + scatter, cymbal aux routing, and the stereo tom-fill spread. The strong existing kick/snare/tom core values are preserved and only augmented. Post-verification: kick pitch-env start, cowbell modeInject, and tom tensionMod corrected to match the cited 808-kick / cowbell / 808-tom archetypes; Pad 4 and Pad 1 coverage gaps filled.

---

## Verification log (8 issues found & fixed)

1. KICK PITCH-ENV BUG (physical/value mismatch): Pad 0 PitchEnv Start valueNorm=0.6505 decodes to 400 Hz, but its rationale AND the kit summary repeatedly state '200 Hz glide start' / 'kick 200->40 Hz boom'. The cited 808-kick archetype is explicit: PitchEnv Start = 0.5 -> 200 Hz (400 Hz = 0.6505). FIX: Pad 0 PitchEnv Start -> 0.5 (200 Hz). Verified node: ln(200/20)/ln(100)=0.5000; ln(400/20)/ln(100)=0.6505.

2. COWBELL MODE-INJECT contradicts its own cited archetype (physical correctness): Pad 20 sets Mode Inject 0.18 'harmonic fill to thicken the clang', but the cowbell archetype explicitly lists Mode Inject under 'Deliberately defaulted (exact bypass -- preserve the pure inharmonic clang)'. ModeInject is 8 INTEGER harmonics (1/k) at f0; an integer series fights the cowbell's defining detuned-fifth/inharmonic beat -- the same reasoning the proposal itself uses to zero modeInject on hats/clap. FIX: Pad 20 Mode Inject -> 0 (moved to documented defaults). Kept Mode Stretch 0.55 + Mode Scatter 0.20 (archetype-sanctioned) for the clang.

3. 808 TOM TENSION-MOD contradicts the cited archetype (physical correctness): all six toms (5,7,9,11,12,14) set Tension Mod 0.30, but the 808-tom archetype states crucially 'tensionModAmt=0 -- tension-mod is an UPWARD energy-driven glide and would fight the descending pitch-env sweep'. The toms' hero motion is the 220->80 Hz DOWNWARD boom-glide; an upward tension overshoot works against it. FIX: Tension Mod -> 0 on all six toms (moved to documented defaults: 'OFF; upward tension glide fights the descending boom-glide, per 808-tom archetype'). NOTE: the KICK keeps Tension 0.30 -- that IS endorsed by the 808-kick archetype as attack-snap reinforcement, so it is unchanged.

4. COVERAGE GAP on Pad 4 (bright snare sister): silently omits several meaningful Membrane-snare params that Pad 2 sets and the 808-snare archetype specifies -- Strike Position (Membrane mode-shape sampling), Click Contact, Click Brightness, Noise Resonance, NoiseBurst Duration. FIX: added Strike Position 0.45, Click Contact 0.08, Click Brightness 0.74, Noise Resonance 0.12, NoiseBurst Duration 0.1538 to Pad 4 (mirroring Pad 2 with the bright-sister tilt), so no meaningful snare param is silently defaulted.

5. COVERAGE GAP on Pad 1 (side-stick/rim): omits Body Damping b3, which is meaningful for a Shell body (the archetype sets b3 0.10 = woody f^2 damping so highs die first). Left at sentinel it derives from Material, which for the bright-rim Material 0.55 gives an arbitrary value. FIX: added Body Damping b3 0.06 (electronic bright rim -- highs ring a touch longer than the acoustic 0.10 woody value but not pure-flat metal).

6. RIDE BODY-MODEL diverges from the cited ride archetype (flagged, kept as documented kit-character): Pad 15 uses NoiseBody, but the ride archetype specifies Bell body ('Bell body = the tuned ping skeleton') for the ride's defining tonal ping; a NoiseBody ride is effectively a short crash and loses the ping. This is defensible (the TR-808 has no real ride; an electronic NoiseBody ride is a valid synthetic interpretation, and the kit already places Bell voices on the cowbell pad 20 and FM-bell pad 23, so NoiseBody differentiates the ride). KEPT NoiseBody but: (a) added an explicit note, (b) confirmed Strike Position 0.40 set for ping focus, (c) Click Mix 0.30 carries the ping onset. Recorded as a documented delta, not a silent body swap.

7. SUMMARY TEXT inconsistency (cosmetic, corrected): the kitCharacter/deltaFromCurrent text claimed 'decaySkew +tilt on hats/crash/ride/snares/cymbals', but Crash 1 and Crash 2 actually leave Decay Skew NEUTRAL (the NonlinearCoupling bloom drives their HF rise instead -- which is correct per the crash archetype). Corrected the prose to 'decaySkew +tilt on hats/ride/snares/cowbell; crashes use the NLC bloom for HF instead of skew'.

8. RANGE/ENCODING CHECK (all pass): every valueNorm is in [0,1]; discrete encodings verified -- choke 0.125->group 1, outputBus 0.0667->bus 1, fmRatio 0.50->2.5 (cowbell detuned-fifth) / 0.40->2.2 (FM-bell), modeStretch norms ->1.13/1.18/1.28/1.33/1.40/1.48 phys (match cited), decaySkew norms ->+0.24/+0.16/+0.20/+0.10/-0.16 (match cited), noiseBurst 0.1538->4ms. No out-of-range or illegal sentinel values found.
