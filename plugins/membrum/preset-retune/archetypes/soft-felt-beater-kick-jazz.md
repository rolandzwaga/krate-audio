# Membrum Recipe — "Soft Felt-Beater Kick (jazz)"

**Body:** Membrane (Bessel drumhead, 48 modes, air-loaded) · **Exciter:** Mallet (soft felt beater)

A gentler, tuned-up maple bass drum for jazz/brush playing: same membrane physics as the maple rock kick, but voiced for a low-velocity felt beater — the attack transient is deliberately **suppressed relative to the body tone**.

## Physics / research basis
- **Pitch:** Small jazz kicks (18–20", or 16" club) read HIGHER than a 22–24" rock kick — perceived ~100–150 Hz vs ~40–80 Hz ([iDrumtune](https://www.idrumtune.com/mixing-drums-know-your-drum-frequencies/), [SOS](https://www.soundonsound.com/techniques/synthesizing-drums-bass-drum)). size 0.72 → natural f0 95.3 Hz; pitch-env drives 140→60 Hz.
- **Modes:** Ideal Bessel ratios 1:1.59:2.14:… are pulled toward the near-harmonic air-loaded timpani series 1:1.5:2:2.44:2.9 ([Rossing / Well-Tempered Timpani](https://wtt.pauken.org/chapter-3/air-loading-2)). High airLoading (0.80) gives the deep, focused, "less whistly" boom.
- **Decay:** Most kick energy is <100 Hz and the audible body is gone within ~50 ms ([Sage](https://www.sageaudio.com/articles/the-science-of-mixing-perfect-kick-bass)); slightly more open than a muffled rock kick.
- **Felt beater attack:** "Smooth, rich, softer attack… round mellowed boom of big-band jazz," HF-suppressed vs the wood beater's bright, scooped-mid punch ([Sweetwater](https://www.sweetwater.com/insync/different-types-of-bass-drum-beaters/), [DRUM!](https://drummagazine.com/beat-it-43-bass-drum-beaters-reviewed/)). → Mallet exciter + low/dull click.
- **Glide:** Tension ∝ displacement² relaxes as amplitude decays → "a couple of semitones" drop ([SOS](https://www.soundonsound.com/techniques/synthesizing-drums-bass-drum)); brief explicitly sets 140→60 Hz / 25 ms.

## Key normalized params (post-audit semantics)
| Param | Norm | Physical |
|---|---|---|
| Exciter | 0.20 | Mallet (soft beater) |
| Body | 0.00 | Membrane |
| Material | 0.42 | brightness 0.42 (brighter maple than rock kick) |
| Size | 0.72 | f0 = 95.3 Hz natural |
| Decay | 0.30 | ~0.42× base decay (short thump) |
| Strike Pos | 0.30 | r/a 0.27 (off-center beater) |
| Level | 0.82 | linear pre-limiter |
| PitchEnv Start | 0.4226 | 140 Hz |
| PitchEnv End | 0.2386 | 60 Hz |
| PitchEnv Time | 0.05 | 25 ms |
| PitchEnv Curve | 0.15 | exp fast-drop (−0.7) |
| Click Mix | 0.30 | subdued (suppressed slap) |
| Click Contact | 0.40 | 3.2 ms (soft, smeared) |
| Click Brightness | 0.22 | ~492 Hz (dull thud) |
| Air Loading | 0.80 | 80% Rossing air-loaded |
| Body Damping b1 | 0.34 | 17.1 s⁻¹ |
| Body Damping b3 | 0.12 | 1.2e-4 s (mild HF damp) |
| Tension Mod | 0.16 | +~0.3 st kerthump |
| Noise Mix | 0.08 | near-off air chuff |
| Noise Cutoff | 0.30 | ~280 Hz LP |
| Coupling Strength | 0.32 | shell feedforward |
| Secondary Enabled | 1.0 | shell bank on |
| Secondary Size | 0.40 | 0.70× head f0 |
| Secondary Material | 0.55 | maple shell |

## Voicing rationale vs the maple rock kick
- **Higher** Material (0.42 vs 0.28–0.35) → brighter, more open body.
- **Smaller** Size (0.72 vs ~0.85) → tighter, tuned-up small jazz kick.
- **Suppressed transient:** Mallet (not Impulse) + Click Mix 0.30 (vs 0.62–0.70) + dull brightness 0.22 (vs 0.40–0.45) — the felt/brush aesthetic.
- **Shorter glide:** 140→60 Hz / 25 ms (vs 150–160→50–55 / 40–45 ms).

## Defaults (left at neutral, with reason)
Filter chain bypassed (Cutoff 1.0); Drive/Fold 0 (clean acoustic kick); Mode Stretch/Skew/Inject/NonlinearCoupling neutral-or-off (natural air-loaded membrane already supplies the body); Morph off; Mode Scatter 0 (focused single-pitch kick — unlike a snare); Pan center; all macros 0.5 (base params voiced directly); secondary-exciter params (FM/Feedback/NoiseBurst/Friction) are no-ops for the Mallet exciter.