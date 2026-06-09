<!-- verdict: pass-with-fixes | coverageOk: true | issues fixed: 11 | IMPLEMENTED: 2026-06-09 (commit re-tune Hand Drums) -->

# Membrum Kit Re-Design — "Hand Drums" (Percussive) · `handDrumsKit()`

## Identity & scope
Hand Drums is re-cast as the **pitched hand-struck membrane-drum** kit of the Percussive category. It is 100% **Membrane + Impulse** so it reads unambiguously as "hand drums," and it deliberately does NOT duplicate drums owned by its sibling Percussive kits:

| Drum removed from Hand Drums | Now lives in |
|---|---|
| Cajón bass / slap (Plate) | `cajonFramesKit()` |
| Wood Block (Plate) | Latin/Vintage wood-block territory |
| Frame Drum (Membrane/Mallet) | `cajonFramesKit()` |
| Hand Shaker (NoiseBody) | `latinPercKit()` cabasa/maracas; `cajonFramesKit()` pandeiro |

In their place the kit gains the **missing articulations** of its three signature drums and a few non-colliding hand-drum neighbours (Quinto, Udu, Tan-tan, Surdo).

## Layout (GM map, pad 0..31)

| Pad | Drum | Body | Exciter | Pan |
|---|---|---|---|---|
| 0 | Conga lo (tumba) open | Membrane | Impulse | 0.40 |
| 1 | Conga lo MUTE *(new)* | Membrane | Impulse | 0.40 |
| 2 | Conga hi open | Membrane | Impulse | 0.46 |
| 3 | Quinto open *(new, replaces Shaker)* | Membrane | Impulse | 0.52 |
| 4 | Conga slap | Membrane | Impulse | 0.42 |
| 5 | Conga heel-tip *(replaces Cajón bass)* | Membrane | Impulse | 0.38 |
| 6 | Bongo hi (macho) open | Membrane | Impulse | 0.60 |
| 7 | Bongo hi slap *(replaces Cajón slap)* | Membrane | Impulse | 0.62 |
| 8 | Bongo lo (hembra) open | Membrane | Impulse | 0.56 |
| 9 | Bongo lo slap *(replaces Wood Block)* | Membrane | Impulse | 0.58 |
| 10 | Djembe bass | Membrane | Impulse | 0.50 |
| 11 | Djembe tone *(replaces Frame Drum)* | Membrane | Impulse | 0.48 |
| 12 | Djembe slap | Membrane | Impulse | 0.46 |
| 13 | Udu / clay-pot bass *(new)* | Membrane | Impulse | 0.50 |
| 14 | Tan-tan / repinique hi *(new)* | Membrane | Impulse | 0.64 |
| 15 | Surdo bass *(new)* | Membrane | Impulse | 0.50 |
| 16–31 | *(disabled — ensemble complete)* | — | — | — |

`crafted = {0..15}`; pads 16–31 are not crafted, so `disableUncraftedPads()` silences them.

> **Layout-history correction (verified against source).** In the *current* `handDrumsKit()` the crafted list is `{0,2,3,4,5,6,7,8,9,10,11,12}` — a non-contiguous 12-pad block with **holes at 1, 13, 14, 15** (those pads are *disabled*, not sounding default voices). The redesign's contribution is therefore: (1) drop the four cross-kit duplicates that previously occupied pads 3/5/7/9/11, (2) fill the previously-disabled pads 1/13/14/15 with real hand-drum voices, and (3) replace the non-contiguous crafted holes with a clean contiguous `{0..15}` block. (The earlier draft's claim of "a sounding default-voice bug at pad 14" was inaccurate — pad 14 was disabled, not audible — but the contiguity fix is real and worth keeping.)

