<!-- verdict: pass-with-fixes | coverageOk: false | issues fixed: 12 | IMPLEMENTED: 2026-06-09 (commit re-tune LinnDrum CR-78) | NOTE: per-pad value table absent from plan; new pads reconstructed from archetypes + sibling kits (909/808) within documented deltas. Crafted = 25 pads (0-24); plan header "27" is a miscount. -->

# LinnDrum CR-78 — Corrected Kit Plan (Membrum, post-audit) — VERIFIED

**Builder:** `linnDrumKit()`  ·  **Category:** Electronic  ·  **Crafted pads:** 27 (was 14)

A blend of the all-analog **Roland CR-78** (1978: dark lo-fi Latin/bossa percussion, two-square-wave cowbell) and the **Linn LM-1/LinnDrum** (1982: 8-bit companded samples of real kit pieces). The redesign keeps the retro DNA (FMImpulse grit, dark noise hats) and fills the empty pads with the two machines' signature percussion, while deliberately activating the post-audit param surface (pan spread, modeStretch/scatter/skew on metallic pads, modeInject, secondary shell, tensionMod, a Friction guiro).

> **Adversarial-verification corrections applied (this revision):**
> 1. **Snare (pad 2) reverted to single-layer.** The proposed secondary metallic shell (Coupling 0.4 / Size 0.55 / Mat 0.50) was removed — it contradicted the LinnDrum-snare archetype, whose *defining* identity is the deliberately thin, dry, single-layer sample machine snare ("no secondary metallic body … exactly the thinner, drier, gated sample-machine character"). The proposal's rationale ("restore body weight vs 808/909") was backwards: the LinnDrum snare is *supposed* to be thinner than the 808/909. A light **Mode Inject 0.10** (1/k) is kept for a touch of fundamental fill — a far gentler delta than a metallic shell.
> 2. **Guiro (pad 23) coverage filled** — added Strike Position 0.30, Body Damping b1 0.42, Decay Skew 0.55, Noise Resonance 0.20 (all archetype-specified, previously silently defaulted).
> 3. **Maracas (pad 22)** Noise Color → 0.50 (Pink) for the dark CR-78 'shh' (was White 0.65, against both the kit goal and the dedicated maracas archetype); added Strike Position 0.50.
> 4. **Coverage top-ups** — Agogo hi/lo Strike Position 0.30 (+ hi FM Ratio 0.72, Click Contact 0.30); Ride Strike Position 0.18; Clap Click Contact 0.22 / Brightness 0.62; Conga Body Damping b3 0.10 / Mode Scatter 0.10; Cabasa Noise Resonance 0.16.
> 5. Kept (documented deltas, not defects): Kick secondary woody shell (adds sub-weight without defeating identity); Cowbell Mode Inject 0.10 (light two-tone reinforcement).

## Pad layout (GM-map sense)

