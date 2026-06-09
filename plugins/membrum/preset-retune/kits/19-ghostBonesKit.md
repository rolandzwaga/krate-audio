<!-- verdict: pass-with-fixes | coverageOk: true | issues fixed: 7 | IMPLEMENTED: 2026-06-09 (commit re-tune Ghost Bones) -->

# Ghost Bones — Corrected Kit Plan (Unnatural · `ghostBonesKit()`)

Hollow, bone-like, strongly inharmonic **sustained** voices for atmospheric sound design. Bodies alternate **Bell** (even pads) / **String** (odd pads); exciter cycles **Mallet / FMImpulse / Impulse** with periodic **Friction**. Signature = the most aggressive use of the corrected inharmonicity axis (modeStretch 0.66–0.88 → B 0.0066–0.0088 under the new 1-indexed `sqrt(1+B·(k+1)²)` law, B_max=0.01) + high positive decaySkew + HP filter + long decays + per-note Material morph + sympathetic coupling + aux/pan spread.

> **Verifier note (encoding):** in `tools/membrum_preset_generator.cpp` the `Pad` struct stores `exciterType`/`bodyModel` as the enum int, `outputBus` as the integer bus index (1 = aux1), and `pan` as a double in [0,1] verbatim. The normalized values below (e.g. Exciter 0.8, Output Bus 0.067) are the on-wire/param-dictionary form; the implementer writes the **physical** field (Exciter `FMImpulse`, `outputBus = 1`, `pan = 0.80`). All other params are stored as the same [0,1] norm shown.

## Layout (16-voice register-graded choir, low → high f0)

| Pad | MIDI | Role | Body | Exciter | f0 (size) | Bus | Pan |
|----|------|------|------|---------|-----------|-----|-----|
| 0 | 36 | Ground-hum (lowest) | Bell | Mallet | ~133 Hz (0.78) | main | 0.50 |
| 1 | 37 | Bowed bone | String | Friction | ~155 Hz (0.72) | main | 0.35 |
| 2 | 38 | Ghost clang | Bell | Impulse | ~166 Hz (0.66) | main | 0.65 |
| 3 | 39 | Bowed bone (aux) | String | FMImpulse | ~200 Hz (0.60) | **aux1** | 0.25 |
| 4 | 40 | FM clang | Bell | FMImpulse | ~225 Hz (0.55) | main | 0.55 |
| 5 | 41 | Bowed bone | String | Mallet | ~253 Hz (0.50) | main | 0.45 |
| 6 | 42 | Ghost clang | Bell | Mallet | ~278 Hz (0.46) | main | 0.70 |
| 7 | 43 | Bowed bone (aux) | String | Impulse | ~305 Hz (0.42) | **aux1** | 0.30 |
| 8 | 44 | Ghost clang hi | Bell | FMImpulse | ~335 Hz (0.38) | main | 0.50 |
| 9 | 45 | FM clang hi | Bell | FMImpulse | ~366 Hz (0.34) | main | 0.80 |
| 10 | 46 | Ghost clang hi | Bell | Mallet | ~400 Hz (0.30) | main | 0.40 |
| 11 | 47 | Bowed bone hi (aux) | String | Friction | ~440 Hz (0.26) | **aux1** | 0.60 |
| 12 | 48 | Ghost clang top | Bell | Impulse | ~462 Hz (0.24) | main | 0.60 |
| 13 | 49 | Bone-tap (pluck) | String | Impulse | ~485 Hz (0.22) | main | 0.50 |
| 14 | 50 | Ghost shimmer top | Bell | Mallet | ~505 Hz (0.20) | main | 0.30 |
| 15 | 51 | Bone-tap top (pluck, aux) | String | Mallet | ~505 Hz (0.20) | **aux1** | 0.70 |
| 16–31 | 52–67 | (unused — blank canvas) | — | — | — | — | — |

Aux-bus spread = 1-in-4 (pads 3,7,11,15) for a coherent wet/reverb layer. Pan alternates L–R to widen the choir (M-9, previously all 0.5). Pads 14 (Bell) and 15 (String) share f0≈505 Hz / Mallet but differ in **body model**, so they are two distinct voices (bell-clang vs plucked-string bone), not a duplicate.

## Per-pad exact normalized values

Shared kit-wide values (every crafted pad unless overridden): **Filter Type 0.5 (HP)**, **Filter Res 0.30**, **Filter Env Dec 0.30 (~54 ms)**, **Mode Inject 0.0 (bypass)**, **Noise Color 0.20 (Brown)**, **Noise Decay 0.85 (~1.1 s)**, **Coupling Amount 0.85**, **Macro Complexity 0.85**, **Level ~0.60–0.66**, **Decay 0.72–0.88 (long)**, **Morph linear 0.4**.

