<!-- verdict: pass-with-fixes | coverageOk: true | issues fixed: 7 | IMPLEMENTED: 2026-06-09 (commit re-tune Jazz Brushes) -->

# Membrum Kit Re-Design — "Jazz Brushes" (Acoustic) — `jazzBrushesKit()`

Corrected, post-audit (2026-06-07) plan. All values NORMALIZED [0,1] (on-wire/preset form). Pads 21-31 stay deliberately disabled.

## Kit identity
The softest Acoustic kit: suppressed transients, brushed (not cracked) snares, felt-beater kick, soft mallet toms, a bow-led ride. It exercises the **noise / friction / morph** surface (brush sweep, brush swirl, cymbal swell) instead of the click/coupling surface the rock kits lean on. Loudness deliberately restrained so the measured-strike body norm + bus limiter keep dynamics intact.

## Layout (GM-sensible; flagged deviations)
Pad index -> MIDI = 36 + index (GM drum map). GM note names below are the TRUE GM names for each MIDI number; deviations from the GM role are flagged.

| Pad | MIDI | GM note | Drum (this kit) | Body | Exciter |
|----|----|----|----|----|----|
| 0 | 36 | Bass Drum 1 | Soft Felt Kick | Membrane | Mallet |
| 1 | 37 | Side Stick | Wood Block (jazz click)* | Plate | Impulse |
| 2 | 38 | Acoustic Snare | Brush Snare (sweep) | Membrane | NoiseBurst |
| 3 | 39 | Hand Clap | **Brush Swirl / Buzz-Roll** (NEW) | Membrane | Friction |
| 4 | 40 | Electric Snare | Brush Snare (tap) | Membrane | NoiseBurst |
| 5,7,9,11,12,14 | 41,43,45,47,48,50 | Toms/Floor Toms | Toms 1-6 (size-graded row) | Membrane | Mallet |
| 6,8,10 | 42,44,46 | Closed/Pedal/Open HH | Closed / Pedal / Open Hat (choke 1) | NoiseBody | NoiseBurst |
| 13 | 49 | Crash Cymbal 1 | Ride (bow)** | Bell | NoiseBurst |
| 15 | 51 | Ride Cymbal 1 | Crash** | NoiseBody | NoiseBurst |
| 16 | 52 | Chinese Cymbal | **Ride Bell / Cup (FM)** (NEW)*** | Bell | FMImpulse |
| 17 | 53 | Ride Bell | **Splash** (NEW)*** | NoiseBody | NoiseBurst |
| 18 | 54 | Tambourine | **Cymbal Swell** (NEW, Morph)*** | NoiseBody | NoiseBurst |
| 19 | 55 | Splash Cymbal | **Side Stick / Cross-Stick** (NEW)* | Shell | Impulse |
| 20 | 56 | Cowbell | **Shaker (cabasa)** (NEW)*** | NoiseBody | NoiseBurst |
| 21-31 | 57-67 | (various) | disabled (focused set) | — | — |

\* **Flagged GM deviation (click colors):** Wood Block kept on the GM Side-Stick slot (pad 1) and a dedicated Side Stick added on pad 19 (the GM *Splash* slot), so the kit keeps two distinct "click" colors. Both are intentional GM-role deviations.

\*\* **Flagged inherited GM swap (ride<->crash):** the crafted kit seats the bow Ride on pad 13 (= MIDI 49 = GM *Crash 1*) and the Crash on pad 15 (= MIDI 51 = GM *Ride 1*) — i.e. ride and crash sit on each other's GM slots. This is inherited from the current `jazzBrushesKit()` and kept (re-seating the whole crafted layout is out of scope), but it is now honestly flagged rather than disguised with mislabeled MIDI numbers.

\*\*\* **Flagged GM-role deviations (cymbal/perc family):** the FM ride-bell (pad 16), splash (17), cymbal swell (18) and shaker (20) are placed on the GM Chinese-Cymbal / Ride-Bell / Tambourine / Cowbell slots respectively. The placements are musically sensible (a clustered cymbal+hand-perc family) but do not match the literal GM role of those notes; all four are intentional deviations.

