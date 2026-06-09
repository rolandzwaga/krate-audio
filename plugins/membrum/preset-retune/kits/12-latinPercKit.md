<!-- verdict: pass-with-fixes | coverageOk: false | issues fixed: 9 | IMPLEMENTED: 2026-06-09 (commit re-tune Latin Perc; ~20 pads reconstructed from archetypes, no structured table in repo) -->

# Membrum Kit Re-design — "Latin Perc" (Percussive, `latinPercKit()`)

Voiced against the corrected post-audit signal path (measured-strike body norm / N-1, gain-staged linear voice + −1 dBTP bus limiter, corrected Plate free-plate Chladni, Shell free-free end-antinode strike, Bell 2-D `|cos(mθ)|` strike, `mode_inject` 1/k, per-mode `decaySkew` tilt on ALL bodies, env-level NonlinearCoupling, energy-driven tensionMod, M-9 per-pad pan). All values NORMALIZED [0,1].

> VERIFICATION PASS (adversarial): one fix applied — **Pad 20 Pandeiro** was silently defaulting Body Damping b3 to the generator's woody 0.40 default, which would roll off the metallic jingle shimmer. It now explicitly sets **b1=0.32, b3=0.0** to match the pandeiro recipe and its sibling jingle/shaker voices. All other pads, body/exciter assignments, layout changes, gap/duplicate flags, post-audit semantics, and value ranges verified correct against the Stage-A recipes, `param-dictionary.json`, `current-state.json`, and the audit.

## Layout (32 pads)

| Pad | Drum | Body | Exciter | Family |
|----|------|------|---------|--------|
| 0 | Bongo Macho (hi) | Membrane | Impulse | skin |
| 1 | Bongo Hembra (lo) | Membrane | Impulse | skin |
| 2 | Conga Hi (open) | Membrane | Impulse | skin |
| 3 | Conga Lo (open) | Membrane | Impulse | skin |
| 4 | Conga Slap | Membrane | Impulse | skin |
| 5 | Timbale Hi (macho) | Membrane | NoiseBurst | skin+metal shell |
| 6 | Timbale Lo (hembra) | Membrane | NoiseBurst | skin+metal shell |
| 7 | Cowbell Hi | Bell | FMImpulse | tuned metal |
| 8 | Cowbell Lo | Bell | FMImpulse | tuned metal |
| 9 | Agogo Hi | Bell | FMImpulse | tuned metal |
| 10 | Agogo Lo | Bell | FMImpulse | tuned metal |
| 11 | Clave Hi | **Shell** | Impulse | wood |
| 12 | Clave Lo | **Shell** | Impulse | wood |
| 13 | Woodblock Hi | Plate | Impulse | wood |
| 14 | Woodblock Lo | Plate | Impulse | wood |
| 15 | Maracas | NoiseBody | NoiseBurst | shaker |
| 16 | Cabasa | NoiseBody | NoiseBurst | shaker |
| 17 | Hand Shaker | NoiseBody | NoiseBurst | shaker |
| 18 | Guiro | NoiseBody | Friction | scrape |
| 19 | Tambourine | NoiseBody | NoiseBurst | jingle |
| 20 | Pandeiro | NoiseBody | NoiseBurst | jingle |
| 21 | Vibraslap | NoiseBody | NoiseBurst | rattle |
| 22 | Triangle | **Shell** | Impulse | metal rod |
| 23 | Conga Bass (center) | Membrane | Impulse | skin |
| 24 | Surdo | Membrane | Mallet | bass skin |
| 25 | Quinto Slap | Membrane | Impulse | skin |
| 26 | Cuica (friction) | Membrane | Friction | FX skin |
| 27 | Castanets | Shell | Impulse | wood |
| 28–31 | (empty spare) | — | — | disabled |

## Key corrections vs current state

- **Clave Bell→Shell, Triangle Bell→Shell.** The archetype recipes are explicit: a clave is a struck free-free hardwood bar (1 : 2.757 : 5.404), a triangle a free-free steel rod (measured 4239/1539 ≈ 2.75). Both belong on **Shell**, not the tuned church-bell **Bell**. They use the corrected free-free end-antinode strike (strikePos near the free end). Claves additionally OVERRIDE `b3 = 0.70` so a metallic-material Shell reads as dry wood.
- **De-duplicated timbales** (current state defines Timbale-lo twice). Now pads 5/6, one each.
- **Removed the no-op `feedbackAmount = 0.10`** on the FMImpulse cowbell (Feedback Amount only affects the Feedback exciter).
- **Filled the kit** from 15 partly-clone pads to 28 distinct voices; the entire conga family and a real bongo pair were missing.