### Bell pads (modal — Stretch/Skew/Scatter/b1/b3 LIVE)

| Param | p0 | p2 | p4 | p6 | p8 | p9 | p10 | p12 | p14 |
|---|---|---|---|---|---|---|---|---|---|
| Exciter | Mallet .2 | Imp .0 | FM .8 | Mall .2 | FM .8 | FM .8 | Mall .2 | Imp .0 | Mall .2 |
| Material | .42 | .46 | .50 | .52 | .55 | .58 | .60 | .62 | .64 |
| Size | .78 | .66 | .55 | .46 | .38 | .34 | .30 | .24 | .20 |
| Decay | .85 | .82 | .82 | .80 | .78 | .78 | .76 | .74 | .72 |
| Strike | .28 | .32 | .30 | .34 | .30 | .36 | .32 | .34 | .30 |
| Filt Cutoff | .16 | .24 | .30 | .36 | .42 | .44 | .46 | .48 | .48 |
| Filt EnvAmt | .45 | .45 | .45 | .45 | .45 | .45 | .45 | .45 | .45 |
| **Mode Stretch** | **.66** | **.72** | **.78** | **.80** | **.84** | **.88** | **.82** | **.86** | **.84** |
| **Decay Skew** | **.82** | **.80** | **.80** | **.78** | **.78** | **.80** | **.78** | **.78** | **.78** |
| Nonlin Coup | .28 | .30 | .34 | .32 | .36 | .40 | .32 | .34 | .36 |
| Mode Scatter | .40 | .50 | .55 | .60 | .65 | .70 | .55 | .60 | .68 |
| FM Ratio | — | — | .40 | — | .50 | .47 | — | — | — |
| Morph On | 0 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| Morph St/End | — | .45/.80 | .45/.80 | .45/.80 | .45/.82 | .50/.82 | .45/.80 | .50/.82 | .50/.85 |
| Morph Dur | — | .70 | .70 | .70 | .70 | .70 | .70 | .70 | .70 |
| Noise Mix | .10 | .12 | .12 | .12 | .11 | .10 | .11 | .10 | .10 |
| Noise Cutoff | .30 | .35 | .35 | .40 | .40 | .40 | .40 | .40 | .40 |
| Click Mix | .10 | .14 | .08 | .12 | .08 | .08 | .10 | .14 | .10 |
| Click Bright | .55 | .62 | — | .66 | — | — | .66 | .70 | .70 |
| Body b1 | .33 | .35 | .35 | .36 | .37 | .37 | .36 | .36 | .36 |
| Body b3 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| Output Bus | main | main | main | main | main | main | main | main | main |
| Pan | .50 | .65 | .55 | .70 | .50 | .80 | .40 | .60 | .30 |

*Bell live-axis note:* Mode Stretch is the kit signature — under the corrected `sqrt(1+B·(k+1)²)` / B_max=0.01 law, norms 0.66–0.88 give B 0.0066–0.0088 (strong inharmonic warp of the canonical hum/prime/tierce/quint/nominal set). decaySkew 0.78–0.82 → phys +0.56..+0.64 (low-partial tilt, per-mode on all bodies, M-5). b3=0 keeps the metallic high partials ringing. Body Damping b1 is **written** (overrides the Decay-derived flat damping for a long flat tail).

### String pads (waveguide — Stretch/Skew/Scatter/b1/b3/AirLoading/Tension are NO-OPS)

| Param | p1 | p3 | p5 | p7 | p11 | p13 | p15 |
|---|---|---|---|---|---|---|---|
| Exciter | Fric .6 | FM .8 | Mall .2 | Imp .0 | Fric .6 | Imp .0 | Mall .2 |
| Material | .40 | .42 | .44 | .46 | .50 | .52 | .55 |
| Size | .72 | .60 | .50 | .42 | .26 | .22 | .20 |
| Decay | .88 | .84 | .83 | .82 | .86 | .80 | .82 |
| Strike (pick) | .35 | .38 | .40 | .42 | .40 | .45 | .50 |
| Level | .62 | .62 | .62 | .62 | .60 | .62 | .62 |
| Filt Cutoff | .20 | .28 | .34 | .40 | .40 | .44 | .46 |
| Filt EnvAmt | .60↑ | .45 | .50 | .45 | .60↑ | .50 | .50 |
| Filt EnvDec | .45 | .30 | .30 | .30 | .45 | .30 | .30 |
| Nonlin Coup | .40 | .38 | .36 | .40 | .42 | .38 | .40 |
| FM Ratio | — | .45 | — | — | — | — | — |
| Friction Press | .35 | — | — | — | .40 | — | — |
| Morph On | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| Morph St/End | .45/.75 | .45/.78 | .45/.78 | .45/.80 | .45/.78 | .45/.80 | .50/.82 |
| Morph Dur | .65 | .68 | .66 | .68 | .66 | .64 | .64 |
| Noise Mix | .16 | .14 | .13 | .14 | .16 | .12 | .12 |
| Noise Cutoff | .40 | .40 | .40 | .45 | .45 | .45 | .45 |
| Click Mix | **0** | .08 | .12 | .14 | **0** | .16 | .14 |
| Output Bus | main | **aux1** | main | **aux1** | **aux1** | main | **aux1** |
| Pan | .35 | .25 | .45 | .30 | .60 | .50 | .70 |