## Kit globals (NEW — currently all 0)
`maxPolyphony 10 · globalCoupling 0.30 · snareBuzz 0.30 · tomResonance 0.40 · couplingDelayMs 1.0 · masterGainNorm 0.5`. These make the per-pad `couplingAmount` values audible (sympathetic snare-buzz + tom-resonance network).

## Per-pad highlights (full exact tables in the structured `pads[]` payload)

### Pad 0 — Soft Felt Kick (Membrane/Mallet)
`material 0.45 · size 0.72 · decay 0.32 · level 0.78 · pitchEnv 140->60 Hz / 25 ms exp · airLoading 0.78 · b1 0.40 b3 0.10 · click 0.28 (contact 0.40, bright 0.22 dull) · noise 0.06 · coupling 0.30 + secondary(0.40/0.55) · tensionMod 0.12 · pan 0.50` — felt-beater suppressed transient (soft-felt-beater-kick-jazz.md).

### Pad 1 — Wood Block (Plate/Impulse)
`material 0.60 · size 0.22 · decay 0.20 · modeStretch 0.50 (inharmonic free-bar) · click 0.70 / bright 0.75 · noise 0.05 (near-off; archetype is noise-OFF, kept just-audible) · b1 0.50 b3 0.10 · pan 0.40` (woodblock-perc.md).

### Pad 2 — Brush Snare sweep (Membrane/NoiseBurst)
`burstDur 0.85 (long smeared) · click 0.0 (no crack) · noise 0.75 / LP 0.62 / decay 0.55 (the brush bed) · HP filter 0.25 + filter-ADSR (envAmt 0.30, dec 0.20, sus 0.30, rel 0.40) · drive 0.20 (warmth, not level) · morph 0.55->0.30 / 0.35 · coupling 0.60 + secondary(0.65/0.45) · tensionMod 0.20 · couplingAmount 0.65 · pan 0.50` (brush-snare-sweep.md). frictionPressure 0.60 set per brief but INERT under NoiseBurst.

### Pad 3 — Brush Swirl (Membrane/**Friction**) — NEW
`material 0.40 · size 0.58 · decay 0.70 · frictionPressure 0.45 (LIVE) · modeInject 0.18 (1/k bowed series) · nonlinearCoupling 0.45 · decaySkew 0.85 (low-tilt) · tensionMod 0.30 · airLoading 0.50 · static LP 0.55 · click 0.0 · noise 0.20 pink long · coupling 0.18 + secondary · pan 0.45` (friction-membrane-drone…md). The kit's only Friction/modeInject voice. (Note: the filter ADSR is left inert — envAmt 0.5 = no sweep — so the LP is a static dark bed; the archetype's optional sustained filter-ADSR is not exercised here.)

### Pad 4 — Brush Snare tap (Membrane/NoiseBurst)
`burstDur 0.231 (~5 ms) · click 0.62 soft / bright 0.60 · noise 0.65 / 0.72 white / decay 0.28 · LP 0.78 envAmt 0.65 · drive 0.22 (warmth) · coupling 0.62 + secondary(0.68/0.48) · tensionMod 0.25 · pitchEnv 210->150/60 ms · pan 0.52` (brush-snare-tap-snare.md).

### Pads 5/7/9/11/12/14 — Tom row (Membrane/Mallet)
size 0.72->0.36, material 0.40->0.60, decay 0.45->0.24, b1 0.30->0.42, pitchEnv 200/110 -> 470/290 Hz / ~50 ms; airLoading 0.65->0.45; **NEW graded axes:** decaySkew 0.46->0.54, modeScatter 0.10->0.15, pan 0.38->0.60 L->R. Soft jazz voicing: tensionMod 0.18, click 0.40 soft, noise 0.15 (acoustic-rack-floor-tom…md).