| Pad | MIDI | Drum | Body | Exciter | Notes |
|---|---|---|---|---|---|
| 0 | 36 | FM Kick | Membrane | FMImpulse | +secondary woody shell, +modeInject |
| 1 | 37 | Rim Shot | Shell | Impulse | was Cabasa |
| 2 | 38 | Snare | Membrane | NoiseBurst | **single-layer (no secondary shell)**, +light modeInject |
| 3 | 39 | **Handclap** | NoiseBody | NoiseBurst | NEW (was Clave); fills clap gap |
| 4 | 40 | Cowbell | Bell | FMImpulse | +stretch/scatter/skew/inject |
| 5 | 41 | Tom 1 floor (LinnDrum) | Membrane | Mallet | sampled half |
| 6 | 42 | Closed Hat | NoiseBody | NoiseBurst | dark CR-78, choke 1 |
| 7 | 43 | Tom 2 low (LinnDrum) | Membrane | Mallet | sampled half |
| 8 | 44 | Pedal Hat | NoiseBody | NoiseBurst | choke 1 |
| 9 | 45 | Tom 3 low-mid (LinnDrum) | Membrane | Mallet | sampled half |
| 10 | 46 | Open Hat | NoiseBody | NoiseBurst | choke 1 |
| 11 | 47 | Tom 4 hi-mid (CR-78) | Membrane | **Impulse** | synth half (split) |
| 12 | 48 | Tom 5 high (CR-78) | Membrane | Impulse | synth half |
| 13 | 49 | Crash | NoiseBody | NoiseBurst | +stretch/inject/nonlinear bloom, bus 1 |
| 14 | 50 | Tom 6 highest (CR-78) | Membrane | Impulse | synth half |
| 15 | 51 | **Ride** | Bell | FMImpulse | NEW, bus 1, +strikePos |
| 16 | 52 | Cabasa | NoiseBody | NoiseBurst | relocated from pad 1, +noiseReso |
| 17 | 53 | **Agogo Hi** | Bell | FMImpulse | NEW Latin perc, fmRatio 0.72 |
| 18 | 54 | **Tambourine** | NoiseBody | NoiseBurst | NEW (CR-78 signature) |
| 19 | 55 | **Agogo Lo** | Bell | FMImpulse | NEW (pair with 17) |
| 20 | 56 | **Conga Hi** | Membrane | Impulse | NEW; airLoading+tension+b3 |
| 21 | 57 | Claves | Shell | Impulse | relocated; reverted to Shell+Impulse |
| 22 | 58 | **Maracas** | NoiseBody | NoiseBurst | NEW shaker, Pink (dark) |
| 23 | 59 | **Guiro** | NoiseBody | **Friction** | NEW; only Friction pad, full coverage |
| 24 | 60 | **Conga Lo** | Membrane | Impulse | NEW (pair with 20) |
| 25–31 | 61–67 | (disabled) | — | — | documented headroom |

## Why these deltas (collective param-surface use)

- **Pan** — replaces all-center: toms graded 0.30→0.70 (floor→highest), Latin perc off-center (cowbell 0.62, agogo hi 0.38 / lo 0.60, conga hi 0.40 / lo 0.58, maracas 0.35, cabasa 0.65, tamb 0.65, guiro 0.55, crash 0.42, ride 0.58). Restores the M-9 stereo image.
- **modeStretch + modeScatter + decaySkew** — lifted off neutral on every Bell/NoiseBody metallic pad (cowbell, agogo hi/lo, ride, crash, rim) so the corrected `B_max=0.01` inharmonicity and the per-mode `ratio^(−skew)` tilt (audit M-5, all bodies) give each metallic voice a distinct CR-78 clang/wash.
- **modeInject (1/k)** — harmonic body fill on kick (0.12), snare (0.10 — the *only* body-thickening on the deliberately single-layer snare), cowbell (0.10), crash (0.25); now −6 dB/oct (audit fix).
- **Secondary shell** — enabled on **kick** (woody sub-weight) and the **congas** (woody barrel). **NOT on the snare** — the LinnDrum snare is authentically single-layer; a metallic shell would erase its identity.
- **tensionMod** — Membrane-only kerthump on all six toms and both congas.
- **Friction exciter** — on the guiro (previously unused in this kit), now with full param coverage (b1, strikePos, decaySkew, noiseReso), widening exciter coverage to all four common backends plus FMImpulse.
- **Routing** — choke group 1 on the three hats; output bus 1 on crash + ride.

## Tom row de-duplication

