# D4 — Morph laws: what the user sees while morphing

**Status: RATIFIED by the user, 2026-10-23 (spec Clarifications "Session 2026-10-23 (12a ratification)").**

Serves FR-007 (spec.md:317–324); plan §3.2. This document restates Phase 8's per-parameter morph laws in user
words. It adds no behaviour: every statement cites the header line it restates (read at `40f6ebca`).

Controls named here:

```
 Main page                         Advanced page, section "Morph behaviour"
 +------------------------------+  +-----------------------------------------------+
 | Morph Position ......... 105 |  | Anchor Morph Mode [Hold|Blend|Yield] ... 1000 |
 +------------------------------+  | Morph smoothing (Morph Ramp Time) ...... 1002 |
                                   +-----------------------------------------------+
```

(105: `plugin_ids.h:116`; 1000, 1002: `plugin_ids.h:202–204`; the "Morph behaviour" section holds 1000 and 1002,
plan.md:1365.)

---

## 1. One position, two endpoints, sixty-two fixed laws

- A patch is two complete sounds, **A** and **B**. Morph Position (105) chooses a point between them, 0 % = A,
  100 % = B. The engine resolves one blended sound every 32 samples (`bass_morph_engine.h:43–45`, `:62`).
- Every sound control (the 62 "leaves" of a patch, `morph_state.h:132–133`) has **one** law, fixed by Phase 8,
  that decides how it travels from A to B (`morph_state.h:139–142`; the engine reads the same table,
  `bass_morph_engine.h:74`).
- At exactly 0 % the sound **is** A, and at exactly 100 % it **is** B — no arithmetic, no rounding drift. If A
  and B hold the same value for a control, that control does not move at all (`bass_morph_engine.h:465–469`).
- Between the endpoints a blended value never leaves the range spanned by A's and B's values
  (`bass_morph_engine.h:483`).
- **There is no user-selectable global curve.** The per-parameter laws are fixed by Phase 8's ruling; the roadmap
  records a global "parameter curve" as *not adopted* (`specs/Profundum-roadmap.md:889–890`; spec FR-007,
  spec.md:324).

## 2. The four laws, in user words

| Law | What you hear while moving the position | Formula (t = position) | Example at 50 % | Source |
|---|---|---|---|---|
| **Linear** | The control moves in a straight line: halfway is the midpoint. Used for amounts, levels, tones, gains, curves. | A + t (B − A) | Harmonic Fold A = 0, B = 100 → 50 | `bass_morph_engine.h:474–475` |
| **Logarithmic** | Times and frequencies move evenly **in ratio** (by octaves), so the musically even middle is where you expect it. | 2^(log2(A + o) + t (log2(B + o) − log2(A + o))) − o | Drop Time A = 10 ms, B = 1000 ms → 100 ms | `bass_morph_engine.h:479–481`; cache `:430–442` |
| **Equal-power** | Two things cross-fade without a loudness dip in the middle. Used for the two blend controls: Excitation Mix (206) and Anchor Fold Feed (306). | √((1 − t) A² + t B²) | A = 0, B = 1 → 0.707 (not 0.5) | `bass_morph_engine.h:476–478`; rows `morph_state.h:173–174`, `:314–315` |
| **Stepped** | Discrete settings (Character algorithm, image mode, waveforms, sources, sync, polarity …) never blend: the whole setting switches from A's choice to B's choice at one point (§3). | the selected endpoint's value | — | `bass_morph_engine.h:462–463` |

`o` is the log offset: 1 ms for Motion Cycle Fade-In (910) only, so a fade-in of 0 ms can still morph on a log
scale (`morph_state.h:66`, `:275–276`); every other row has o = 0. Example: Fade-In A = 0 ms, B = 5000 ms → at 50 %,
√(1 × 5001) − 1 ≈ 69.7 ms.

Count by law (from the table in §6): 33 Linear, 11 Logarithmic, 2 Equal-power, 16 Stepped. The 11 logarithmic rows
match the "11 std::log2 calls" of `bass_morph_engine.h:111`; the 16 stepped rows are the 13 enums and 3 switches of
`morph_state.h:132`.

## 3. Stepped switching and the 0.52 / 0.48 hysteresis

