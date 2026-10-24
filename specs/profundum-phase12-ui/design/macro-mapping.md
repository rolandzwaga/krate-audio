# D3 — Macro mapping

**Status: RATIFIED by the user, 2026-10-23 (spec Clarifications "Session 2026-10-23 (12a ratification)").**

Serves FR-005, FR-006 (part (a), SC-006 (a)), FR-021, FR-060, FR-061. Plan sources: §3.2, §4.6.5, §4.10, §5.5. Task T004.

The four personality macros are Weight 100, Dark ↔ Bright 101, Tight ↔ Loose 102 and Clean ↔ Violent 103
(`plugins/profundum/src/plugin_ids.h:111–114`). Each is bipolar about the neutral 0.5 (`ProfundumMacroMatrix::kNeutral`,
`dsp/include/krate/dsp/systems/profundum_macro_matrix.h:67`). The UI adds no mapping of its own (FR-021): every statement
below is read from the engine's `kRows` table (`profundum_macro_matrix.h:75–92`) and the engine's own contribution math
(`:193–199`, `:240–243`).

---

## 1. The macro table (FR-005)

Source: `ProfundumMacroMatrix::kRows`, `dsp/include/krate/dsp/systems/profundum_macro_matrix.h:75–92` (harvested 2026-10-21,
`:73–74`). These are the same 16 rows as Phase 9 handoff 12.1 (`specs/profundum-phase9-voice-engine/evidence/phase-handoffs.txt:157–178`).
The target order is `ProfundumMacroTarget` (`profundum_macro_matrix.h:41–47`). The registered-ID column follows the plan §4.10
`kMacroTargetParamIds` order.

Each ID's leaf, checked against its section pack:

- 200 → leaf 0 `recipe.depth`, 201 → 1 `recipe.body`, 204 → 4 `recipe.edge`, 205 → 5 `recipe.shift`, 206 → 6
  `excitation.mix`, 207 → 7 `excitation.drive` (`plugins/profundum/src/parameters/core_params.h:24–31`);
- 302 → 57 `anchor.level` (`anchor_params.h:36`);
- 500 → 9 `foldAmount`, 501 → 10 `shape` (`fold_shape_params.h:23–24`);
- 601 → 12 `characterAmount`, 602 → 13 `characterTone` (`character_params.h:32–33`);
- 420 / 421 / 423 → 18 / 19 / 21 `ampEnv.attackMs` / `decayMs` / `releaseMs` (`note_params.h:68–71`);
- 700 → 53 `transientAttackDb` (`dynamics_params.h:22`).

Leaf ranges and laws are in `dsp/include/krate/dsp/systems/morph_state.h:160–316` (`kFieldTable`). `BassMorphEngine::kLawTable`
is an alias of it (`bass_morph_engine.h:74`).

Units:

- "time ×2^c" means the amp time is multiplied by 2^c and clamped to [0.1 ms, 10 000 ms] (`scaleTimeClamped`,
  `profundum_macro_matrix.h:260–268`; `kMinEnvelopeTimeMs` / `kMaxEnvelopeTimeMs`, `primitives/envelope_utils.h:51–52`).
- Every other row adds c to the field and clamps it to the field's range (`addClamped`, `profundum_macro_matrix.h:249–257`;
  the ranges are in `apply`, `:209–223`).

T033's probe regenerates this block in 12b.

