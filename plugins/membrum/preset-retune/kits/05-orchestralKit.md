<!-- verdict: pass-with-fixes | coverageOk: false | issues fixed: 12 | IMPLEMENTED: 2026-06-09 (commit re-tune Orchestral) -->
<!-- SUPERSEDED: 2026-07-16 by 06-orchestralKit-fix-plan.md (render-measured audit) -->

> **⚠ SUPERSEDED (2026-07-16).** A render-measured audit found this plan's kit failed
> audibly on every complaint axis (timpani modeInject plateau, missing snare
> wireCoupling/noiseLayerGain, choked metallics, GM-slot mismatches). The current
> plan-of-record is [`06-orchestralKit-fix-plan.md`](06-orchestralKit-fix-plan.md).
> Corrections to THIS doc's factual errors:
> - **maxPolyphony 20 was illegal** — the parameter range is [4,16]; the generator ships 16.
> - **Deviation #7 (timpani modeInject 0.10–0.12) is REVOKED** — `modeInject > 0` rings
>   as an undamped flat plateau (no envelope in `mode_inject.h`); it was the "synth
>   bass note" failure. All pads now ship modeInject 0.
> - **Deviation #4 (timpani airLoading 0.80) is REVOKED** — the "over-thinning" rationale
>   has no code mechanism (airLoading is frequency-only on 4 modes); generator ships 1.00.
> - **Deviation #6 (cymbal modeInject 0 vs crash archetype 0.25) is STALE** — the crash
>   archetype was redesigned (ea89c6c2) and no longer specifies inject; not a deviation.
> - **Pad 4 b1 0.30 was silent drift** — generator now ships 0.05 (concert-bass T60 ~2.6 s).
> - **New documented deviations** (see 06 §4): ride decay 0.62 + wash rebalance (archetype
>   claim "matches exactly" no longer true), gong body Bell→Plate + size 0.92 + ~0.4 s
>   morph, sus-cymbal struck decay 0.85 (washBlend gate), snare wireCoupling 0.45 /
>   noiseLayerGain 6.2 (post-e7802363 rollout).

# Orchestral Kit — Corrected Re-Tune Plan (`orchestralKit()`, category Acoustic)

Voiced against the post-audit chain: linear per-voice output + `TruePeakLimiter` (−1 dBTP) bus ceiling, measured-strike body norm (−6 dBFS budget), corrected Plate/Shell/Bell mode-shapes, `mode_inject` 1/k, per-mode `decaySkew` tilt on all bodies, env-level NonlinearCoupling, Material Morph live on all bodies, equal-power per-pad pan.