Stepped controls switch **as a group**, never one by one (`morph_state.h:48`; the rows' `.group` field):

| Group | Stepped controls (IDs) | Governing amount (ID) | Position it follows |
|---|---|---|---|
| Character | Character 600 | Character Amount 601 | Morph position p |
| Image mode | Image Mode 800 | Spread 801 | p |
| Field motion | Field Motion Source 807, Field Sync 808, Field Sync Rate 809 | Field Motion Depth 805 | p |
| Motion | Motion Source 900, Motion Target 901, Cycle Shape 905, Cycle Sync 906, Cycle Sync Rate 907, Cycle Retrigger 909, Motion Polarity 911 | Motion Amount 902 | p |
| Anchor | Anchor Waveform 300, Anchor Mode 301 | Anchor Level 302 | Anchor position p_a (§4) |

(Governing amounts: `bass_morph_engine.h:375–387`; Anchor uses p_a: `:416–422`.)

The switching rule of one group (`bass_morph_engine.h:389–414`):

1. **At the ends:** at 0 % the group is A's choice, at 100 % it is B's (`:395–400`).
2. **A silent side yields:** if one endpoint's governing amount is 0 (that side's setting cannot be heard — e.g.
   A's Character Amount is 0) and the other's is not, the audible side's choice is used across the whole interior
   (`:401–406`). The switch then happens where it is inaudible.
3. **Hysteresis:** otherwise, moving toward B the group switches to B when the position reaches **52 %**
   (`kThetaUp = 0.52f`, `bass_morph_engine.h:70`); moving back it returns to A only when the position falls to
   **48 %** (`kThetaDown = 0.48f`, `:71`). Between 48 % and 52 % the group keeps its current choice (`:407–413`), so
   parking the knob near the middle, or automation jitter there, never chatters between two characters.

```
 selection of a        B .........................+-----------------
 stepped group                                ^   |
                                              |   v  (back to A at 48 %)
                       A ---------------------+---+
                                         (to B at 52 %)
                       0 %               48 % 52 %               100 %
```

The switch follows the **smoothed** position (§5), not the knob: it happens when the ramped p crosses 52 % / 48 %
(`bass_morph_engine.h:212`, `:218`).

## 4. The Anchor follows its own curve (Hold / Blend / Yield)

The Anchor block — Anchor Waveform 300, Anchor Mode 301, Anchor Level 302, Punch Boost 303, Punch Time 304, Ghost
Time 305, Anchor Fold Feed 306 — resolves at its own position p_a instead of p (`morph_state.h:296–315`,
`.anchorBlock = true`; `bass_morph_engine.h:458–461`). p_a moves toward c(p), where c is set by Anchor Morph Mode
(1000) (`bass_morph_engine.h:185–197`, `:213`). The engine's names are Lock / Blend / Punch
(`bass_morph_engine.h:49`); the registered strings the user sees are **Hold / Blend / Yield**, index for index
(`morph_params.h:9`, `:170–173`, `:179–180`; FR-036):

| Registered label (1000) | Engine mode | Curve | What the user hears | Source |
|---|---|---|---|---|
| **Hold** | Lock | c = 0 | The Anchor stays on A's settings for the whole morph, even at 100 %. The sub foundation never changes while the rest of the sound morphs. | `bass_morph_engine.h:191–192` |
| **Blend** (default) | Blend | c = p | The Anchor morphs together with everything else. | `:190`; default `:73`, `morph_params.h:185–188` |
| **Yield** | Punch | c = p³ | The Anchor holds near A for most of the travel and gives way to B late. | `:72`, `:193–194` |

| p | 0 | 0.25 | 0.5 | 0.75 | 1 |
|---|---|---|---|---|---|
| Hold (c = 0) | 0 | 0 | 0 | 0 | 0 |
| Blend (c = p) | 0 | 0.25 | 0.5 | 0.75 | 1 |
| Yield (c = p³) | 0 | 0.015625 | 0.125 | 0.421875 | 1 |

(c(0.5) = 0.125 exactly in Punch: `bass_morph_engine.h:187`.)

Consequences the user can see:
- The Anchor's stepped settings (300, 301) use the same 52 % / 48 % rule, applied to p_a (§3). In **Hold** they
  never leave A (rule 1 at p_a = 0). In **Yield**, at rest, they switch to B near p ≈ 80.4 % (p³ = 0.52) and back
  near p ≈ 78.3 % (p³ = 0.48) — derived from `:70–72` and `:394–414`.
- Mass is also resolved linearly at p_a for the Anchor's own use (`bass_morph_engine.h:59`, `:491–501`), so in Hold
  the Anchor keeps A's mass response.
- Changing the mode never jumps: p_a re-targets and travels at the morph smoothing rate (`bass_morph_engine.h:167`,
  `:213–214`, `:370–373`).

## 5. Morph smoothing (Morph Ramp Time, 1002)

"Morph smoothing" is the roadmap's name for Phase 8's position ramp (`specs/Profundum-roadmap.md:888–889`). Phase 11
registered it as **Morph Ramp Time (1002)**, so the roadmap's condition "if Phase 11 registers it" is met (spec
FR-007, spec.md:322–323). The knob shows its registered display string (plan.md:450).