<!-- GENERATED-BEGIN macro-rows -->
| Row | Macro | Target | Amount | Curve | Unit | Registered ID | Flags |
|---:|---|---|---:|---|---|---|---|
| 0 | Weight | RecipeDepth | 0.5625 | Linear | recipe [0, 1] | 200 Depth | |
| 1 | Weight | RecipeBody | 0.5625 | Linear | recipe [0, 1] | 201 Body | |
| 2 | Weight | AnchorLevel | 0.5625 | Linear | level [0, 1] | 302 Anchor Level | |
| 3 | DarkBright | RecipeEdge | 0.5 | Linear | recipe [0, 1] | 204 Edge | |
| 4 | DarkBright | RecipeShift | 0.5 | Linear | recipe [−1, 1] | 205 Spectral Shift | |
| 5 | DarkBright | Shape | 0 | Linear | SHAPE [0, 1] | 501 Shape | **inert** (amount 0; ruling B10-2) |
| 6 | DarkBright | CharacterTone | 0.300000012 | Linear | Tone [0, 1] | 602 Character Tone | |
| 7 | TightLoose | AmpAttackOct | 5 | Linear | octaves of time (time ×2^c) | 420 Attack | |
| 8 | TightLoose | AmpDecayOct | 5 | Linear | octaves of time (time ×2^c) | 421 Decay | |
| 9 | TightLoose | AmpReleaseOct | 5 | Linear | octaves of time (time ×2^c) | 423 Release | |
| 10 | TightLoose | TransientAttackDb | −15 | Linear | dB [−12, 12] | 700 Transient Attack | |
| 11 | TightLoose | CompressionAmount | −1.25 | Linear | amount [0, 1], post-law | — | **no control** |
| 12 | CleanViolent | ExcitationMix | 0.5 | Linear | [0, 1] | 206 Excitation Mix | |
| 13 | CleanViolent | ExcitationDrive | 0.5 | Linear | [0, 1] | 207 Excitation Drive | |
| 14 | CleanViolent | FoldAmount | 50 | Linear | [0, 100] | 500 Harmonic Fold | |
| 15 | CleanViolent | CharacterAmount | 0.5 | Linear | [0, 1] | 601 Character Amount | |
<!-- GENERATED-END -->

Row notes:

- **Row 5 (inert).** `DarkBright → Shape` ships with amount 0 (`profundum_macro_matrix.h:81`). The sign check skips it
  (`:151–153`: "ruling B10-2 (2026-10-20): SHAPE is dropped from Dark <-> Bright (amount 0)"). The reason for the ruling is
  recorded at `specs/profundum-phase9-voice-engine/spec.md:603` (and B10-2 at `:1173`): on B_s the macro sweep broke at 0.55–0.65, because
  ShapeMorph's centroid is not monotone. The row stays in the table and is shown as dropped, not hidden. It draws no ring and
  is not highlighted (§3, §4).
- **Row 6.** The amount is the float 0.300000012, that is `0.3f` (`profundum_macro_matrix.h:82`; handoff 12.1,
  `phase-handoffs.txt:178`).
- **Row 11 (no control).** `CompressionAmount` is not a `MorphState` field, and `apply` never touches it
  (`profundum_macro_matrix.h:201–203`).
  - The engine hands `clamp(law compressionAmount + c, 0, 1)` after the Mass law, through `compressionContribution()` (`:226–229`).
  - It has no registered ID (plan §4.10: `kNoParamId`).
  - Phase 9 handoff 11.6 records the consequence (`phase-handoffs.txt:149–152`): at Mass ≤ 50 the law hands 0, so the Loose
    half cannot lower compression there. The Tight half acts at every Mass.
  - The Tight ↔ Loose 102 tooltip names **Compression** (FR-060; D2 item 5).
- **Contribution.** For macro value m, u = (m − 0.5)·2 and c = amount · applyModCurve(curve, |u|) · sign(u)
  (`profundum_macro_matrix.h:240–243`). Every row is `Linear`, and `applyModCurve(Linear, x) = x` for x clamped to [0, 1]
  (`dsp/include/krate/dsp/core/modulation_curves.h:38–43`), so c = amount · u. At the neutral 0.5, c = 0 for every row.

## 2. Macro offsets do not move the knobs (FR-005, FR-061)

Macro offsets act on the resolved state and **do not move the knobs** (`apply(MorphState&)`, `profundum_macro_matrix.h:204`);
they are drawn as a one-sided ring from the knob's value toward value + offset (FR-061, P-5 ruling 2026-10-23).

- A ±0 contribution is skipped, so a macro at neutral leaves the state bit-identical (`profundum_macro_matrix.h:201–202`,
  `:249–251`, `:260–263`).
- The ring is drawn by the plugin-local `OffsetRingKnob`, an `ArcKnob` subclass in `plugins/profundum/src/ui/` (tasks.md
  precondition table, P-5). A target bound to any other control type draws no ring (plan §4.6.5).
- **Motion modulation is not drawn** (FR-061). The rings are identical with Motion 900 Off and with Motion 900 on Cycle (SC-028).

## 3. Highlight sets (FR-060)

Hovering or moving macro m highlights the controls in `kMacroTargetParamIds` that belong to m's `kRows` rows with
`amount != 0`. `CompressionAmount` is excluded because it has no control. Nothing is highlighted otherwise, and no permanent
matrix is drawn.

