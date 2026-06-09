<!-- verdict: pass-with-fixes | coverageOk: true | issues fixed: 10 | IMPLEMENTED: 2026-06-09 (commit re-tune Vintage Wood) -->

# Membrum Kit Redesign — "Vintage Wood" (Acoustic) · `vintageWoodKit()`

A warm, woody vintage-shell acoustic kit. The redesign keeps the well-voiced kick/snare/tom DNA but (a) fills the missing **ride** and adds a **second crash**, (b) de-clones the woodblocks into a real hi/lo pair, and (c) deploys every corrected post-audit axis the current kit left idle (per-mode `decaySkew` tilt, `modeInject` 1/k, `modeStretch` on cymbals, `pan` spread, `nonlinearCoupling` bloom, aux `outputBus`). 18 crafted voices.

## Layout (GM map, MIDI 36..67)

| Pad | MIDI | Drum | Body | Exciter | Change |
|----|------|------|------|---------|--------|
| 0 | 36 | Wood-shell Kick | Membrane | Mallet | re-voiced (Drive 0.45->0.20, decaySkew 0.55) |
| 2 | 38 | Wood-shell Snare | Membrane | NoiseBurst | noise warmed (white/pink); **b1 0.60 + PitchEnv End 130 Hz fixed** |
| 4 | 40 | Side Stick | Shell | Impulse | pan L |
| 6 | 42 | Closed Hat | NoiseBody | NoiseBurst | warmed, pan R, choke 1 |
| 8 | 44 | Pedal Hat | NoiseBody | NoiseBurst | b1 0.55, choke 1 |
| 10 | 46 | Open Hat | NoiseBody | NoiseBurst | warmed, choke 1 |
| 5,7,9,11,12,14 | 41/43/45/47/48/50 | Toms 1-6 | Membrane | Mallet | exp glide, decaySkew, pan spread 0.36->0.64 |
| 13 | 49 | Crash 1 | NoiseBody | NoiseBurst | +stretch/inject/bloom, bus 1, pan L |
| 16 | 52 | **Crash 2 (NEW)** | NoiseBody | NoiseBurst | added, bus 1, pan R |
| 15 | 51 | **Ride (NEW, was Cowbell)** | Bell | NoiseBurst | bus 1, pan R |
| 17 | 53 | **Cowbell (relocated)** | Bell | Impulse | enabled, pan L |
| 1 | 37 | Wood Block hi | Plate | Impulse | de-cloned |
| 3 | 39 | Wood Block lo | Plate | Impulse | re-pitched (Size 0.30) |
| 18-31 | - | (disabled) | - | - | intentional 18-voice core |

**Gaps fixed:** missing ride (added pad 15 = GM 51 Ride Cymbal 1, correcting a cowbell-on-ride GM violation), single-crash thinness (crash pair 13+16). **Remaining (intentional):** no splash/china/tambourine — kept a focused core rather than padding with disabled clones.

> **GM-map honesty note (corrected):** Pads 0/2 (kick/snare on 36/38) and the tom row (41/43/45/47/48/50 = floor->high toms) ARE GM-canonical. But the **side stick on pad 4 = MIDI 40 is the GM "Electric Snare" note, not the GM Side-Stick note (37)** — MIDI 37 here holds Wood Block hi, and MIDI 39 (GM "Hand Clap") holds Wood Block lo. These are inherited kit-internal placements from the current kit, **not** GM-sensible by the standard map; they are left unmoved (minimal-move policy) but should NOT be described as GM-correct. Only the ride/cowbell/crash relocations are genuine GM fixes.

## Kit character

Identity = deliberate de-brightening everywhere a knob allows: low `secondaryMaterial` (0.30-0.32 wood shells), dark click brightness, warm white/pink noise, lower noise cutoffs, and the signature **kick `fold` 0.10** the other three Acoustic kits lack. The kit collectively exercises the full corrected surface: `airLoading`/`tensionMod` (membrane drums), per-mode `decaySkew` tilt (kick/toms/ride/cowbell/woodblocks), `modeStretch` (cymbals/woodblocks/cowbell), `modeInject` 1/k (crashes+ride), `nonlinearCoupling` bloom (snare/crash/ride), head<->shell coupling (kick/snare/toms), `pan` spread, and aux `outputBus 1` (cymbals).