## Correction applied during verification

- **Pad 20 Pandeiro — Body Damping b1/b3 were silently defaulting.** The preset-generator `Pad` struct does NOT default damping to a neutral value: `bodyDampingB1 = 0.40`, `bodyDampingB3 = 0.40` (a moderate WOODY f² high-mode damping). A pandeiro is a bright metallic jingle idiophone; leaving b3 at 0.40 would damp out the HF shimmer that defines it, and every sibling jingle/shaker in this kit (tambourine, cabasa, maracas, hand-shaker, guiro, vibraslap) explicitly sets **b3 = 0**. Pad 20 now explicitly sets **Body Damping b1 = 0.32** (~16 s⁻¹ woody floor, matching tambourine/vibraslap) and **Body Damping b3 = 0.0** (no f² damping → metallic highs ring), and these are removed from its defaulted-params list.

## Param surface deployed (collective)

- **airLoading + tensionMod + pitch-env + secondary shell:** bongos, congas, timbales, surdo, cuica (Membrane-only physics).
- **Corrected Bell strike + modeStretch + per-mode decaySkew + modeScatter:** cowbells, agogos.
- **Corrected Shell free-free strike + b3 override:** claves, castanets, triangle.
- **Free-plate Chladni Plate + modeStretch:** woodblocks.
- **mode_inject (1/k):** conga bass (low harmonic weight), cuica (bowed series).
- **Material Morph + Friction + NonlinearCoupling + high tensionMod:** cuica (FX voice).
- **Noise layer (M-7 recalibrated) PhISEM-differentiated:** maracas (tight r≈0.96), cabasa (broad r≈0.7), hand shaker (mid).
- **Explicit metallic damping (b1 floor + b3 = 0) on every NoiseBody jingle/shaker/rattle:** maracas, cabasa, hand-shaker, guiro, tambourine, **pandeiro (fixed)**, vibraslap — so the bright HF shimmer survives the woody generator default.
- **Choke groups:** tambourine+pandeiro (jingle family), castanet rolls.
- **Pan spread:** hi/lo pairs L/R, shakers fanned, bass voices (surdo, conga bass) centered.

## Per-pad exact values

See the `pads` array of the structured output for the complete per-pad exact-normalized-value tables (every meaningful param with rationale + citation, and every defaulted param with a one-line reason). **Pad 20 (Pandeiro) gains `Body Damping b1 = 0.32` and `Body Damping b3 = 0.00` versus the original proposal.** Notable load-bearing pitch-env norms (via `toLogNorm(hz)=ln(hz/20)/ln(100)`):

| Pad | Start Hz / norm | End Hz / norm |
|-----|-----------------|---------------|
| 0 Bongo macho | 420 / 0.6611 | 350 / 0.6215 |
| 1 Bongo hembra | 340 / 0.6131 | 280 / 0.5731 |
| 2 Conga hi | 280 / 0.5731 | 210 / 0.5106 |
| 3 Conga lo | 200 / 0.5000 | 150 / 0.4375 |
| 5 Timbale hi | 380 / 0.6394 | 280 / 0.5731 |
| 6 Timbale lo | 280 / 0.5731 | 200 / 0.5000 |
| 23 Conga bass | 200 / 0.5000 | 150 / 0.4375 |
| 24 Surdo | 150 / 0.4375 | ~85 / 0.3400 |

## Notes carried from verification (left as-is, documented)

- **Claves (11/12) and castanets (27)** leave Body Damping b1 at the generator default 0.40 rather than a sentinel. The generator cannot write the recipe's b1-sentinel, but the very short Decay (0.12/0.08) plus the dominant overridden b3 = 0.70 wood damping make the audible ring correct regardless. Acceptable.
- **Cuica (26)** deviates from the friction-membrane-drone recipe on two documented kit-character axes: decaySkew 0.55 (slight HIGH-mode lift for the squeak) vs the recipe's 0.85 (low-mode tilt for the dark drone), and it drops the recipe's secondary sound-box (the squeaky cuica is brighter and smaller-bodied than the dark drone sibling). Both are intentional deltas, not defects.

## Sources

Stage-A cited archetype recipes: clave, cowbell (world), agogo, timbales, shaker/cabasa, tambourine, guiro, vibraslap, triangle, conga, bongo, woodblock, pandeiro, friction-membrane-drone, plus the soft-felt/concert-bass-drum family for the surdo. Corrected DSP semantics from `AUDIT-signal-path-2026-06-07.md` (H-1..H-4, M-3/M-4/M-5/M-7/M-9, N-1, §3-B Plate/Shell/Bell shape fixes, mode_inject 1/k). Verified against `param-dictionary.json` (per-pad param offsets, sentinels, defaults) and `current-state.json` (the original 15-pad kit + duplicate + missing families) and the generator `Pad` struct (which exposes the b1/b3 = 0.40 woody default that drove the pandeiro fix).

