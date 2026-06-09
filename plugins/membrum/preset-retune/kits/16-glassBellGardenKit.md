<!-- verdict: pass-with-fixes | coverageOk: true | issues fixed: 6 | IMPLEMENTED: 2026-06-09 (commit re-tune Glass Bell Garden) | NOTE: morph durations (seconds in plan) approximated to normalized morphDuration field (~1.7s->0.85, 1.4s->0.75, 1.1s->0.65, 0.4s->0.30). -->

# Glass Bell Garden — Corrected Re-Design (Membrum Unnatural kit)

`glassBellGardenKit()` — voiced against post-audit DSP (2026-06-07): N-1 measured-strike body norm, corrected (k+1) Mode Stretch with widened B_max=0.01, env-level NonlinearCoupling, L-3 FM index, 2-D meridional Bell strike shape, per-pad Pan, per-mode Decay Skew on all bodies, Mode Inject 1/k.

> VERIFICATION PASS (adversarial): values cross-checked against archetypes/*.md, param-dictionary.json, AUDIT-signal-path-2026-06-07.md, and the IMPLEMENTATION TARGET tools/membrum_preset_generator.cpp. Corrections applied are listed in "Verification corrections" at the end.

## What this kit is
A bright, glassy, inharmonic-but-tuned **all-Bell** garden. Every pad uses the Bell body (the only tuned-metal long-ring resonator), but variety comes from **seven distinct sub-role archetypes** rather than one i-indexed ramp. The kit fills all **32 pads** (was 16 crafted + 16 dead) and deliberately exercises the full param surface — Pan spread, Mode Inject, full Mode-Stretch range, Decay Skew everywhere, head↔shell coupling, Material Morph bloom, aux Output Bus sends, and Choke-group cascade.

## Generator field semantics (so the plan converts directly to edits)
In `tools/membrum_preset_generator.cpp` the `Pad` struct stores most params as the **normalized [0,1]** value verbatim (e.g. `modeStretch=0.42`, `decaySkew=0.60`), and a few as discrete sentinels:
- `chokeGroup` and `outputBus` are **raw integers** (written as uint8). Set `chokeGroup = 1`, `outputBus = 1` directly — do NOT pass the on-wire norm (norm 1.0 would decode to bus 15 via the VST `int(norm*15+0.5)` path; norm 0.125 -> choke group 1 is the on-wire equivalent of the integer 1).
- `tsFilterType` uses the `FilterType` constants `LP=0.0, HP=0.5, BP=1.0` (the ghost-glass HP corner is `tsFilterType = FilterType::HP = 0.5`).
- `secondaryEnabled`, `morphEnabled`, `morphCurve` are float-as-bool (0.0/1.0).

## Kit-level options
- `maxPolyphony` 24 (raised from 16 — the long sustained tails + 32 pads need more voices)
- `globalCoupling` 0.65 (sympathetic garden ring; unchanged)
- `couplingDelayMs` 1.8
- `crafted` = all 32 pads (was 0–15)

## Pad map (sub-roles)
| Pads | Role | Body/Exciter | Identity |
|---|---|---|---|
| 0–5 | Struck glass bells (register row, low→high) | Bell / Mallet | clean glass taps, Mode Stretch ~1.13, coupled shell on 2 & 5 |
| 6–11 | FM-glass pings (low→high) | Bell / FMImpulse | fmRatio 0.30→0.62 graded inharmonic chime, faint Mode Inject 1/k on low pings |
| 12–13 | Bowed singing-glass | Bell / Friction | decay 0.95, decaySkew 0.85, Morph settle, aux bus |
| 14 | Glass gong (bloom) | Bell / Mallet | size 0.85 (~113 Hz), NLC 0.8 bloom, heavy shell coupling, aux |
| 15 | FM-glass shatter-crash | Bell / FMImpulse | Mode Stretch 1.325 cymbal dispersion, violet sheen, aux |
| 16–20 | Crotale-glass tines (high) | Bell / Mallet | Mode Stretch NEUTRAL (harmonic octave), Mode Inject 1/k reinforcement |
| 21 | Temple-glass bell | Bell / Mallet | warm low hum, decaySkew 0.78, low b1/b3 |
| 22–25 | Ghost-glass sustains | Bell / Mallet·FM·Friction | Mode Stretch 0.66→0.77 skeletal, HP filter, Morph, aux |
| 26 | Glass-harmonica drone | Bell / Friction | rubbed glass, decay 0.95, aux |
| 27–31 | Bell-tree glissando cascade | Bell / NoiseBurst | bright detuned bowls, Choke group 1, L→R pan, aux |

## Per-pad exact normalized values (meaningful params)
> All values are on-wire NORMALIZED [0,1] (and equal the generator field value), except `chokeGroup`/`outputBus` which are integers. Bell `f0_nominal = 800·0.1^Size`. Params not listed are at documented defaults (see per-pad "defaulted" reasons in the structured data). Stretch norm 0.333 = physical-neutral 1.0; 0.42→1.13, 0.55→1.325, 0.62→1.43, 0.66→1.49, 0.77→1.655. DecaySkew norm 0.5 = 0.

### Struck glass bells (0–5, Mallet)
| Pad | Mat | Size | Decay | Stretch | Skew | Scatter | b1 | b3 | Click | NoiseMix | CplAmt | Pan | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0.55 | 0.62 | 0.86 | 0.42 | 0.60 | 0.46 | 0.30 | 0.0 | 0.32 | 0.13 | 0.55 | 0.18 | f0~192Hz |
| 1 | 0.62 | 0.54 | 0.84 | 0.43 | 0.62 | 0.48 | 0.30 | 0.0 | 0.34 | 0.13 | 0.60 | 0.30 | f0~232Hz |
| 2 | 0.68 | 0.46 | 0.82 | 0.44 | 0.64 | 0.50 | 0.30 | 0.0 | 0.36 | 0.13 | 0.65 | 0.42 | +shell (cplStr 0.25, secEn 1, secSize 0.38, sec mat 0.85) |
| 3 | 0.75 | 0.38 | 0.80 | 0.45 | 0.66 | 0.52 | 0.30 | 0.0 | 0.38 | 0.12 | 0.70 | 0.58 | f0~333Hz |
| 4 | 0.82 | 0.30 | 0.78 | 0.46 | 0.68 | 0.54 | 0.30 | 0.0 | 0.40 | 0.12 | 0.70 | 0.70 | f0~401Hz |
| 5 | 0.90 | 0.22 | 0.76 | 0.47 | 0.70 | 0.56 | 0.30 | 0.0 | 0.42 | 0.12 | 0.75 | 0.82 | +shell (cplStr 0.25, secEn 1, secSize 0.42, sec mat 0.85) |

Struck row also: Exciter 0.20 (Mallet), Body 0.80 (Bell), Strike Pos 0.30→0.22 (descending), Level 0.70→0.66, Click Brightness 0.72→0.88 (ascending). Citations: glass-bell-algorithmic (material 0.40→0.95 ramp, size reversed 0.10→0.70, decay 0.55→0.90, decaySkew 0.55→0.79, scatter 0.30→0.62, b3=0, couplingAmount 0.55→0.85, secondary on i%3==0 with secMaterial 0.85).

### FM-glass pings (6–11, FMImpulse)
| Pad | Mat | Size | Decay | FMRatio | Stretch | Skew | ModeInj | NLC | b1 | CplAmt | Pan |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 6 | 0.70 | 0.42 | 0.55 | 0.30 | 0.42 | 0.58 | 0.12 | 0.12 | 0.30 | 0.55 | 0.26 |
| 7 | 0.73 | 0.37 | 0.55 | 0.38 | 0.43 | 0.60 | 0.12 | 0.16 | 0.30 | 0.60 | 0.38 |
| 8 | 0.76 | 0.32 | 0.52 | 0.46 | 0.44 | 0.62 | 0.10 | 0.20 | 0.30 | 0.65 | 0.50 |
| 9 | 0.80 | 0.27 | 0.50 | 0.54 | 0.45 | 0.64 | 0.10 | 0.24 | 0.30 | 0.70 | 0.62 |
| 10 | 0.85 | 0.22 | 0.48 | 0.62 | 0.46 | 0.66 | 0.08 | 0.28 | 0.30 | 0.70 | 0.74 |
| 11 | 0.90 | 0.16 | 0.46 | 0.60 | 0.47 | 0.68 | — | 0.30 | 0.30 | 0.75 | 0.86 (+shell: cplStr 0.25, secEn 1, secSize 0.46, sec mat 0.85) |

All FM pings: Exciter 0.80 (FMImpulse), Body 0.80, Strike Pos 0.30, Level 0.72→0.68, b3 0.0, Click 0.15 / bright 0.70, Noise 0.0. FM Ratio denorm = 1.0+3.0·norm (0.30→1.9, 0.62→2.86). NOTE (verified): Mode Inject 1/k is kept FAINT here (a synthetic tonal core under the chime); because these bodies carry glass Mode Stretch (~1.13–1.21), the injected integer series is secondary/subtle — the physically-clean injection case is the neutral-stretch crotale row (16–20). Citations: fm-bell-percussion (FMImpulse, fmRatio 0.40→2.2, b3=0, click 0.15/0.70, noise 0), glass-bell (stretch/skew/scatter/NLC 0.0→0.30 ramp), AUDIT (mode_inject now 1/k, L-3 FM index restored).

### Sustained voices (12–13 bowed glass, 14 gong, 15 crash, 26 glass-harmonica)
| Pad | Role | Exc | Mat | Size | Decay | Stretch | Skew | FM/Fric/NLC | Morph | Bus | Pan |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 12 | singing-glass lo | Friction | 0.78 | 0.55 | 0.95 | 0.45 | 0.85 | Fric 0.28 / NLC 0.22 | 0.78→0.55 exp 1.7s | 1 | 0.34 |
| 13 | singing-glass hi | Friction | 0.85 | 0.42 | 0.95 | 0.47 | 0.82 | Fric 0.32 / NLC 0.22 | 0.85→0.60 exp 1.7s | 1 | 0.66 |
| 14 | glass gong | Mallet | 0.60 | 0.85 | 0.95 | 0.62 | 0.65 | NLC 0.80 (bloom) | 0.85→0.55 lin 0.4s | 1 | 0.50 |
| 15 | shatter-crash | FMImpulse | 0.92 | 0.45 | 0.78 | 0.55 | 0.60 | FM 0.45 / NLC 0.45 | static | 1 | 0.50 |
| 26 | glass-harmonica | Friction | 0.80 | 0.35 | 0.95 | 0.43 | 0.83 | Fric 0.26 / NLC 0.22 | 0.80→0.60 exp 1.7s | 1 | 0.50 |

Singing-glass 12/13 + glass-harmonica 26: Strike Pos 0.20 (rim), Level 0.60–0.62, Click 0.0 (bowed, no onset), Noise 0.10 pink, scatter 0.06–0.10, b1 0.30 b3 0.0, Macro Complexity 0.85, Morph Curve exponential. Gong 14: Strike Pos 0.35, Coupling Strength 0.95, Secondary on, secSize 0.40, sec mat 0.85, Noise 0.30 violet cutoff 0.85, Click 0.20, scatter 0.47, b1 0.28 b3 0.0, Coupling Amount 0.85, Morph Curve 0.5 (linear). Crash 15: Strike Pos 0.32, scatter 0.65, Noise 0.40 violet cutoff 0.92 decay 0.72 reso 0.20, Click 0.35 bright 0.85 contact 0.30, b1 0.30 b3 0.0, Coupling Amount 0.75. Citations: singing-bowl (decay 0.95, decaySkew 0.85, scatter 0.06, frictionPressure 0.28, NLC 0.22, morph 0.78→0.55 exp 1.7s, click 0, noise 0.10 pink, bus 1), gong-tam-tam (size 0.85→113Hz, stretch 1.43, NLC 0.80 bloom, coupling 0.95, morph 0.85→0.55 lin 0.4s, violet noise wash, bus 1), fm-bell-crash (material 0.92, size 0.45, stretch 0.55, scatter 0.65, NLC 0.45, noise 0.40/0.92/0.72, click 0.35/0.85/0.30, b1 0.30 b3 0).

### Crotale-glass tines (16–20) + temple bell (21), Mallet
| Pad | Mat | Size | Decay | Stretch | Skew | ModeInj | b1 | b3 | Click | ClickContact | ClickBright | Pan |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 16 | 0.92 | 0.12 | 0.85 | **0.333** | 0.58 | 0.16 | 0.30 | 0.0 | 0.42 | 0.20 | 0.85 | 0.22 |
| 17 | 0.92 | 0.16 | 0.84 | **0.333** | 0.58 | 0.16 | 0.30 | 0.0 | 0.42 | 0.20 | 0.85 | 0.36 |
| 18 | 0.92 | 0.20 | 0.83 | **0.333** | 0.58 | 0.14 | 0.30 | 0.0 | 0.42 | 0.20 | 0.85 | 0.50 |
| 19 | 0.90 | 0.22 | 0.82 | **0.333** | 0.58 | 0.14 | 0.30 | 0.0 | 0.42 | 0.20 | 0.85 | 0.64 |
| 20 | 0.95 | 0.08 | 0.80 | **0.333** | 0.58 | 0.12 | 0.30 | 0.0 | 0.42 | 0.20 | 0.88 | 0.78 |
| 21 | 0.82 | 0.50 | 0.92 | 0.40 | 0.78 | — | 0.18 | 0.06 | 0.28 | 0.45 | 0.42 | 0.50 |

> FIX APPLIED: Click Contact 0.20 added to crotale pads 17–20 (previously silently defaulted to 0.30; the crotales archetype specifies 0.20 = 2.6 ms hard-beater contact, matching pad 16).

Crotales 16–20: Exciter 0.20, Body 0.80, Strike Pos 0.10 (antinode), Level 0.72→0.70, Noise 0.0, scatter 0.08, Mode Inject 1/k REINFORCES the near-harmonic octave (only musical because Stretch is NEUTRAL here). Temple bell 21: Strike Pos 0.18, Level 0.78, scatter 0.06, Noise 0.04, Click contact 0.45 / bright 0.42, b1 0.18 b3 0.06, Mode Inject 0 (would contradict the tuned hum). Citations: crotales (material 0.92, size 0.12 HI/0.22 LO, decay 0.85, stretch 0.333 neutral octave, skew 0.58, b1 0.30 b3 0, click 0.42/contact 0.20/bright 0.85, noise 0), kalimba (scatter 0.08 hand-tuned), indian-temple-bell (material 0.82, size 0.50→253Hz, decay 0.92, stretch 0.40, skew 0.78, b1 0.18 b3 0.06, click 0.28/0.45/0.42, noise 0.04), AUDIT (mode_inject 1/k musical on near-harmonic body).

### Ghost-glass sustains (22–25, HP filter + Morph + aux)
| Pad | Exc | Mat | Size | Decay | Stretch | Skew | HP Cutoff | NLC | Morph | Pan |
|---|---|---|---|---|---|---|---|---|---|---|
| 22 | Mallet | 0.45 | 0.55 | 0.80 | 0.66 | 0.78 | 0.22 | 0.32 | 0.45→0.80 lin 1.4s | 0.14 |
| 23 | FMImpulse | 0.50 | 0.45 | 0.80 | 0.71 | 0.81 | 0.24 | 0.32 | 0.45→0.80 lin 1.4s | 0.38 |
| 24 | Mallet | 0.55 | 0.30 | 0.80 | 0.77 | 0.81 | 0.30 | 0.32 | 0.45→0.80 lin 1.4s | 0.62 |
| 25 | Friction | 0.50 | 0.50 | 0.82 | 0.74 | 0.81 | 0.26 | 0.32 | 0.45→0.80 lin 1.4s | 0.86 |

Ghost 22–25: Body 0.80, Strike Pos 0.30, Level 0.63–0.65, Filter Type HP (`tsFilterType=0.5`), reso 0.30, env amt 0.45; Noise 0.12 brown cutoff 0.35 decay 0.85; Click 0.12 (0.0 on bowed pad 25); b1 0.35 b3 0.0; CplAmt 0.85; Output Bus 1; scatter 0.45; Mode Inject 0 (no harmonic series under the skeletal cluster). Pad 23 FM Ratio 0.40 (c:m 2.2); pad 25 Friction Pressure 0.30. Citation: ghost-tone (stretch 0.65–0.89, skew 0.78+, HP filter cutoff ~0.22, NLC 0.32, morph 0.45→0.80 lin 1.4s, brown noise 0.12, b1 0.35 b3 0, coupling 0.85, aux bus).

### Bell-tree glissando cascade (27–31, NoiseBurst, Choke group 1, aux)
| Pad | Mat | Size | Decay | Stretch | Skew | Scatter | NoiseMix | Click | ChokeGroup(int) | OutputBus(int) | Pan |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 27 | 0.95 | 0.34 | 0.85 | 0.55 | 0.78 | 0.55 | 0.42 | 0.55 | 1 | 1 | 0.10 |
| 28 | 0.95 | 0.28 | 0.84 | 0.55 | 0.78 | 0.55 | 0.42 | 0.55 | 1 | 1 | 0.30 |
| 29 | 0.95 | 0.22 | 0.83 | 0.55 | 0.78 | 0.55 | 0.42 | 0.55 | 1 | 1 | 0.50 |
| 30 | 0.95 | 0.16 | 0.82 | 0.55 | 0.78 | 0.55 | 0.42 | 0.55 | 1 | 1 | 0.70 |
| 31 | 0.95 | 0.10 | 0.80 | 0.55 | 0.78 | 0.55 | 0.42 | 0.55 | 1 | 1 | 0.90 |

Bell-tree: Exciter 0.40 (NoiseBurst), Body 0.80, Strike Pos 0.30, Level 0.66–0.70, NoiseBurst Duration 0.45, Noise violet cutoff 0.92 decay 0.85, Click bright 0.92, b1 0.30 b3 0.0, Morph 0.85→0.55 lin 1.1s. `chokeGroup = 1` and `outputBus = 1` are set as integer fields in the generator (the param-dictionary on-wire equivalents are norm 0.125 -> group 1 and the aux-send index 1). Citation: bell-tree (NoiseBurst, material 0.95, size 0.30→401Hz base graded, decay 0.85, stretch 0.55, skew 0.78, scatter 0.55, noise 0.42/0.92/0.85, click 0.55/0.92, b1 0.30 b3 0, morph 0.85→0.55 lin 1.1s; choke + aux for cascade).

## Full-surface usage checklist (kit-collective)
- **Mode Stretch**: full range — 0.333 neutral (crotales 16–20) → 0.42 glass (struck/FM) → 0.55 (crash/bell-tree) → 0.62 (gong) → 0.66–0.77 skeletal (ghost). ✔
- **Decay Skew**: non-neutral on every pad (0.58 crotale → 0.85 singing-glass). ✔
- **Mode Inject (1/k)**: clean reinforcement on neutral-stretch crotales 16–20; faint tonal core on FM pings 6–10 (secondary on the stretched body). ✔
- **Exciters**: Mallet, FMImpulse (full fmRatio 0.30→0.62 range), Friction, NoiseBurst — all by role. ✔
- **NonlinearCoupling**: bloom on gong (0.80) + crash (0.45) + faint on FM/ghost/singing-glass. ✔
- **Material Morph**: gong, singing-glass, ghost, bell-tree, glass-harmonica (crash deliberately static). ✔
- **Head↔shell Coupling + metallic Secondary 0.85**: pads 2, 5, 11, 14. ✔
- **ToneShaper HP filter** (`tsFilterType=0.5`): ghost-glass 22–25. ✔
- **Pan**: spread across full field per role cluster. ✔
- **Output Bus 1** (integer field): all long sustained tails (12–15, 22–26, 27–31). ✔
- **Choke group 1** (integer field): bell-tree cascade 27–31. ✔
- **Deliberately OFF kit-wide**: Drive/Fold (clean glass), PitchEnv (rigid metal, no glide), Tension Mod + Air Loading (Membrane-only no-ops on Bell — documented per pad).

## Gaps / duplications flagged
- Original kit: ONE ramped Bell recipe across 16 pads (audit §4 #4 sameness) → resolved by 7 sub-roles.
- Original kit: 16 dead pads → all 32 now crafted.
- Original kit: no high crotale/clear-pitch role, no sustained role, Mode Inject 0, Mode Stretch capped 0.72, Pan 0.5 everywhere → all filled/used.
- By design: NO membrane drum roles (kick/snare/hat/tom) — this is a pitched-idiophone garden in the Unnatural category, not a GM drum kit. tensionMod/airLoading remain inert (Membrane-only) on every Bell pad.

## Verification corrections (this pass)
1. **Click Contact 0.20 added to crotale pads 17–20** — they previously omitted it and would default to 0.30; the crotales archetype specifies 0.20 (pad 16 already set it). Closes the one real coverage gap.
2. **chokeGroup / outputBus stated as integer generator fields** (`= 1`) with a note distinguishing them from the VST on-wire norm decode (norm 1.0 -> bus 15 trap avoided). No bus/group value changed (group 1 / bus 1 throughout).
3. **Sharpened the FM-ping Mode-Inject rationale** to mark it as a faint/secondary tonal core on the stretched body (vs. the clean neutral-stretch crotale reinforcement). No value changed.
All other values verified consistent with the cited archetypes and post-audit semantics; no physics, range, or layout errors found.

## Citations
glass-bell-algorithmic, singing-bowl, crotales, indian-temple-bell, fm-bell-percussion, fm-bell-crash, gong-tam-tam, ghost-tone, bell-tree, kalimba archetypes; param-dictionary.json (offsets/denorm); AUDIT-signal-path-2026-06-07.md (corrected semantics: N-1 body norm, (k+1) stretch, 1/k mode_inject, env-level NonlinearCoupling, L-3 FM index, M-9 per-pad pan, M-5 per-mode decaySkew); tools/membrum_preset_generator.cpp (glassBellGardenKit lines 3243-3298; Pad struct field semantics: chokeGroup/outputBus int, FilterType::HP=0.5).

---

## Verification log (6 issues found & fixed)

1. COVERAGE (minor): Crotale-glass pads 17-20 silently default Click Contact. The cited crotales recipe sets Click Contact 0.20 (=2.6 ms hard-beater contact); pad 16 sets it but 17-20 omit it, so they fall to PadConfig default 0.30 (~3.5 ms, softer). FIX: added Click Contact 0.20 to pads 17-20 (matching pad 16 and the crotales archetype).

2. PLAN CONSISTENCY (not a DSP defect): The plan expresses Choke Group as on-wire norm 0.125 but Output Bus as the literal field value 1. Verified against the IMPLEMENTATION TARGET tools/membrum_preset_generator.cpp: the Pad struct stores chokeGroup and outputBus as RAW INTEGERS (int chokeGroup=0, outputBus=0; written as uint8), NOT normalized values. So in the generator both are set as ints: chokeGroup=1, outputBus=1. The param-dictionary norm*8 / norm*15 decode (norm 0.125 -> group 1; bus norm would need ~0.067 -> bus 1) is the VST on-wire path, not the generator path. Both resolve to group 1 / bus 1, so no functional error, but FIX: harmonized the markdown to state these as generator integer fields (chokeGroup=1, outputBus=1) and added a clarifying note so the plan converts directly to generator edits without the norm*15 trap (norm 1.0 would wrongly decode to bus 15 on-wire).

3. PHYSICS (verified, sharpened not changed): Mode Inject 1/k (0.08-0.16) is applied on the FM-glass pings (6-10) whose bodies carry Mode Stretch 0.42-0.47 (phys ~1.13-1.21, glass-inharmonic). An injected INTEGER series under a stretched/inharmonic body is mildly contradictory (cf. fm-bell-percussion recipe which keeps Mode Inject 0 on a neutral-stretch bell). It is kept because it is FAINT (a 'tonal core' under the chime) and is the deliberate exercise of the previously-dead axis; the crotale row (16-20), where Mode Stretch is NEUTRAL 0.333, is the physically-clean injection case. FIX: tightened the rationale to flag the faint/secondary nature on the stretched FM pings vs. the clean reinforcement on the neutral crotales. No value change.

4. PHYSICS (verified correct, no change): Spot-checked all post-audit semantics. Mode Stretch norm->phys = 0.5+norm*1.5 verified: crotale 0.333->1.0 neutral, gong 0.62->1.43 (recipe says ~1.43), crash 0.55->1.325 (recipe 1.325), ghost 0.77->1.655 (recipe 1.66). Filter Type 0.5 = HP confirmed (FilterType::HP=0.5 in generator; on-wire int(0.5*3)=1=Highpass) for ghost-glass 22-25. Choke 0.125 on-wire -> group 1 confirmed. All exciters legal (Mallet 0.2/NoiseBurst 0.4/Friction 0.6/FMImpulse 0.8 -> enum idx 1/2/3/4). airLoading/tensionMod left inert (Membrane-only) on every Bell pad, documented. NonlinearCoupling used as amplitude-brightening (gong 0.80, crash 0.45), Drive/Fold off (clean glass). All correct.

5. RANGES (verified): every valueNorm in [0,1]; float-as-bool (Secondary Enabled, Morph Enabled, Morph Curve) and sentinel/discrete (Filter Type, Output Bus, Choke Group) set to legal values; b3=0 (pure-metal, legal sentinel-override to 0). No out-of-range values found.

6. LAYOUT (verified): 32/32 pads crafted (was 16 + 16 dead); 7 sub-role archetype families correctly deployed; gaps (no high crotale role, no sustained role, Mode Inject 0, Stretch capped 0.72, Pan 0.5) correctly flagged and filled. NON-GAP correctly noted: no GM membrane drums (kick/snare/hat/tom) by design (pitched-idiophone Unnatural kit). No duplication beyond the intentional within-role pitch grading. Body model = Bell on all 32 is correct (only tuned long-ring metal/glass resonator).