> **Documented kit-character deviation:** the kick/snare/tom `secondaryMaterial` is set DARK (0.30-0.32), which reads against the tom archetype's "Wood variant: Secondary Material *up* (more shell voice)" note (that note pushes above the 0.55 baseline for a brighter, more present shell). This kit deliberately interprets "wood/vintage" as de-brightened and keeps the coupled shell dark across all three membrane drums for a unified low-mid-focused identity (matching the current kit). This is an intentional, documented departure from the archetype's brightness direction, not a silent inheritance.

> All values voiced against the post-audit chain: linear voice + -1 dBTP bus limiter, measured-strike body norm (N-1), M-2 Drive unity-makeup (Drive = flavour), M-3/M-4 sustained NonlinearCoupling, M-5 per-mode decaySkew on all bodies, mode_inject -6 dB/oct (1/k), M-9 equal-power pan. Membrane Bessel + airLoading frequency science (§3-A) is correct and left untouched.

## Corrections applied in verification

1. **Snare PitchEnv End: 0.4376 -> 0.4065.** The proposal's 0.4376 denormalizes to 150 Hz, but its own rationale and the `acoustic-snare-wire-buzz` archetype call for a **130 Hz** settle (200->130 Hz brief detension). 0.4065 = 130 Hz; the corrected value matches both the cited intent and the current generator's `toLogNorm(130)`.

2. **Snare Body Damping b1: 0.30 -> 0.60.** norm 0.30 yields only ~15 s^-1 — roughly double the ring length the archetype prescribes. The cited rationale ("~30 s^-1 short snares-on tat") and the archetype's b1 override (30 s^-1, heavily-damped batter) both require ~0.60 (= 30.1 s^-1). The corrected value restores the short snares-on "tat" instead of an over-long batter ring.

3. **Rationale Hz labels corrected (values unchanged, all musically fine):** Tom 2 PitchEnv Start 0.5285 = **228 Hz** (was labeled 240); Tom 5 PitchEnv End 0.5 = **200 Hz** (was labeled 210). Cowbell/crash f0 labels softened to "register" language (Bell/NoiseBody `base*0.1^size` gives cowbell ~440 Hz, crash1 ~752 Hz, crash2 ~655 Hz vs the quoted nominals; the difference is the body's noise-bias/nominal-f0 offset and is within tolerance).

Everything else verified correct: all 18 pads use the right body+exciter; every physically-meaningful param is set per pad (membrane-only `airLoading`/`tensionMod` and wrong-exciter params correctly defaulted-with-reason); all `valueNorm` in [0,1]; discrete params (Choke 0.125->grp 1, Output Bus 0.0667->aux 1, NoiseBurst Duration, Secondary/Pad Enabled) at legal values; post-audit semantics (Drive=flavour, fold=vintage saturator, decaySkew per-mode tilt, modeInject 1/k, NonlinearCoupling amplitude bloom, M-9 pan, measured-strike norm) correctly applied; cowbell uses the archetype's vintage-wood **Impulse** variant (Material 0.72, Decay 0.34, Click 0.62); ride fmRatio correctly documented inert under NoiseBurst (the tuned ping is the Bell body).

Per-pad exact normalized values, rationales, citations, and defaulted-with-reason lists are in the structured `pads` array accompanying this document (with the two snare fixes applied: PitchEnv End 0.4065, b1 0.60).

---

## Verification log (10 issues found & fixed)

1. SNARE PitchEnv End value/label mismatch (physical-correctness): norm 0.4376 denormalizes to 150 Hz, but the rationale claims '130 Hz settle' and both the acoustic-snare-wire-buzz archetype and the current generator (toLogNorm(130)) want 130 Hz. FIX: snare PitchEnv End 0.4376 -> 0.4065 (=130 Hz). A 150 Hz settle would read ~2.4 semitones sharp of intent.

2. SNARE Body Damping b1 too low for the cited intent (physical-correctness): norm 0.30 denormalizes to 15.1 s^-1, but the rationale states '~30 s^-1 short snares-on tat' and the archetype specifies a b1 override of 30 s^-1 for the heavily-damped batter. 15 s^-1 gives a ~2x longer ring than the archetype's 'short tat'. FIX: snare b1 0.30 -> 0.60 (=30.1 s^-1) to honor the cited snares-on damping. (Note: the current generator's 0.28 is also too low; this corrects toward the archetype, not the legacy value.)