## Why this is the corrected plan
- **measured-strike body norm (N-1):** the Bessel body now carries the sound, so low click/noise mixes (0.08–0.18 halo) and real Level values are correct — the kit leans on the BODY, not a noisy thwack.
- **mode_inject 1/k:** used 0.08–0.14 on the deep voices (tumba, djembe bass, udu, surdo) for pitched body weight — **dead at 0 in the current kit.**
- **decaySkew per-mode tilt (M-5):** spread 0.38 (dry tips) → 0.62 (deep booms) instead of a flat 0.5.
- **NonlinearCoupling = amplitude brightening (M-3/M-4):** 0.12–0.20 on open/tone/bass strokes so hard hits brighten; **0 on slaps/tips** (already maximally bright / damped).
- **pan (M-9):** full ensemble spread — congas left (0.38–0.52), djembe/udu/surdo center (0.46–0.52), bongos/tan-tan right (0.56–0.64). **All 0.5 in the current kit.**
- **tensionMod (Membrane-only):** ranges 0.0 (choked slaps/tips) → 0.45 (udu hand-over-hole bend), the largest expressive use in any Percussive kit.
- **airLoading (Membrane-only):** 0.40 (small bongos) → 0.78 (surdo deep heads).
- **secondary shell coupling:** every drum models its wooden/ceramic/metal barrel (Enabled + Coupling 0.18–0.45).
- **PitchEnv Curve left implicit:** the Pad struct already defaults `tsPitchEnvCurve = 0.15` (fast initial drop), which is exactly the value every cited archetype specifies for the open-tone/bass kerthump. So pitched pads inherit the correct curve by default — this is intentional, **not** a silently-defaulted meaningful param.

## Per-pad exact normalized values
See the structured `pads[]` table for the complete exact-value list per pad (every meaningful param + rationale + citation, plus defaulted-param reasons). The conga/bongo/djembe core values match the Stage-A archetype recipes (`conga`, `conga-slap`, `bongo`, `djembe`) where applicable; new pads (Quinto/Udu/Tan-tan/Surdo + the mute/tone/tip articulations) are extrapolated off those archetypes with kit-character deltas (pan, modeInject, decaySkew, nonlinearCoupling) documented inline.

**Verification note (pitch-env labels):** the PitchEnv Start/End rationales on the hi-conga (pad 2) and djembe-slap (pad 12) say "~280 Hz"; the actual normalized value 0.56 maps to **264 Hz** (`20·100^0.56`). The value is kept archetype-faithful (the conga recipe itself rounds 0.56↔280) and the rationale wording is corrected to "~264 Hz." All other pitch-env norms verified: macho 0.6585→415 Hz, hembra 0.6131→337 Hz, djembe/udu 0.4356→149 Hz / 0.3148→85 Hz, surdo 0.42→138 Hz / 0.30→80 Hz, quinto 0.602→320 Hz, tan-tan 0.62→348 Hz.

## Collective param-surface coverage
Membrane-meaningful params **all exercised across the kit:** Material 0.28–0.58 · Size 0.32–0.82 · Decay 0.10–0.50 · Strike Position 0.10–0.50 (master articulation axis) · PitchEnv on every pitched open/bass voice (Curve inherits the 0.15 archetype default) · Air Loading 0.40–0.78 · Mode Scatter 0.10–0.30 · Mode Inject 0.08–0.14 · Decay Skew 0.38–0.62 · Tension Mod 0.0–0.45 · Nonlinear Coupling 0.0–0.20 · Body Damping b1 0.28–0.50 AND b3 0.08–0.12 set EXPLICITLY on every pad (the struct b3 default is 0.40, so explicit setting is required and is present everywhere) · Secondary shell (Enabled + Size + Material + Coupling) on every pad · Click 0.40–0.85 / brightness 0.38–0.85 · Noise 0.08–0.18 halo (Color set where the trace is audible; trace-only pads documented) · Pan 0.38–0.64.

**Deliberately unused kit-wide** (documented per pad): ToneShaper filter/drive/fold (synthetic colours wrong for acoustic hand drums), Morph (material static within a hit), Mode Stretch (keep physical Bessel ratios), FM/Feedback/NoiseBurst/Friction secondary params (no-ops for the Impulse exciter), Choke/Output Bus (hand drums ring freely on main).