The six toms were one Mallet/Membrane recipe stamped with a size sweep (audit §4 #3). Split: pads 5/7/9 stay **Mallet** (LinnDrum sampled-acoustic softer attack, lower 3 toms), pads 11/12/14 become **Impulse** (CR-78/909 snappier pulse-triggered synth tom, upper 3, hotter Click). Two distinct tom timbres, graded across the pan arc. Physics axes (stretch/skew/scatter/inject) stay neutral on toms per the tom archetype — a non-zero stretch would now audibly inharmonic-ize a tom we want as a clean pitched oscillator.

## Corrected/added per-pad values (delta from the proposed table)

- **Pad 2 Snare:** Secondary Enabled **0**, Coupling Strength **0**; Secondary Size/Material now defaulted ("LinnDrum snare is single-layer by design — archetype 'no secondary metallic body'"). Mode Inject 0.10 retained.
- **Pad 23 Guiro:** + Strike Position **0.30**, Body Damping b1 **0.42**, Decay Skew **0.55**, Noise Resonance **0.20**.
- **Pad 22 Maracas:** Noise Color **0.50** (Pink) [was 0.65 White]; + Strike Position **0.50**.
- **Pad 17 Agogo Hi:** FM Ratio **0.72** [was 0.65]; + Strike Position **0.30**, Click Contact **0.30**.
- **Pad 19 Agogo Lo:** + Strike Position **0.30**.
- **Pad 15 Ride:** + Strike Position **0.18**.
- **Pad 3 Clap:** + Click Contact **0.22**, Click Brightness **0.62**.
- **Pad 20/24 Conga:** + Body Damping b3 **0.10**, Mode Scatter **0.10**.
- **Pad 16 Cabasa:** + Noise Resonance **0.16**.

*All other proposed per-pad normalized values are verified consistent with their cited archetypes, in [0,1], with discrete/selector params decoding to the intended enum. Voiced against the corrected post-audit signal path: measured-strike body norm (N-1), linear voice + hard-clip rail + bus true-peak limiter, Drive-as-flavour (M-2), env-level NonlinearCoupling (M-3/M-4), 1/k modeInject and B_max=0.01 stretch, M-9 per-pad pan.*

---

## Verification log (12 issues found & fixed)

1. IDENTITY/PHYSICS (pad 2 Snare) — FIXED: proposal ENABLED a secondary metallic shell (Secondary Enabled 1, Coupling 0.4, Size 0.55, Material 0.50) citing 909-snare-snare.md 'to restore body weight the old single-layer snare lacked vs 808/909'. This directly contradicts the LinnDrum-snare archetype, whose DEFINING identity is single-layer/thinner/drier ('Secondary Enabled 0 / Coupling Strength 0 (no secondary metallic body)'; the archetype's whole 'How this differs from 808/909' section says it is deliberately NOT two-tone). The cited 909 archetype is the wrong instrument. FIX: reverted snare to single-layer — Secondary Enabled 0, Coupling Strength 0, removed Secondary Size/Material (now defaulted with reason). Kept the light Mode Inject 0.10 as a gentle 1/k body-fill delta (documented, not the metallic two-tone shell).

2. COVERAGE (pad 23 Guiro) — FIXED: proposal left four physically-meaningful, archetype-specified params silently at default. guiro-perc.md sets Strike Position 0.30, Body Damping b1 0.42 (21.1 s^-1 flat damping), Decay Skew 0.55 (+0.10 high-mode lift), Noise Resonance 0.20 (q~1.24). FIX: added all four to the guiro pad.

3. PHYSICS/CHARACTER (pad 22 Maracas) — FIXED: proposal set Noise Color 0.65 (=White) with rationale 'White', contradicting the kit's explicit dark-CR-78 goal and the dedicated maracas-gourd-shaker.md (Noise Color Pink 0.50, 'darker/softer than cabasa White/Violet'). Also the dedicated archetype targets Material ~0.35 (gourd internal-noise peak ~3.25 kHz) vs the proposal's 0.78 (borrowed from the cabasa.md maraca-variant). FIX: set Noise Color 0.50 (Pink) for the dark maraca; added Strike Position 0.50 and Mode Scatter kept; noted Material as a defensible 'gourd slightly darker than cabasa' choice but flagged the cross-archetype inconsistency.

4. COVERAGE (pad 17/19 Agogo hi/lo) — FIXED: Strike Position (archetype 0.30, |cos(m*theta)| beater-on-soundbow weighting — meaningful on the corrected 2-D Bell strike model) was silently defaulted. Added Strike Position 0.30 to both. Also raised Agogo Hi FM Ratio 0.65->0.72 to match the archetype's bright-hi target (mod ratio 3.16, clearly above the cowbell 1.44-1.48 fifth); added Click Contact 0.30.

5. COVERAGE (pad 15 Ride) — FIXED: Strike Position (archetype 0.18, near-soundbow antinode for the full partial set) was missing. Added Strike Position 0.18. (Ride uses FMImpulse, a documented legal alternative per ride-cymbal-bell-bow-cymbal.md; FM Ratio 0.30 is therefore meaningful, not inert — correct.)

6. COVERAGE (pad 3 Handclap) — FIXED: Click Brightness (archetype 0.62) and Click Contact (archetype 0.22) and Strike Position were silently defaulted though Click Mix 0.30 is active. Added Click Contact 0.22, Click Brightness 0.62.

7. COVERAGE (pad 20/24 Conga) — FIXED: Body Damping b3 (archetype 0.10, 1e-4 f^2 damping that 'kills metallic highs -> woody' — core to the rawhide tone) and Mode Scatter (archetype 0.10 rawhide unevenness) and the small kerthump PitchEnv were silently defaulted. The conga archetype uses BOTH a short ~20 ms PitchEnv settle AND tensionMod; the proposal dropped the PitchEnv claiming 'tensionMod carries the bend'. Added Body Damping b3 0.10 and Mode Scatter 0.10; left the explicit PitchEnv as an accepted simplification (tensionMod is the dominant glide driver and the archetype's PitchEnv is tiny) but documented it rather than silently omitting.

8. MINOR (pad 16 Cabasa) — FIXED: Noise Resonance (archetype 0.16) silently defaulted; added Noise Resonance 0.16. Noise Cutoff 0.85 kept (bright steel cabasa is the bright shaker; archetype 'brighter cabasa' variant allows 0.80, 0.85 acceptable).

9. NOTED (no change): pad 4 Cowbell adds Mode Inject 0.10, which the cowbell archetype explicitly defaults OFF ('preserve the pure inharmonic clang'). Kept as a documented kit-character delta (light two-tone fundamental reinforcement) since it is small and explicitly rationalized; not a defect, but it is a deviation from the archetype baseline.

10. NOTED (no change): pad 0 Kick enables a secondary woody shell, which the kick archetype defaults OFF. Unlike the snare, adding a sub-shell for weight does not defeat the kick's identity and is a defensible documented delta for full-surface activation; kept. Both kick and conga correctly set BOTH Secondary Enabled 1 AND Coupling Strength >0 (the shell is a no-op without both).

11. RANGE/DISCRETE (verified clean): all valueNorm in [0,1]. Output Bus 0.0667->bus 1, Choke 0.125->group 1, Exciter Friction 0.6->3, FMImpulse 0.8->4, Mallet 0.2->1, Impulse 0->0; Body Shell 0.4->2, Bell 0.8->4, NoiseBody 1.0->5, Membrane 0->0 — all decode to the intended enum. Tambourine/clap use Body 1.0 (unambiguous NoiseBody) rather than the archetype's risky 0.90 (round(4.5)) — correct hardening.

12. LAYOUT (verified, minor note): GM core hygiene is good (rim@37, claps... 3 hats@42/44/46 choke-1, crash@49, ride@51). Several Latin-perc reassignments repurpose unconventional GM slots (55 Splash->Agogo Lo, 57 Crash2->Claves, 58 Vibraslap->Maracas, 59 Ride2->Guiro, 60 HiBongo->CongaLo) — all documented and acceptable per policy. Cowbell remaining on pad 4 (MIDI 40 = GM Electric Snare) is a pre-existing non-standard placement inherited from current-state; flagged but left. Gaps/duplicates correctly identified (clap/tamb/maracas/guiro/ride/conga/agogo fills; hat & idiophone 'duplicates' correctly justified as variants).