### Pads 6/8/10 — Hats (NoiseBody/NoiseBurst, choke group 1)
Closed: `decay 0.07 · b1 0.60 · noise 0.85 / 0.78 warm / white 0.65 · scatter 0.70 · pan 0.40`. Pedal: `decay 0.05 · b1 0.70 (chunky shut)`. Open: `decay 0.55 · b1 0.55 b3 0.20 · noise 0.55 / 0.92 / decay 0.78 · scatter 0.85` (closed/pedal/open-hi-hat-acoustic.md). Warmer cutoff/white color = jazz hat.

### Pad 13 — Ride bow (Bell/NoiseBurst)  *(GM Crash-1 slot; see ** above)*
`material 0.92 · size 0.42 · strikePos 0.18 (near soundbow antinode -> full partial set) · decay 0.85 · modeStretch 0.45 · decaySkew 0.62 (shimmer lift) · scatter 0.62 (single clean value — fixes current 0.28->0.85 double-assign) · b1 0.16 b3 0.0 (metallic; was 0.30, corrected) · noise 0.30 / 0.85 / 0.75 · click 0.45 ping / bright 0.85 · nonlinearCoupling 0.18 · outputBus 1 · pan 0.60` (ride-cymbal-bell-bow-cymbal.md). fmRatio 0.35 + feedbackAmount 0.05 INERT under NoiseBurst (documented vestige).

### Pad 15 — Crash (NoiseBody/NoiseBurst)  *(GM Ride-1 slot; see ** above)*
`material 0.92 · size 0.32 · decay 0.65 · modeStretch 0.55 · modeInject 0.20 · nonlinearCoupling 0.30 (bloom) · decaySkew 0.60 · scatter 0.60 (single clean value; fixes current 0.55->0.85 double-assign) · noise 0.55 / 0.78 warm / white 0.62 · outputBus 1 · pan 0.58` (crash-cymbal-cymbal.md).

### Pad 16 — Ride Bell/Cup FM (Bell/**FMImpulse**) — NEW  *(GM Chinese-Cymbal slot)*
`material 0.90 · size 0.34 · decay 0.70 · fmRatio 0.30 (LIVE clang, modRatio 1.9 Chowning) · modeStretch 0.45 · decaySkew 0.60 · nonlinearCoupling 0.15 · scatter 0.35 · b1 0.18 b3 0.0 · click 0.40 / bright 0.82 · noise 0.12 sheen · outputBus 1 · pan 0.62` (fm-bell-percussion-synthetic-bell.md). The kit's genuine FM voice (the only pad where fmRatio is audible).

### Pad 17 — Splash (NoiseBody/NoiseBurst) — NEW  *(GM Ride-Bell slot)*
`material 0.95 · size 0.22 · strikePos 0.35 · decay 0.28 · modeStretch 0.55 · decaySkew 0.58 · scatter 0.50 · b1 0.30 b3 0.0 · noise 0.55 / 0.92 violet / decay 0.25 · click 0.30 / bright 0.85 · burstDur 0.40 · bright macro 0.70 · outputBus 1 · pan 0.66` (splash-cymbal…md).

### Pad 18 — Cymbal Swell (NoiseBody/NoiseBurst, Morph) — NEW  *(GM Tambourine slot)*
`material 0.90 · size 0.40 · strikePos 0.90 (edge strike, broad mode set) · decay 0.85 · morph 0.55->0.95 / 0.85 (slow bloom) · modeStretch 0.55 · decaySkew 0.55 · **nonlinearCoupling 0.45 (energy-cascade swell brightening — the archetype's primary swell lever)** · scatter 0.65 · click 0.0 · noise 0.65 / 0.85 / decay 0.90 · b1 0.22 b3 0.0 · outputBus 1 · pan 0.55` (suspended-cymbal…md). Kit's 2nd Morph user.