| Macro | ID | Highlight set | Excluded, and why |
|---|---|---|---|
| Weight | 100 | {200, 201, 302} | — |
| Dark ↔ Bright | 101 | {204, 205, 602} | 501 Shape: row 5, amount 0 (`profundum_macro_matrix.h:81`) |
| Tight ↔ Loose | 102 | {420, 421, 423, 700} | Compression: row 11, no control; named in the macro's tooltip instead |
| Clean ↔ Violent | 103 | {206, 207, 500, 601} | — |

Only some of these IDs are on the Main page (D1 item 3: 200, 201, 204, 302, 500). The rest are highlighted where D2 places them.
SC-009 compares each set against this table.

## 4. Ring arithmetic (FR-061; plan §4.6.5, §5.5)

The controller computes `macroRingRange(ParamID id) const noexcept -> float` (plan §4.6.5):

1. Look up the target index t for `id` in `kMacroTargetParamIds` (plan §4.10). Return 0 if there is none, if t is
   `CompressionAmount`, or if `kRows[t].amount == 0`.
2. Build the macro values by copy-then-assign. `ProfundumMacroValues` has float members (`profundum_macro_matrix.h:58–63`), so a
   brace init would narrow.
   - Set `v.weight = float(getParamNormalized(100))`, and likewise `darkBright` (101), `tightLoose` (102) and `cleanViolent` (103).
   - Then `ProfundumMacroMatrix mm; mm.setMacros(v);` and `c = mm.contributions()[t]` (`profundum_macro_matrix.h:175–180`,
     `:193–199`).
   - Return 0 if c is ±0.
3. Take the row `kSoundRows[kParamRoutes[id].index]` (`plugins/profundum/src/parameters/sound_param_table.h:63`, `:101`), its
   `const FieldInfo& f = lawOf(row.leaf)` (`sound_param_row.h:66–68`), and `base = plainFromNormalized(f, getParamNormalized(id))`
   (`sound_param_row.h:178–183`). Then apply the `apply` rules:
   - amp times 420 / 421 / 423: `mod = clamp(base · 2^c, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs)`
     (`profundum_macro_matrix.h:260–268`);
   - every other target: `mod = clamp(base + c, f.lo, f.hi)` (`profundum_macro_matrix.h:249–257`).
4. Return `float(normalizedFromPlain(f, mod) − getParamNormalized(id))` (`sound_param_row.h:186–192`). The result is signed and
   lies in [−1, 1]. The ring runs from the knob's value to value + this offset.

Laws that matter here (`morph_state.h`):

- Every target other than the amp times is `Linear` on its box, with one exception: `excitation.mix` is `EqualPower`
  (`:173–174`). The knob still maps `excitation.mix` linearly: `plainLawOf` is logarithmic only for the `Logarithmic` law (`sound_param_row.h:172–175`).
  So for all of these targets, Δnorm = c / (hi − lo) until the clamp.
- The amp times are `Logarithmic` over [0.1, 10 000] ms (`morph_state.h:204–211`).

**Log-time hand check (plan §5.5).** For an amp time, unclamped:

- Δnorm = c · ln 2 / ln(tMax / tMin) = c · ln 2 / ln(10⁵) = c · log₁₀2 / 5 = 0.0602060 · c, whatever the base.
- At Tight ↔ Loose = 1 (c = +5) the ring is +0.301030. At 0 (c = −5) it is −0.301030.
- "Unclamped" means base · 2^c stays inside [0.1, 10 000] ms: base ≤ 312.5 ms for c = +5, base ≥ 3.2 ms for c = −5.
- Example: attack 10 ms → 320 ms gives log₁₀(32) / 5 = 1.505150 / 5 = 0.301030.
- Clamped example: release 5000 ms at c = +5 gives 160 000 ms, clamped to 10 000 ms, so Δnorm = log₁₀(10 000 / 5000) / 5 = 0.060206.

**Worked rings at the registered defaults.** The defaults are `MorphState{}` (`kDefaultLeafBits`, `sound_param_row.h:113`):

- depth 0.5, body 0.5, edge 0.5, shift 0 (`processors/spectral_shape_recipe.h:240–241`);
- excitation mix 0, drive 0.5 (`systems/profundum_core.h:355–356`);
- anchor level 0.5 (`systems/anchor_oscillator.h:72`, `:102`);
- fold 0, shape 0, character amount 0, tone 0.5 (`morph_state.h:105–109`);
- amp attack 10 ms, decay 50 ms, release 100 ms (`morph_state.h:72–75`);
- transient attack +6 dB (`morph_state.h:118`; `processors/transient_shaper.h:82`).