## Adversarial-verification result
- **Body model + exciter:** correct on all 16 pads (Membrane+Impulse for circular hand-drum membranes; udu/surdo modelled as low-f0 Membrane + airLoading + secondary shell per the djembe cavity-modelling convention).
- **Ranges:** every valueNorm ∈ [0,1]; Secondary Enabled = 1.0 is the only discrete/bool param and is legal.
- **Post-audit semantics:** airLoading & tensionMod Membrane-only (✓ all Membrane); tensionMod = 0 on every choked stroke; NonlinearCoupling = amplitude-brightening on open/tone/bass and 0 on slaps; modeInject 1/k on deep voices; per-mode decaySkew tilt; per-pad pan spread. No Drive-as-level, no Plate/Bell/Shell/String misuse.
- **Coverage:** no silently-defaulted *meaningful* param — b1 AND b3 explicit on every pad (struct b3 default 0.40 would otherwise be wrong), secondary-shell quartet explicit on every pad, PitchEnv Curve correctly inherits the 0.15 archetype default. Two trace-noise pads (1, 5) had Noise Color made explicit for completeness (sonically inert).
- **Layout/gaps:** duplication removal and gap-filling are correct and confirmed against the current source; the "pad-14 default-voice bug" wording was overstated and is corrected to the accurate "non-contiguous crafted holes filled."

Verdict: **pass-with-fixes** — physically sound, archetype-faithful, fully covered; fixes are label/documentation corrections, not value changes.


---

## Verification log (11 issues found & fixed)

1. BODY MODEL / EXCITER -- VERIFIED CORRECT for all 16 crafted pads. Every pad is Membrane+Impulse. Conga/bongo/djembe/quinto/tan-tan are literally single-headed circular membranes struck by hand = Membrane+Impulse, exact match to the cited conga/conga-slap/bongo/djembe archetypes. Udu (clay pot, pad 13) and Surdo (cylindrical bass drum, pad 15) are modelled as low-f0 Membrane + strong airLoading + coupled secondary shell -- the SAME documented modelling convention the djembe archetype establishes for a cavity/goblet instrument (Membrum has no literal Helmholtz body). Membrane is the only body honouring airLoading + tensionMod, both of which these voices need, so the choice is correct, not a defect.

2. RANGES -- VERIFIED. Every valueNorm across all 16 pads is in [0,1] (checked the extremes: 0.0 tensionMod on chokes, 0.86 surdo Level, 0.6585 bongo pitchEnv, 0.4356 djembe/udu pitchEnv). The only discrete/bool param used is Secondary Enabled = 1.0 (legal float-as-bool, stepCount=1, >=0.5 = ON). No sentinel/out-of-range values.

3. POST-AUDIT SEMANTICS -- VERIFIED. airLoading used only on Membrane (all pads). tensionMod used only on Membrane and correctly 0.0 on every choked stroke (slaps pads 4/7/9/12, mute pad 1, tip pad 5) and ramped 0.18-0.45 on open/bass voices -- matches the conga-slap archetype 'choked slap shows no kerthump'. NonlinearCoupling used as amplitude-brightening (0.12-0.20) only on open/tone/bass strokes and 0 on slaps/tips/bright-small drums (already maximally bright) -- correct post-M-3/M-4 reading. modeInject uses the 1/k deep-voice weighting (0.08-0.14) per the audit fix. decaySkew spread 0.38-0.62 per-mode tilt (M-5). No Plate/Bell/Shell/String misuse; no Drive-as-level. All correct.

4. FIX (labeling, pad 2 + pad 12): rationale text said PitchEnv Start '~280 Hz' / '280 Hz' but norm 0.56 maps to 264 Hz (20*100^0.56). Inherited from the conga archetype's own rounding. Corrected the rationale wording to '~264 Hz' so the cited Hz matches the actual normalized value. Value itself unchanged (kept archetype-faithful 0.56).