### Pad 19 — Side Stick (Shell/Impulse) — NEW  *(GM Splash slot)*
`material 0.30 · size 0.20 · strikePos 0.30 · decay 0.16 · b1 0.42 b3 0.10 · scatter 0.50 · click 0.88 / bright 0.62 woody · noise 0.0 · pan 0.50 (center, replaces the snare backbeat)` (side-stick-cross-stick-perc.md).

### Pad 20 — Shaker cabasa (NoiseBody/NoiseBurst) — NEW  *(GM Cowbell slot)*
`material 0.85 · size 0.08 · decay 0.08 · noise 0.85 / 0.73 / reso 0.16 / white 0.75 / decay 0.12 · click 0.0 · burstDur 0.20 · b1 0.55 b3 0.0 · pan 0.64` (shaker-cabasa…md).

## Param-surface coverage (kit collectively)
- **Exciters:** Mallet (kick/toms), NoiseBurst (snares/hats/cymbals/shaker), Impulse (woodblock/side-stick), Friction (swirl), FMImpulse (ride bell). *(Feedback intentionally absent — no self-oscillating voice fits a jazz brush kit; flagged.)*
- **Bodies:** Membrane, NoiseBody, Plate, Bell, Shell. *(String absent — no waveguide voice in this acoustic set; flagged.)*
- **Dead axes now live:** pan (full kit spread), decaySkew (cymbals up / toms low / swirl low), modeStretch (cymbals/ride/woodblock/splash/FM-bell), modeInject 1/k (swirl only), Material Morph (sweep + swell), Friction + tensionMod + nonlinearCoupling (swirl + crash + swell + ride + FM-bell), head<->shell secondary coupling (kick/snares/toms/swirl), kit-global sympathetic coupling, strikePosition now set where it carries timbre (ride 0.18, splash 0.35, swell 0.90, side-stick 0.30, woodblock 0.30).

## Delta from current
15->18 crafted pads; pan/decaySkew/modeStretch/modeInject now deliberately voiced; tom row gains graded skew/pan/scatter; ride double-assign cleaned (modeScatter 0.62, b3 0.0) + FM cup ride split off; crash gains stretch/bloom/skew; kit globals dialed in. Verification fixes applied on top of the proposal: corrected all GM note labels, flagged the inherited ride<->crash GM swap, added ride strikePos 0.18, added swell nonlinearCoupling 0.45 + strikePos 0.90, removed the unsupported "filter-ADSR sustain" coverage claim for pad 3.

---

## Verification log (7 issues found & fixed)

1. LAYOUT / GM labels WRONG (documentation): the proposal's pad-index->MIDI arithmetic is correct (+36) but the GM *names* attached to many MIDI numbers are wrong. Real GM: 49 Crash1, 51 Ride1, 52 Chinese, 53 Ride Bell, 54 Tambourine, 55 Splash, 56 Cowbell, 57 Crash2, 59 Ride2. The proposal labeled pad16/MIDI52 as a free slot but it is GM Chinese Cymbal; pad17/MIDI53 as 'Crash 2' (it is GM Ride Bell); pad18/MIDI54 as 'Ride 2 cup' (it is GM Tambourine); pad19/MIDI55 as 'Chinese cymbal' (it is GM Splash); pad20/MIDI56 'Ride bell' (it is GM Cowbell -- parenthetical 'cowbell slot' was right). FIX: corrected every GM name in the layout table and layoutChanges prose to the true GM note name; kept the placements (a clustered cymbal/perc family is musically fine), now honestly flagged as GM-slot deviations.

2. LAYOUT / inherited ride<->crash GM swap NOT flagged: the kit carries Ride on pad13 (=MIDI 49 = GM Crash 1) and Crash on pad15 (=MIDI 51 = GM Ride 1) -- i.e. ride and crash sit on each other's GM slots. The proposal's layout table even mislabeled the MIDI numbers (claimed pad13=51, pad15=49) to hide this. FIX: corrected the MIDI numbers in the table to the true values (pad13=49, pad15=51) and added an explicit flagged deviation noting the inherited ride/crash GM-slot swap (kept, since re-seating the whole crafted layout is out of scope, but now documented rather than disguised).