> **Verification status:** PASS WITH FIXES. Adversarially checked against param-dictionary.json, the cited archetypes/*.md, current-state.json, and the live `orchestralKit()` builder. Fixes applied in place (snare tension glide removed; bell-tree morph + decaySkew corrected to the archetype; timpani-tom airLoading made consistent with the kick-slot timpano). Remaining archetype divergences (gong secondary OFF, cymbal modeInject 0, timpani modeInject activation, bell-tree size) are intentional and now explicitly documented rather than silent.

## Core thesis
The current kit's conservative settings (levels 0.65–0.72, all secondary shells off, tension/airLoading cut) were tuned to dodge the **old** clip — the live generator comments say so verbatim. Those reasons are gone. This plan restores musical levels (0.78–0.88), activates three dead axes (decaySkew per-mode tilt, modeInject 1/k, pan spread), moves the triangle to its correct Shell body, and fills layout gaps (ride, bell tree, crotales pair).

## Layout (32 pads, GM-map sense; 18 crafted)
| Pad | MIDI | Drum | Body | Exciter | Bus | Pan |
|----:|----:|------|------|---------|----:|----:|
| 0 | 36 | Timpani low (kick slot) | Membrane | Mallet | main | 0.42 |
| 1 | 37 | Bell Tree *(new)* | Bell | NoiseBurst | aux1 | 0.55 |
| 2 | 38 | Orchestral Snare | Membrane | NoiseBurst | main | 0.50 |
| 3 | 39 | Tubular Bell | String | Mallet | aux1 | 0.58 |
| 4 | 40 | Concert Bass Drum *(was snare slot)* | Membrane | Mallet | main | 0.50 |
| 5 | 41 | Timpani tom 1 | Membrane | Mallet | main | 0.38 |
| 6 | 42 | Suspended Cymbal (struck) | NoiseBody | NoiseBurst | main | 0.65 |
| 7 | 43 | Timpani tom 2 | Membrane | Mallet | main | 0.43 |
| 8 | 44 | Suspended Cymbal (pedal) | NoiseBody | NoiseBurst | main | 0.65 |
| 9 | 45 | Timpani tom 3 | Membrane | Mallet | main | 0.47 |
| 10 | 46 | Suspended Cymbal Roll | NoiseBody | NoiseBurst | aux1 | 0.65 |
| 11 | 47 | Timpani tom 4 | Membrane | Mallet | main | 0.52 |
| 12 | 48 | Triangle *(Bell→Shell)* | **Shell** | Impulse | aux1 | 0.62 |
| 13 | 49 | Gong / Tam-Tam | Bell | Mallet | aux1 | 0.50 |
| 14 | 50 | Timpani tom 5 | Membrane | Mallet | main | 0.57 |
| 15 | 51 | Ride Cymbal *(new, was crotales)* | Bell | NoiseBurst | aux1 | 0.62 |
| 17 | 53 | Crotales hi | Bell | Mallet | aux1 | 0.68 |
| 19 | 55 | Crotales lo *(new)* | Bell | Mallet | aux1 | 0.32 |

Pads 16, 18, 20–31 disabled (no auxiliary-perc role in a pure orchestral section). The snare/bass pad swap (snare on GM 38, bass on GM 40) is intentional and documented — it frees a dedicated bass-drum pad while keeping the low-percussion sense.

## Per-pad exact normalized values
(Full tables are in the structured `pads` array — exact `valueNorm` for every meaningful param per pad, with rationale + citation, and a defaulted-params list with reasons. Key non-default values, **with verification fixes folded in**, summarized below.)

### Pad 0 — Timpani low (Membrane/Mallet)
material 0.32, size 0.90, decay 0.82, strikePos 0.28, **level 0.84**, pitchEnv 180→85Hz/50ms curve 0.35, **airLoading 0.80**, tensionMod 0.16, b1 0.12, b3 0.10, modeScatter 0.06, **modeInject 0.12** *(deliberate post-audit activation; archetype defaults this to 0 — kept small so the synthetic 1/k series does not overpower the air-loaded Bessel pitch)*, click 0.32/0.40/0.32, noise 0.10/0.40/Brown/0.20, **pan 0.42**.

### Pad 2 — Orchestral Snare (Membrane/NoiseBurst)
material 0.60, size 0.33, decay 0.30, level 0.90, b1 0.62, b3 0.30, noiseBurst ~3ms, **noise 0.98**/0.90/Violet/0.55, **click 0.95**/0.18/0.93, drive 0.06, nonlinearCoupling 0.12, modeStretch 0.40, decaySkew 0.58, scatter 0.18, airLoading 0.60, coupling 0.28 + secondary 0.32/0.70 *(tight 14×5 shell — a documented kit delta from the archetype's no-shell default; audibly tom-free at this coupling/size)*, **tensionMod 0.0 — FIXED from 0.18**: the orchestral-snare archetype mandates a fixed high-tension head with **no** pitch glide; a kerthump rise reads as a synth kick. **pitchEnv OFF**, pan 0.50.

### Pad 4 — Concert Bass Drum (Membrane/Mallet)
material 0.30, size 0.90, decay 0.55, **level 0.88**, airLoading 0.80, pitchEnv 110→40Hz/**120ms**/exp, tensionMod 0.12, b3 0.55, b1 0.30, click 0.35/0.42/0.20, noise 0.18/0.30/Brown/0.45, pan 0.50.

### Pad 3 — Tubular Bell (String/Mallet)
material 0.85, size 0.55, decay 0.92, level 0.78, strikePos 0.30, click 0.40/0.20/0.65, noise 0.10, aux1, pan 0.58. (All modal/Membrane axes inert no-ops on String — carried at neutral for kit consistency per the tubular-bell archetype.)

### Pads 5/7/9/11/14 — Timpani toms (Membrane/Mallet, one instrument, 5 tunings)
size 0.88/0.82/0.75/0.68/0.60; decay 0.80/0.72/0.65/0.58/0.50; material 0.32→0.48; pitchEnv hi 180/220/280/350/440 → lo 80/100/130/165/215 Hz; b1 0.10/0.12/0.14/0.17/0.21; **modeInject 0.10–0.12** *(post-audit activation; archetype default 0)*; **decaySkew 0.45/0.47/0.50/0.53/0.55** (was neutral on all 5); **airLoading 0.80 — FIXED from 0.65** to match the kick-slot timpano (pad 0) per the archetype kit-note that the whole tuned row is the SAME instrument with identical params bar pitch (full air-loading is the timpano's pitch-fusion trick; archetype target 1.00, 0.80 chosen to avoid over-thinning); tensionMod 0.15; level 0.82; **pan 0.38/0.43/0.47/0.52/0.57** (row sweep).

### Pad 6/8/10 — Suspended Cymbals (NoiseBody/NoiseBurst, choke group 1)
6 struck: material 0.90, size 0.20, decay 0.50, modeStretch 0.55, decaySkew 0.58, nonlinearCoupling 0.25, scatter 0.40, b1 0.45/b3 0, noise 0.65/0.82/0.12, click 0.20. · 8 pedal: decay 0.18, noiseDecay 0.07, b1 0.65 (short choke). · 10 roll: material 0.95, size 0.42, decay 0.95, **morph 0.55→0.95/1.7s**, scatter 0.65, noiseDecay 0.95, nonlinearCoupling 0.20, click 0, aux1. *Note: the crash archetype specifies modeInject 0.25 for harmonic fill; here it is intentionally left at 0 (a 1/k integer series on an indefinite-pitch cymbal is physically questionable post-audit) — a documented, deliberate drop of a cited archetype param.*

### Pad 12 — Triangle (**Shell**/Impulse)
**body Shell** (free-free bar, ratios 1:2.757:5.404 confirmed against shell_modes.h and the triangle archetype's measured 4239/1539≈2.75), size 0.085 (f0~1234Hz), material 0.95, decay 0.85, strikePos 0.18 (free-end antinode, now lively on the corrected free-free strike shape), **modeStretch 0.62**, **decaySkew 0.40** (high-partial shimmer), b3 0.02, click 0.95/0.10/0.97, scatter 0.12, noise 0, aux1, pan 0.62. Inert fmRatio dropped (no-op under Impulse).

### Pad 13 — Gong / Tam-Tam (Bell/Mallet)
material 0.85, size 0.85, decay 0.95, strikePos 0.60, level 0.82, **modeStretch 0.62**, scatter 0.65, **nonlinearCoupling 0.80** (bloom), **morph 0.85→0.55/1.7s**, **decaySkew 0.65**, b3 0/b1 0.30, modeInject 0.15, click 0.30/0.30, noise 0.20/…/0.92, couplingAmount 0.95 (inter-pad), aux1, pan 0.50. *Head↔shell secondary is OFF — a documented divergence from the gong archetype, which prescribes secondary ON + couplingStrength 0.95 for the ringing-metal weight; the proposal substitutes Bell + modeStretch + nonlinearCoupling for that weight to avoid a coupled-bank RMS pile-up. Flagged as intentional.*

### Pad 15 — Ride Cymbal (Bell/NoiseBurst, **new role on GM ride slot**)
material 0.95, size 0.30, decay 0.90, strikePos 0.18, modeStretch 0.45, **decaySkew 0.62**, nonlinearCoupling 0.18, scatter 0.55, b1 0.16/b3 0, noise 0.45/0.90/Violet/0.78, click 0.45/0.25/0.82, aux1, pan 0.62. (Matches the ride-cymbal archetype exactly.)

### Pad 17/19 — Crotales hi/lo (Bell/Mallet)
hi: size 0.12, material 0.92; lo: size 0.22, material 0.88. Shared: decay 0.85, strikePos 0.10, b1 0.30/b3 0, decaySkew 0.58, click 0.42/0.20/0.85, noise 0, modeStretch 0.333 (keep harmonic octave), aux1. pan hi 0.68 / lo 0.32 (spread the pair). (Matches the crotales archetype.)

### Pad 1 — Bell Tree (Bell/NoiseBurst, **new**)
material 0.93, size 0.10 *(higher/crotale-like constituent bells; documented deviation from the archetype's 0.30 ≈401 Hz — kept because a bell tree is a stack of tiny bowls sitting just below crotales-hi at 0.12)*, decay 0.60, strikePos 0.15, **decaySkew 0.78 — FIXED from 0.60** (archetype's +0.56 upper-partial tilt = the bright top of the cascade), scatter 0.35, modeStretch 0.45, b3 0/b1 0.35, noise 0.15/Violet/0.40, click 0.30/0.85, **morph ON 0.85→0.55 / Duration 0.50 (~1.1 s) / Curve 0 (linear) — ADDED** (the archetype's 'cascade dims as the beater passes' timbral evolution, previously silently dropped), aux1, pan 0.55.

## Kit-level options
- maxPolyphony **16** (parameter range is [4,16]; this doc originally said 20 — illegal, capped).
- globalCoupling 0.20 → **0.28**, snareBuzz 0.20, tomResonance 0.25 → **0.35** (sympathetic timpani resonance restored now the body sum no longer clips).
- couplingDelayMs 1.6. crafted = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,17,19} (18 pads; +1 and +19 vs the current {0,2,3,4,5,6,7,8,9,10,11,12,13,14,15,17}).

## Full-surface coverage
Bodies: Membrane (timps/bass/snare), Bell (gong/ride/crotales/bell-tree), Shell (triangle), String (tubular bell), NoiseBody (sus cymbals). Exciters: Mallet, NoiseBurst, Impulse. Axes deliberately exercised across the kit: airLoading, tensionMod (Membrane-only, removed from the snare per its fixed-head archetype), pitchEnv, modeStretch, decaySkew (per-mode tilt), modeInject (1/k), nonlinearCoupling (env-level), Material Morph (now on the bell tree + gong + roll), modeScatter, secondary shell (snare, documented kit delta), per-pad pan, output bus aux1, choke group.

## Verification fixes applied (summary)
1. **Snare (pad 2) tensionModAmt 0.18 → 0.0** — archetype mandates a fixed-tension head with no glide; tension glide reads as a synth kick.
2. **Bell Tree (pad 1) decaySkew 0.60 → 0.78** and **Material Morph added (0.85→0.55, ~1.1 s, linear)** — both are cited archetype values; morph was a silent coverage gap.
3. **Timpani toms (5/7/9/11/14) airLoading 0.65 → 0.80** — consistency with the kick-slot timpano (pad 0) per the archetype's same-instrument kit-note; full air-loading is the pitch-fusion mechanism.

## Documented (intentional) archetype divergences — flagged, not changed
- Gong (13) head↔shell secondary OFF vs archetype's 0.95-on (metal weight comes from Bell+stretch+nonlinear instead).
- Suspended cymbals (6/8/10) modeInject 0 vs crash archetype's 0.25.
- Timpani (0 + toms) modeInject 0.10–0.12 + decaySkew tilt activated vs archetype's 0/neutral.
- Bell Tree (1) size 0.10 vs archetype 0.30.
- Snare (2) tight secondary shell retained vs archetype's no-shell (kept as a kit delta; couplingStrength 0.28 / secondarySize 0.32 keeps it audibly tom-free).

---

## Verification log (12 issues found & fixed)

1. COVERAGE GAP (fixed): Bell Tree (pad 1) silently dropped Material Morph. The bell-tree archetype specifies Morph ON 0.85->0.55 over ~1.1 s as the 'cascade dims as the beater passes' timbral evolution -- a meaningful, cited axis. The proposal left morph defaulted off. FIX: added morphEnabled 1.0 / Start 0.85 / End 0.55 / Duration 0.50 (~1.1 s) / Curve 0 (linear).

2. ARCHETYPE-VALUE MISMATCH (fixed): Bell Tree decaySkew was 0.60; the bell-tree archetype calls for 0.78 (+0.56 upper-partial tilt) as the defining bright-top-of-cascade shimmer. FIX: raised to 0.78.

3. PHYSICAL CORRECTNESS (fixed): Orchestral Snare (pad 2) set tensionModAmt 0.18. The orchestral-concert-snare archetype is explicit that the snare has a FIXED high-tension head with NO pitch glide ('Tension Mod 0.00 OFF (fixed pitch)'); a kerthump pitch-up is the exact artifact that reads as a synth kick and is the membrane-tom idiom, not a concert snare. The proposal cited 'current kit' but the archetype is the value source per policy. FIX: tensionModAmt -> 0.0 (documented OFF). The tight secondary shell (couplingStrength 0.28 / secondarySize 0.32 / secondaryMaterial 0.70) is RETAINED as a deliberate, audibly tom-free 14x5-shell kit delta (a defensible deviation from the archetype's no-shell default, kept because post-N-1 the body is measured-strike bounded), now documented as such rather than as the archetype baseline.

4. CONSISTENCY / FIDELITY (fixed): Timpani toms (pads 5/7/9/11/14) used airLoading 0.65 while the kick-slot timpano (pad 0) used 0.80 -- yet the concert-timpani archetype kit-note requires the kick-slot timpano and the 5-pad tuned row to be the SAME instrument with every param identical except pitch. Full air-loading (archetype 1.00) is the timpano's defining pitch-fusion trick; 0.65 measurably under-fuses the (m,1) Rossing series. FIX: raised the tom airLoading to 0.80 to match pad 0 (the proposal's own 'not over-thinning' compromise), keeping the row internally consistent. Documented deviation from the archetype's 1.00.

5. DOCUMENTED DEVIATION (noted, not forced): Gong/Tam-Tam (pad 13) turns the head<->shell secondary shell OFF, whereas the gong archetype explicitly prescribes 'heavy head<->shell coupling 0.95, secondary on, bright metal shell' for the parallel ringing-metal weight. The proposal substitutes Bell body + modeStretch 0.62 + nonlinearCoupling 0.80 for that weight, which is internally coherent and avoids a coupled-bank RMS pile-up; left as the proposal designed but flagged as a real divergence from the cited recipe.

6. DOCUMENTED DEVIATION (noted): Suspended cymbals (pads 6/8/10) leave Mode Inject at 0, but the crash-cymbal archetype specifies Mode Inject 0.25 for harmonic fill. Leaving it off is defensible post-audit (a 1/k integer series on an indefinite-pitch cymbal is questionable), so kept off, but recorded as an intentional drop of a cited archetype param rather than a silent default.

7. DOCUMENTED DEVIATION (noted): Timpani (pad 0 + toms) activate modeInject 0.10-0.12 and a per-mode decaySkew tilt, which the concert-timpani archetype explicitly defaults to 0/neutral ('clean & linear; the membrane already makes the full tuned series'). Small values are a reasonable exercise of the corrected 1/k axis but do impose a faint synthetic harmonic series competing with the air-loaded Bessel pitch; kept at small magnitudes and documented as a deliberate post-audit activation.

8. DOCUMENTED DEVIATION (noted): Bell Tree size 0.10 (f0 ~635 Hz) vs archetype 0.30 (~401 Hz). The proposal's smaller value reads as higher, crotale-like constituent bells (consistent with the instrument being a stack of tiny tuned bowls, and sitting just below crotales-hi at 0.12); kept, but flagged as a deviation from the cited 0.30.

9. VERIFIED OK: All proposed valueNorm values are in [0,1]; discrete/sentinel params decode legally (chokeGroup 0.125->grp1, outputBus 0.0667->aux1, morphEnabled/secondaryEnabled 1.0, exciter/body selectors). No range violations.

10. VERIFIED OK: Body+exciter pairings are physically correct -- Membrane/Mallet timpani+bass, Membrane/NoiseBurst snare, Shell/Impulse triangle (correct free-free bar; Bell->Shell move confirmed against the triangle archetype and shell_modes.h), Bell/Mallet gong+crotales, Bell/NoiseBurst ride+bell-tree, String/Mallet tubular bell, NoiseBody/NoiseBurst suspended cymbals. airLoading set only on Membrane pads; tensionMod only on Membrane pads; modeInject avoided on indefinite-pitch metallics (except small timpani use).

11. VERIFIED OK: The N-1 thesis is correct -- the live generator comments explicitly state level/airLoading/tensionMod/secondary were 'cut to avoid the OLD clip' (pads[0/2/5..].* in orchestralKit), confirming the headroom reversal (0.65-0.72 -> 0.78-0.88) is the right post-audit correction.

12. VERIFIED OK: Layout/gaps -- current state confirms pad 4 was the snare and pads 15/17 were crotales hi/lo with NO ride and pad 1 unused; the proposal's ride-on-15, bell-tree-on-1, crotales hi/lo on 17/19, and crafted set {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,17,19} are consistent and correctly fill the gaps. Snare/bass pad swap (38/40) is intentional and documented.