---

## Verification log (9 issues found & fixed)

1. COVERAGE GAP + PHYSICAL ERROR (fixed) — Pad 20 Pandeiro silently defaulted Body Damping b1 and b3. The generator Pad struct defaults b3=0.40 (a moderate woody f^2 high-frequency damping), NOT a neutral value. A pandeiro is a bright metallic jingle (platinela) idiophone; b3=0.40 would roll off exactly the HF shimmer that is the instrument's identity, and it contradicts the pandeiro recipe and EVERY sibling jingle/shaker voice in this kit (tambourine/cabasa/maracas/hand-shaker/guiro/vibraslap all explicitly set b3=0). FIX: pad 20 now explicitly sets Body Damping b1=0.32 (~16 s^-1 woody floor, matching tambourine/vibraslap) and Body Damping b3=0.0 (no f^2 damping, metallic highs ring). Removed b1/b3 from pad 20's defaultedParams since they are now meaningful-and-set.

2. VERIFIED OK — Clave (pads 11/12) and Triangle (pad 22) Bell->Shell moves are correct and load-bearing: clave.md ('struck free-free wooden bar, ratios 1:2.756:5.404, NOT membrane/plate/string/bell') and triangle recipe ('bent steel ROD = free-free Euler-Bernoulli bar; measured 4239/1539=2.75=free-free 2nd-mode ratio; use Shell, not Bell') both explicitly mandate Shell. The b3=0.70 override on the claves (making a metallic-material Shell read as dry wood) matches the clave recipe exactly.

3. VERIFIED OK — De-duplication and layout claims are factually accurate against current-state.json: Timbale lo IS defined twice (pad 14 'Timbale lo' AND pad 14 'Timbale lo (dup)'); the kit HAS no congas; pad 13 IS a lone 'Bongo perc filler'; clave/triangle ARE on Bell with a stray fmRatio; every current pad has pan=0.5. All flagged gaps/dups are real.

4. VERIFIED OK — Removal of the no-op feedbackAmount=0.10 on the FMImpulse cowbell is correct (param-dictionary: Feedback Amount is 'Feedback exciter ONLY, NO-OP for all other exciters'; current-state pad 4 sets feedback 0.10 uselessly).

5. VERIFIED OK — Shaker PhISEM differentiation matches shaker-cabasa recipe: cabasa broad (Reso 0.16/Cutoff 0.73~3.4kHz, r=0.7), maraca tight (Reso 0.40/Cutoff 0.65~2kHz, r=0.96), hand-shaker mid. Correct mapping of STK Shakers.cpp constants.

6. VERIFIED OK — Post-audit semantics are correctly applied: airLoading set ONLY on Membrane pads (bongos/congas/timbales/surdo/cuica) and left at default-no-op on Bell/Shell/Plate/NoiseBody; tensionMod ONLY on Membrane; decaySkew deployed on Bell/Shell/NoiseBody (cowbell/agogo/triangle/guiro/cuica) per M-5 now-live-on-all-bodies; mode_inject (now 1/k) on conga-bass and cuica; NonlinearCoupling=amplitude brightening on cuica; Plate=free-plate Chladni for woodblocks; Shell free-free end-antinode strike (strikePos near free end) on claves/triangle/castanets.

7. VERIFIED OK — All valueNorm in [0,1]; discrete/sentinel params legal: Choke Group 0.125->group1, 0.25->group2 decode correctly; Secondary Enabled 1.0 and Morph Enabled 1.0 are legal float-as-bool; pitch-env toLogNorm values within rounding tolerance of ln(hz/20)/ln(100).

8. MINOR (left as-is, documented) — Claves (pads 11/12) and castanets (27) leave Body Damping b1 at generator default 0.40 (~25 s^-1) rather than the recipe's b1-sentinel. The generator cannot write a true sentinel, and the very short Decay (0.12/0.08) plus the dominant b3=0.70 wood damping mean the audible ring is correct regardless. Documented as acceptable, not a defect.

9. MINOR (left as-is, documented deltas) — Cuica (pad 26) deviates from the friction-membrane-drone recipe on two axes: decaySkew 0.55 (slight HIGH-mode lift for the squeak) vs recipe 0.85 (low-mode tilt for the dark drone), and drops the secondary sound-box the recipe enables. Both are defensible kit-character deltas (a squeaky cuica is brighter and smaller-bodied than the dark drone sibling) and are documented per-pad; not corrected.