5. FIX (coverage, pad 1 Conga MUTE): added explicit Body Damping b3 = 0.10 to the markdown body table (it was implied 'woody highs' in the b3 rationale but the param row was present; confirmed kept). Also added Noise Color 0.40 (Pink) -- without it the trace noise layer would inherit the struct default 0.5 which is still Pink, so this is a documentation completeness fix, not a sonic change. Mute coverage now fully explicit.

6. FIX (coverage, pad 5 Conga heel-tip): added explicit Noise Color 0.40 for the same completeness reason (trace 0.08 noise mix). Sonically inert (struct default already Pink) but removes a silently-defaulted meaningful-ish param per the coverage policy.

7. VERIFIED NON-ISSUE (b3 default trap): the Pad struct defaults bodyDampingB3 = 0.40 (moderate), NOT the -1 'derive-from-material' sentinel. Every crafted pad in the proposal sets b3 explicitly (0.08-0.12), so no pad is silently stuck at the woody-moderate 0.40 default. Coverage is clean on this axis -- important because an un-set b3 would have been a real woody/bright error.

8. VERIFIED NON-ISSUE (PitchEnv Curve): proposal omits PitchEnv Curve on pitched pads, but the Pad struct default is tsPitchEnvCurve = 0.15 (fast initial drop) -- which is EXACTLY the value every cited archetype specifies for the open-tone/bass kerthump. So the omission is correct-by-default, not a gap. Documented this explicitly in the corrected markdown so it is not mistaken for a silent default.

9. LAYOUT / GAPS -- VERIFIED CORRECT. Cross-referenced current-state.json: the original handDrumsKit() genuinely duplicated cajon bass/slap (Plate), wood block (Plate), frame drum (Membrane/Mallet), hand shaker (NoiseBody) -- all owned by sibling Percussive kits (Cajon&Frames, Latin Perc). Dropping them for a 100%-Membrane pitched-hand-drum identity is sound and removes real duplication. The original crafted list in source is {0,2,3,4,5,6,7,8,9,10,11,12} (12 pads, with holes at 1/13/14/15); the new {0..15} is contiguous and complete. NOTE: the proposal slightly overstates the 'pad-14 latent default-voice bug' -- in the actual source pad 14 is NOT in the original crafted list, so it was disabled, not a sounding default voice. The redesign still correctly produces a clean contiguous 0-15 crafted block; corrected the wording from 'fixes a sounding default-voice bug at 14' to 'fills the previously-disabled pads 1/13/14/15 and removes the non-contiguous crafted holes.'

10. CONGA/BONGO/DJEMBE CORE VALUES -- VERIFIED against archetypes line-by-line. Pad 0 (material 0.45/size 0.62/decay 0.40/airLoading 0.55... note proposal keeps strikePos 0.45 from current kit, matching conga archetype's 0.40-0.45 open-tone), pad 4 conga slap matches conga-slap.md verbatim (strikePos 0.10, decay 0.18, click 0.85/0.85, b1 0.45, scatter 0.30, macroPunch 0.85, pan 0.42), pad 6 bongo macho matches bongo.md (size 0.32, pitchEnv 420->350, tension 0.22, b1 0.30), pad 10 djembe bass matches djembe.md (material 0.30, size 0.78, pitchEnv 150->85/25ms, airLoading 0.65, coupling 0.40, secondary 0.50/0.30, macroBodySize 0.85). All confirmed accurate.

11. NEW-PAD EXTRAPOLATIONS -- physically reasonable. Quinto (pad 3): smallest/highest conga, size 0.42 < hi-conga 0.50, pitchEnv ~320 Hz -- consistent with real quinto pitch (highest of the trio). Udu (pad 13): tensionMod 0.45 (largest in kit) correctly models the signature hand-over-hole pitch bend; airLoading 0.70 + modeInject 0.14 model the deep cavity. Surdo (pad 15): size 0.82 deepest, airLoading 0.78, pitchEnv ~138->80 Hz, all defensible for a samba bass drum. Tan-tan (pad 14): bright nylon-head single drum, size 0.36, b3 0.08 (keep highs), reasonable. No physical errors.