| Property | Value | Source |
|---|---|---|
| Registered title / unit | "Morph Ramp Time", "ms" | `morph_params.h:268–270` |
| Range | 5 – 2000 ms, logarithmic knob law | `morph_params.h:11`, `:160–166`; `bass_morph_engine.h:67–68` |
| Default | 40 ms | `morph_params.h:184`; `bass_morph_engine.h:69` |
| Automatable | yes (`kCanAutomate`) | `morph_params.h:268–270` |
| Display | "%.1f ms" below 1 s, "%.2f s" from 1 s | `morph_params.h:286–295` |

What it does for the user:
- It is the time the smoothed position takes to travel the **full** 0 % → 100 % distance; the position moves at a
  constant rate (step per 32-sample interval = 32 000 / (ramp ms × fs)), so a 0 % → 50 % move takes half the ramp
  time (`bass_morph_engine.h:369–373`).
- The Anchor position p_a slews at the same rate (`bass_morph_engine.h:372`).
- A new ramp time applies from the next interval without moving the position (`bass_morph_engine.h:153–163`).
- Only **position** moves are smoothed. Editing an endpoint's value while sitting between A and B takes effect at the
  next 32-sample interval and is not ramped by the morph engine (`bass_morph_engine.h:109–112`).

## 6. The law table (62 rows)

Columns: `leaf` (the `kLawTable` row name; rows in leaf order 0 – 61), `law`, `axis`, `lo`, `hi`, `id` (the
registered parameter that writes that leaf, from `kSoundRows` / `kParamRoutes`, `sound_param_table.h:63–65`,
`:80–101`). Stepped rows have no continuous range: `kLawTable` stores lo = hi = 0 for them (`morph_state.h:63–67`), so the table prints lo = 0 and hi = the row's last enumerator `maxEnum` (Bool: 1), which is what the `Profundum_UiContractTables` probe emits (`macro_ui_test.cpp:270–271`; regenerated 2026-10-24 by `evidence/close/regen-tables.sh`, log `evidence/run-log/close/regen-tables-probe.log`).
Leaves 34 / 35 and 43 / 44 share one registered ID each, because each Sync Rate row carries two leaves
(`sound_param_table.h:7–9`; `field_params.h:59`, `motion_params.h:63`). `motion.cyclePhaseDeg` is the one half-open
range, [0, 360) (`morph_state.h:65`, `:271–272`).

Source of the rows: `kFieldTable`, `morph_state.h:142–317` (re-exported as `BassMorphEngine::kLawTable`,
`bass_morph_engine.h:74`); range constants from `envelope_utils.h:51–52`, `drop_envelope.h:31–33`,
`bass_stereo_field.h:70–71`, `lfo.h:34–35`, `mass_law.h:72–73`, `transient_shaper.h:78–79`,
`anchor_oscillator.h:84–86`; IDs from `plugin_ids.h:110–205` and the pack row arrays (`morph_params.h:50–53`,
`core_params.h:23–33`, `anchor_params.h:31–41`, `note_params.h:63–75`, `fold_shape_params.h:22–25`,
`character_params.h:29–34`, `dynamics_params.h:21–24`, `field_params.h:46–62`, `motion_params.h:51–75`).

Drafted from the headers in 12a. In 12b, T033 regenerates the CSV between the markers from the
`Profundum_UiContractTables` probe, and the close compliance row cites the diff (plan §3.2). Note for T033's
`regen-tables.js`: the CSV sits inside a `csv` code fence between the markers; replace the CSV lines and keep the two
fence lines.