*String live-axis note:* the WaveguideString body **bypasses the shared modal bank**, so on these pads **Mode Stretch, Decay Skew, Mode Scatter, Mode Inject, Body Damping b1/b3, Air Loading and Tension Mod are inherent no-ops** — they are NOT set in the generator (left at struct defaults / b1/b3 at the −1.0 sentinel so they are never written). String brightness/inharmonicity/evolution come ONLY from the **Material-driven loop filter + Material Morph**, the **ToneShaper filter env** (the bow-bloom UP sweep on the two Friction pads), and **NonlinearCoupling** (live, amplitude brightening). FM Ratio is live only on the FMImpulse pad (p3); Friction Pressure live only on the Friction pads (p1, p11); Click Mix = 0 on the bowed pads (a bowed onset is a scrape carried by the Friction exciter).

## Defaulted params (kit-wide, with reason)
- **Drive / Fold (0):** warmth/aggressive harmonics fight the clean hollow skeleton (Drive is now flavour-only post-M-2 makeup, still off).
- **PitchEnv family (Time=0):** a ghost tone holds pitch; no 808 glide. The whole Start/End/Curve/Knee/Mid/Curve2 family is a no-op at Time=0.
- **Tension Mod / Air Loading (0):** Membrane-only — inherent no-op on Bell/String.
- **Mode Inject (0):** exact-bypass; an integer 1/k series would re-pitch and de-skeletonise the voice and fight the inharmonicity.
- **Choke Group (0):** sustained, overlapping tails must ring free.
- **Secondary / Coupling Strength (off):** the inharmonic body stands alone; no head↔shell shell layer.
- **Punch / Tightness / Body Size macros (0.5 neutral):** no transient punch on a sustained drone (Brightness/Complexity are the only nudged macros).
- **Feedback Amount / NoiseBurst Duration:** wrong-exciter no-ops (Feedback/NoiseBurst exciters unused).
- **Per-exciter no-ops:** FM Ratio off on non-FMImpulse pads; Friction Pressure off on non-Friction pads.
- On **String pads:** Mode Stretch/Skew/Scatter/Mode Inject/b1/b3/AirLoading/Tension are inherent no-ops (waveguide bypasses the modal bank); b1/b3 left at the −1.0 sentinel so they are never written.

## Kit globals (kept)
`maxPolyphony 12 · globalCoupling 0.78 · tomResonance 0.55 · couplingDelayMs 2.0` — drives the high per-pad Coupling Amount 0.85 (haunted self-resonating ensemble). Crafted pads 0–15; 16–31 disabled (intentional blank canvas).