| ID | Macro at 1 (c = +amount) | Ring | Macro at 0 (c = −amount) | Ring |
|---|---|---:|---|---:|
| 200 | 0.5 + 0.5625 → 1 (clamp) | +0.5 | 0.5 − 0.5625 → 0 (clamp) | −0.5 |
| 201 | 0.5 + 0.5625 → 1 (clamp) | +0.5 | → 0 (clamp) | −0.5 |
| 302 | 0.5 + 0.5625 → 1 (clamp) | +0.5 | → 0 (clamp) | −0.5 |
| 204 | 0.5 + 0.5 = 1 | +0.5 | 0 | −0.5 |
| 205 | 0 + 0.5 = 0.5 on [−1, 1] | +0.25 | −0.5 | −0.25 |
| 501 | row 5 inert | none | row 5 inert | none |
| 602 | 0.5 + 0.3 = 0.8 | +0.3 | 0.2 | −0.3 |
| 420 | 10 → 320 ms | +0.301030 | 10 → 0.3125 ms | −0.301030 |
| 421 | 50 → 1600 ms | +0.301030 | 50 → 1.5625 ms | −0.301030 |
| 423 | 100 → 3200 ms | +0.301030 | 100 → 3.125 ms | −0.301030 |
| 700 | 6 − 15 = −9 dB on [−12, 12] | −0.625 | 6 + 15 = 21 → 12 (clamp) | +0.25 |
| 206 | 0 + 0.5 = 0.5 | +0.5 | 0 − 0.5 → 0 (clamp) | 0 (draws nothing) |
| 207 | 0.5 + 0.5 = 1 | +0.5 | 0 | −0.5 |
| 500 | 0 + 50 = 50 on [0, 100] | +0.5 | → 0 (clamp) | 0 (draws nothing) |
| 601 | 0 + 0.5 = 0.5 | +0.5 | → 0 (clamp) | 0 (draws nothing) |

At macro 0.75 (u = 0.5), each unclamped ring is half its macro-at-1 value: for example 200 → +0.28125 and the amp times → +0.150515.