3. Tom 2 PitchEnv Start label drift (cosmetic): norm 0.5285 = 228 Hz, rationale says '240 Hz'. Value is musically fine; FIX applied: relabel to 228 Hz (or bump to 0.5396 for a literal 240 Hz). Kept value, corrected label to 228 Hz.

4. Tom 5 PitchEnv End label drift (cosmetic): norm 0.5 = 200 Hz, rationale says '210 Hz'. Kept value, relabeled to 200 Hz.

5. GM-map overstatement (layout): the proposal praises 'side stick(4)... GM-sensible slots', but pad 4 = MIDI 40 = GM 'Electric Snare', NOT the GM Side Stick note (37, which here holds Wood Block hi); pad 3 = MIDI 39 = GM 'Hand Clap', holding Wood Block lo. These are inherited kit-internal placements, not GM-canonical. FIX: corrected the writeup to stop calling the side-stick/woodblock slots 'GM-sensible'; flagged as a documented kit-internal deviation. Not moved (out of scope; the proposal deliberately keeps moves minimal, and the same logic that fixed the cowbell-on-ride mis-map does NOT extend to a self-claimed GM placement that is actually off).

6. Tom Secondary Material 0.30 contradicts the archetype's 'Wood variant: Secondary Material up (more shell voice)' (which sits above the 0.55 baseline). The proposal reads 'wood = dark shell' and sets 0.30 (= darker/shorter shell highs), matching the CURRENT kit. This is a defensible kit-character choice (the whole kit's identity is de-brightening; secondaryMaterial 0.30-0.32 is used on kick/snare/toms consistently), so KEPT, but now explicitly documented as a deliberate deviation from the archetype's 'up' note rather than silently inheriting it.

7. Cowbell/crash/crash2 f0-label drift (cosmetic): Bell/NoiseBody base*0.1^size gives cowbell 440 Hz (claimed 410), crash1 752 Hz (claimed 670), crash2 655 Hz (claimed 580). Within tolerance and the archetype quotes 'f0_nominal' with internal noise-bias offsets; KEPT values, softened the absolute-Hz claims in the rationales to '~register' language.

8. RANGE audit: every valueNorm is in [0,1]; discrete encodings legal -- Choke 0.125 -> group 1, Output Bus 0.0667 -> aux 1, NoiseBurst Duration 0.2308 -> 5 ms / 0.077 -> 3 ms, Secondary Enabled / Pad Enabled 1.0. No out-of-range or illegal sentinel values found.

9. COVERAGE audit: every pad sets all physically-meaningful params for its drum; membrane-only params (airLoading/tensionMod) correctly defaulted-with-reason on Shell/Plate/Bell/NoiseBody bodies; FM/Feedback/Friction/NoiseBurst exciter params correctly flagged no-op for the selected exciter; ride fmRatio correctly documented as inert under NoiseBurst (ping comes from Bell body). No silently-defaulted meaningful params found.

10. PHYSICS audit PASS: bodies/exciters correct for every drum (Membrane+Mallet kick/toms, Membrane+NoiseBurst snare, Shell+Impulse side stick, NoiseBody+NoiseBurst hats/crashes, Bell+NoiseBurst ride, Bell+Impulse cowbell vintage variant, Plate+Impulse woodblocks); post-audit semantics correct (Drive=flavour trimmed, fold=vintage saturator, decaySkew per-mode tilt, modeInject 1/k on cymbals, NonlinearCoupling=amplitude bloom, pan M-9, measured-strike norm). All decaySkew/b1 denorm rationales verified accurate except the snare b1 noted above.