3. COVERAGE / Ride pad 13 silently defaulted a meaningful param: the ride archetype (ride-cymbal-bell-bow-cymbal.md) deliberately sets Strike Position 0.18 (near the soundbow antinode -> full partial set for the bell ping); the proposal left strikePosition at the 0.30 default with no documented reason. FIX: added strikePosition 0.18 with citation.

4. COVERAGE / Cymbal Swell pad 18 dropped the archetype's primary swell lever: suspended-cymbal-orchestral-hat-cymbal.md (rolled-swell column) sets NonlinearCoupling 0.45 -- the amplitude-driven energy-cascade brightening that IS the 'louder/longer = brighter' swell build -- plus Strike Position 0.90 (edge strike) and an HP filter at 0.58. The proposal set NonlinearCoupling=0 ('morph+noise carry the build'), silently defaulting the single most characteristic swell param. FIX: added nonlinearCoupling 0.45 and strikePosition 0.90 (both cited); left the HP filter off as an acceptable documented simplification since morph+noise already carry the brightness sweep.

5. CONSISTENCY / pad 3 kit-character claim overstated: the kitCharacter and layoutChanges text cite a 'filter-ADSR sustain' as a reason the Brush Swirl exists and as param-surface coverage, but pad 3's params set tsFilterEnvAmount 0.5 (= 0, no modulation) so the filter ADSR is inert -- only a static LP at 0.55 is applied. The friction-drone archetype does call for a sustained filter ADSR, but rather than invent ADSR values not in the proposal I corrected the CLAIM: removed 'filter-ADSR sustain' from the coverage prose so the plan does not assert a surface it doesn't exercise. (Physics of pad 3 is otherwise correct: Friction exciter LIVE, modeInject 0.18 = 1/k bowed series, nonlinearCoupling 0.45, tensionMod 0.30 and airLoading 0.50 both Membrane-valid, decaySkew 0.85 low-tilt.)

6. DOC / Woodblock pad 1 noiseLayerMix 0.05 vs archetype 0.0: the woodblock archetype's signature is the ABSENCE of sustained noise (Noise Mix 0). The proposal sets 0.05 (near-off). Kept (audibly negligible) but the rationale now explicitly notes it is a near-off deviation from the archetype's noise-OFF, not the archetype value.

7. VERIFIED OK (no change needed): all valueNorm in [0,1]; discrete/sentinel params legal (outputBus 0.0667->aux1, chokeGroup 0.125->group1, tsFilterType 0/0.5 -> LP/HP, secondaryEnabled/morphEnabled 1.0). Body/exciter pairings all physically correct: Membrane+Mallet (kick/toms), Membrane+NoiseBurst (brush snares), Membrane+Friction (swirl), Plate+Impulse (woodblock free-bar), Shell+Impulse (side-stick free-free bar), Bell+NoiseBurst (bow ride), Bell+FMImpulse (live FM cup -- fmRatio 0.30 -> 1.9 Chowning, the kit's only audible FM), NoiseBody+NoiseBurst (hats/crash/splash/swell/shaker). Post-audit semantics honored: airLoading set only on Membranes, tensionMod only on Membranes, NonlinearCoupling used as amplitude brightening, Drive as flavour (pad2/4 warmth not level), modeInject 1/k only on the bowed swirl, decaySkew per-mode tilt on cymbals/ride/toms, fmRatio/feedbackAmount documented inert under NoiseBurst on ride pad13, pan spread M-9, kit globals dialed so per-pad couplingAmount engages. Ride pad13 double-modeScatter (0.28->0.85 in the current builder) correctly cleaned to a single 0.62; b3 corrected 0.30->0.0 for a metallic ride.