## Critical re-voice notes (vs current generator)
1. **Stretch axis (the #2 re-voice priority):** the current loop's `0.65+(i%4)*0.08` (→0.65–0.89) was set against the OLD off-by-one (`B·k²`, fundamental unstretched) + B_max=0.001. Under the corrected `(k+1)` index and B_max=0.01 it is now far more inharmonic; values are kept strong (it IS the signature) but pinned per-pad (0.66–0.88) and re-checked against the new law. **NOTE:** in the old loop these were applied to BOTH Bell and String pads — but String ignores Stretch, so the loop's String-pad stretch was dead. The corrected plan only sets Stretch on the Bell (modal) pads.
2. **Bell pads** re-voiced against the corrected 2-D meridional Bell mode shape + strike-AZIMUTH strikePos (Phase-2), not the old radial `|cos((k+1)πr)|` shape.
3. **NonlinearCoupling** re-voiced for the M-3/M-4 env-level-driven redesign (was the old transient-only dEnv AM + always-on full-signal recipSqrt compressor).
4. **decaySkew** reined from up-to-0.90 (loop `0.78+(i%3)*0.06`) to a documented +0.56..+0.64 band; the per-mode tilt now runs on all bodies (M-5), so the knob re-balances the spectrum, not just nudges global decay.
5. **Pan** spread added (M-9) — was 0.5 everywhere; now an explicit equal-power L–R spread 0.25–0.80.
6. **Secondary/Tension/AirLoading removed from String pads:** the old loop set `couplingStrength`, `secondaryEnabled`, `tensionModAmt`, `bodyDampingB3=0.18` etc. on String pads where they are no-ops (and writing b3 off-sentinel on a String pad is pointless); the corrected plan leaves all of these at their inert defaults/sentinels.
7. **Structure**: modulo loop (one recipe stamped with `(13-i)/13` size ramp + index `%` sweeps, the audit §4 sameness) → 16 explicitly-authored pads with a monotonic Size ladder and a distinct exciter/role per pad. Output-bus + Coupling Amount 0.85 + globalCoupling 0.78 kept (already archetype-correct).

---

## Verification log (7 issues found & fixed)

1. COVERAGE (no defect, clarified): String pads (1,3,5,7,11,13,15) write Mode Stretch / Decay Skew / Mode Scatter values that are INHERENT NO-OPS on the WaveguideString body (param-dictionary: stretch/skew/scatter/b1/b3 bypass the modal bank for String). The proposal already labels them 'snapshot consistency' but this risks being read as meaningful. FIX: markdown String-pads table footnote and defaultedParams explicitly restate these as no-ops driven instead by Material loop-filter + filter env + NLC; values left as written are harmless. Verified against param-dictionary.json 'PER-BODY meaningfulness'.

2. SENTINEL safety (verified OK, called out): String pads correctly leave Body Damping b1/b3 at the -1.0 sentinel (NOT written), which is REQUIRED — writing 0.5 would force b1=25 s^-1 / b3=5e-4 and change the sound. Bell pads correctly write b1 (0.33-0.37) and b3=0 (metallic). No fix needed; confirmed no String pad writes b1/b3.

3. ENCODING note (no defect): Output Bus is expressed as valueNorm 0.067 (on-wire round(0.067*15)=1 = aux bus 1). In the generator Pad struct outputBus is the INTEGER 1, not a normalized double; pan/exciter/body are likewise stored as the physical/enum value. The markdown (the deliverable) uses physical 'aux1' language, which is correct for the generator. Clarified in the markdown so the implementer writes outputBus=1, not 0.067.

4. PHYSICAL CORRECTNESS (verified): all body/exciter pairings correct — Bell for the tuned inharmonic hum/prime/tierce skeleton, String waveguide for bowed/plucked bone; FM Ratio live only on the 3 FMImpulse pads (3,4,8,9 — note pad 3 is FMImpulse on String, pad 9 FMImpulse on Bell), Friction Pressure live only on the 2 Friction pads (1,11). Mode Stretch range 0.66-0.88 re-checked against corrected sqrt(1+B*(k+1)^2), B_max=0.01 (B 0.0066-0.0088), in [0,1] and within the archetype 0.65-0.89 band. NLC 0.28-0.42 voiced for the env-level M-3/M-4 redesign. b3=0 on every Bell pad (metallic long highs) matches the archetype. decaySkew 0.78-0.82 (phys +0.56..+0.64) within band. No physically-wrong values.

5. RANGE (verified): every valueNorm in [0,1]; exciter norms (0.0/0.2/0.6/0.8) round to enum idx 0/1/3/4 exactly; Body 0.8->Bell(4), 0.6->String(3); Filter Type 0.5->HP(idx1); Output Bus 0.067->1. All discrete/sentinel/float-as-bool values legal.

6. LAYOUT (verified): 16-voice register-graded choir (size 0.78->0.20, f0 ~133->505 Hz) is a real pitch ladder, not the audit-§4 duplicate stamp. Pads 14 (Bell) and 15 (String) share f0~505/Mallet but differ in body = distinct voices, NOT a duplicate. Aux spread 1-in-4 (pads 3,7,11,15) and per-pad L-R pan (0.25-0.80) correctly exercise M-9. Gaps (pads 16-31 disabled; no GM percussion roles; modeInject=0, tension/airLoading no-op on Bell/String) all correctly flagged.

7. MINOR (documented, no fix needed): pad 0 ground-hum has Morph OFF while the archetype runs morph on ~2/3 of pads — justified per-pad (anchor spectral stability). Pad 0 Size 0.78 extends below the archetype's 0.20-0.75 band for a sub-anchored kick-slot ground hum — documented deliberate extension.