**Dead band.** `ArcKnob::setModulationRange` keeps the stored value unless the new one differs from it by more than 0.0005
(`plugins/shared/src/ui/arc_knob.h:74–78`). The shared header is not changed. So `macroRingRange(id)` is the value of record
(exact to SC-028's 1e-6), and each knob's drawn range agrees with it within 5e-4 (plan §4.6.5).

## 5. Main-page audibility column (SC-006 (a), FR-006)

This table gives each of the 17 Main IDs of D1 item 3 — {1, 100, 101, 103, 104, 105, 200, 201, 204, 300, 302, 400, 412, 500, 900,
1000, 1001} — with its gated PASS row.

- "CSV :n" is the line number in `specs/profundum-phase9-voice-engine/evidence/audibility-table.csv`. Line 1 is the header, and
  every cited line has `pass` = PASS and `gated` = gated.
- "PPA" is a SECTION of `Profundum_ParamPathAudibility` in `plugins/profundum/tests/integration/param_path_audibility_test.cpp`
  (`TEST_CASE` at :75).
- AT-03, AT-10 and AT-26 are the rows plan §3.2 names. The others were found by their contrast text.

| ID | Control | Gated PASS row(s) | Value vs bar |
|---|---|---|---|
| 1 | Voice Mode | MPA SECTION "1 Voice Mode: Mono -> Poly" (first note held, second added at 1.0 s) | E_sub at the first f0, 50.7747 / 54.2378 dB vs ≥ 3 dB (E1 / C3; run-log T039.txt) |
| 100 | Weight | AT-47 "Macro Weight: 0 -> 1", A + sustain 1: CSV :92 (E1), :97 (C3) | E_sub 3.21192 / 3.20928 dB vs ≥ 3 dB |
| 101 | Dark ↔ Bright | AT-48 "Macro Dark <-> Bright: 0 -> 1", base B + sustain 1 (ruling B10-1): CSV :93 (E1), :98 (C3) | D_br 0.945659 / 0.800617 oct vs ≥ 0.25 oct. On base A, rows :94 and :99 FAIL and are recorded, not gated: the macro is inert on the sine recipe by construction (B10-1). |
| 103 | Clean ↔ Violent | AT-50 "Macro Clean <-> Violent: 0 -> 1": CSV :96 (E1), :101 (C3) | D_nl 11.0488 / 11.8209 dB vs ≥ 3 dB |
| 104 | Mass | AT-26 "Mass: 0 -> 100", A + sustain 1 + anchor level 0.5 (G11-2): CSV :102 (E1), :109 (C3); on B: CSV :104 (E1). PPA "AT-27 macro: Mass 104 0 -> 100" (:82) | E_sub 5.24671 / 5.3203 / 4.09452 dB vs ≥ 3 dB |
| 105 | Bass Morph | AT-41 "Morph position: 0 -> 1": CSV :116 (E1), :118 (C3); AT-42 "0 -> 0.5": CSV :117, :119. PPA "AT-42 morph: Morph Position 105 0 -> 0.5" (:96) | D_nl 14.3505 / 13.8628 dB (AT-41) and 8.15013 / 6.72976 dB (AT-42) vs ≥ 3 dB |
| 200 | Depth | MPA SECTION "200 Depth: 0 -> 1 on kSawAnchor, Edge 0.5" | D_cx 30.8489 / 30.9367 dB vs ≥ 3 dB |
| 201 | Body | MPA SECTION "201 Body: 0 -> 1 on kSawAnchor, Edge 0.5" | D_cx 22.0919 / 22.0079 dB vs ≥ 3 dB |
| 204 | Edge | MPA SECTION "204 Edge: 0 -> 1 on kSawAnchor" | D_br 2.43288 / 2.45642 oct vs ≥ 0.25 oct |
| 300 | Anchor character (waveform) | AT-14 – AT-17 "Anchor waveform: Sine -> Triangle / RoundedSine / AsymmetricSine / OctaveDownSine": CSV :8 – :11 (E1), :19 – :22 (C3) | D_cx 5.6124 – 15.8745 dB vs ≥ 3 dB; AT-17 power at f0/2 53.97 dB vs ≥ 3 dB |
| 302 | Anchor level | AT-10 "Anchor: level 0 -> 1": CSV :2 (E1), :13 (C3). PPA "AT-10 anchor: Anchor Level 302 0 -> 1" (:84) | E_sub 5.94809 / 5.92968 dB vs ≥ 2 dB |
| 400 | Drop | AT-19 "Drop amount: 0 -> 24 st": CSV :166 (E1), :171 (C3) | start pitch 24 st vs ≥ 6 st |
| 412 | Glide | AT-23 "Glide: portamento time 0 -> 200 ms": CSV :170 (E1), :175 (C3). PPA "AT-23 glide: Portamento 412 0 -> 200 ms" (:87) | t_50 ratio 287.344 / 287.219 vs ≥ 2 |
| 500 | Fold | AT-03 "Fold: foldAmount 0 -> 100": CSV :57 (E1), :67 (C3). PPA "AT-03 fold: Harmonic Fold 500 0 -> 100" (:90) | D_nl 47.6755 / 47.6593 dB vs ≥ 3 dB |
| 900 | Motion source | AT-34 "Off -> CYCLE" (target Mass, anchor level 0.5): CSV :120, :139. AT-39 "Off -> ENV" (with the per-row ENV decay 1000 ms): CSV :133 (E1); AT-39: CSV :151 (C3). AT-40 "Off -> DRIFT": CSV :136, :155. PPA "AT-38 motion: Motion Source 900 Off -> Cycle" (:95) | frame-wise E_sub p2p 3.13675 – 5.75851 dB vs ≥ 3 dB |
| 1000 | Anchor morph behaviour (Hold / Blend / Yield) | MPA SECTION "1000 Anchor Morph Mode: Hold -> Blend at Morph Position 1" (A Anchor Level 1, B 0) | E_sub 5.9489 / 5.92955 dB vs ≥ 2 dB (the AT-10 bar) |
| 1001 | Edit Target | **Exempt (user ruling 2026-10-24): routing control, not a sound lever** | Its audibility is AT-41 / AT-42 (105), whose B endpoint is written through 1001 = B |

Count (updated 2026-10-24): 16 of the 17 Main IDs have a gated PASS row; 1001 is exempt by ruling. MPA = `Profundum_MainPageAudibility` (main_page_audibility_test.cpp, T039, ruled 2026-10-24). Was: 11 of the 17 Main IDs have a gated PASS row. The other 6 (1, 200, 201, 204, 1000, 1001) have none. Under SC-006 (a) these
are 12a defects, and the user must rule on them before 12b (T011). Nothing above is invented: every cited row is either a CSV
line marked `PASS` and `gated`, or a named PPA SECTION.