<!-- GENERATED-BEGIN law-table -->
```csv
leaf,law,axis,lo,hi,id
recipe.depth,Linear,SineComplex,0,1,200
recipe.body,Linear,SineComplex,0,1,201
recipe.bodyCurvature,Linear,SineComplex,0,1,202
recipe.bodyEmphasis,Linear,SineComplex,-1,0,203
recipe.edge,Linear,DarkBright,0,1,204
recipe.shift,Linear,DarkBright,-1,1,205
excitation.mix,EqualPower,CleanNonlinear,0,1,206
excitation.drive,Linear,CleanNonlinear,0,1,207
excitation.asymmetry,Linear,CleanNonlinear,0,1,208
foldAmount,Linear,CleanNonlinear,0,100,500
shape,Linear,DarkBright,0,1,501
characterAlgorithm,Stepped,CleanNonlinear,0,5,600
characterAmount,Linear,CleanNonlinear,0,1,601
characterTone,Linear,DarkBright,0,1,602
dropAmountSt,Linear,ShortLong,0,48,400
dropTimeMs,Logarithmic,ShortLong,1,4000,401
dropShape,Linear,ShortLong,0,1,402
dropRebound,Linear,ShortLong,0,1,403
ampEnv.attackMs,Logarithmic,ShortLong,0.1,10000,420
ampEnv.decayMs,Logarithmic,ShortLong,0.1,10000,421
ampEnv.sustain,Linear,ShortLong,0,1,422
ampEnv.releaseMs,Logarithmic,ShortLong,0.1,10000,423
ampEnv.attackCurve,Linear,ShortLong,-1,1,424
ampEnv.decayCurve,Linear,ShortLong,-1,1,425
ampEnv.releaseCurve,Linear,ShortLong,-1,1,426
field.mode,Stepped,MonoStereo,0,2,800
field.spread,Linear,MonoStereo,0,1,801
field.gravity,Linear,MonoStereo,0,1,802
field.crossoverHz,Logarithmic,MonoStereo,60,250,803
field.curve,Linear,MonoStereo,0,1,804
field.motionDepth,Linear,MonoStereo,0,1,805
field.motionRate,Linear,MonoStereo,0,1,806
field.motionSource,Stepped,MonoStereo,0,1,807
field.cycleSync,Stepped,MonoStereo,0,1,808
field.cycleNote,Stepped,MonoStereo,0,9,809
field.cycleModifier,Stepped,MonoStereo,0,2,809
motion.source,Stepped,Motion,0,3,900
motion.target,Stepped,Motion,0,4,901
motion.amount,Linear,Motion,-1,1,902
motion.depth,Linear,Motion,0,1,903
motion.cycleRateHz,Logarithmic,Motion,0.01,20,904
motion.cycleShape,Stepped,Motion,0,5,905
motion.cycleSync,Stepped,Motion,0,1,906
motion.cycleNote,Stepped,Motion,0,9,907
motion.cycleModifier,Stepped,Motion,0,2,907
motion.cyclePhaseDeg,Linear,Motion,0,360 (open),908
motion.cycleRetrigger,Stepped,Motion,0,1,909
motion.cycleFadeInMs,Logarithmic,Motion,0,5000,910
motion.polarity,Stepped,Motion,0,1,911
motion.driftSmoothness,Linear,Motion,0,1,912
motion.envAttackMs,Logarithmic,Motion,0.1,10000,913
motion.envDecayMs,Logarithmic,Motion,0.1,10000,914
mass,Linear,CleanNonlinear,0,100,104
transientAttackDb,Linear,ShortLong,-12,12,700
transientSustainDb,Linear,ShortLong,-12,12,701
anchor.waveform,Stepped,Anchor,0,4,300
anchor.mode,Stepped,Anchor,0,3,301
anchor.level,Linear,Anchor,0,1,302
anchor.punchBoostDb,Linear,Anchor,0,12,303
anchor.punchTimeMs,Logarithmic,Anchor,5,250,304
anchor.ghostTimeMs,Logarithmic,Anchor,1,100,305
anchorFoldFeed,EqualPower,Anchor,0,1,306
```
<!-- GENERATED-END -->

### Summary by axis (counted from the table above)

| Axis | Rows (leaves) | Laws |
|---|---|---|
| SineComplex | 4 (0 – 3) | 4 Linear |
| DarkBright | 4 (4, 5, 10, 13) | 4 Linear |
| CleanNonlinear | 7 (6 – 9, 11, 12, 52) | 5 Linear, 1 Equal-power (Excitation Mix), 1 Stepped (Character) |
| ShortLong | 13 (14 – 24, 53, 54) | 9 Linear, 4 Logarithmic (Drop Time, Attack, Decay, Release) |
| MonoStereo | 11 (25 – 35) | 5 Linear, 1 Logarithmic (Crossover), 5 Stepped |
| Motion | 16 (36 – 51) | 4 Linear, 4 Logarithmic (Cycle Rate, Cycle Fade-In, Env Attack, Env Decay), 8 Stepped (seven IDs; 907 carries two leaves) |
| Anchor | 7 (55 – 61), resolved at p_a | 2 Linear, 2 Logarithmic (Punch Time, Ghost Time), 1 Equal-power (Anchor Fold Feed), 2 Stepped |
| **Total** | **62** | **33 Linear, 11 Logarithmic, 2 Equal-power, 16 Stepped** |
