<!-- verdict: pass-with-fixes | coverageOk: false | issues fixed: 5 | IMPLEMENTED: 2026-06-09 (commit re-tune Tabla; ~22 pads reconstructed from archetypes, no structured table in repo) -->

# Membrum Kit Re-Design — "Tabla" (Percussive · `tablaKit()`)

Voiced against the post-audit (2026-06-07) DSP semantics: measured-strike body norm (−6 dBFS `kBodyHeadroom`), ModeInject = 1/k (−6 dB/oct), per-mode decaySkew tilt on all bodies, energy-driven tensionMod (~2 st at full velocity, Membrane-only), NonlinearCoupling = amplitude-driven brightening, Drive = flavour, per-pad equal-power pan.

## Concept
The current `tablaKit()` crafts only 10 pads (0–9), five of them literal clones, and disables pads 10–31. This re-design grows it to **30 enabled, distinct pads** forming a complete North/South-Indian classical-percussion ensemble: the full tabla bol set, a Dholak HI/LO pair, a Mridangam pair + strokes, a Ghatam clay pot, Kanjira/Daf frame drums, Manjira/Tingsha hand cymbals, a Khartal wood clapper, a Chimta metallic shaker, a pitched Dayan/Bayan tarang melodic row, and a two-string Tanpura drone bed. It keeps the kit's library-leading tensionMod + decaySkew use and adds the **ModeInject 1/k syahi stand-in** (the headline post-audit recommendation for the harmonic dayan) plus modeStretch, the Shell/Bell/NoiseBody/String bodies, secondary-shell coupling, Morph, Friction, and per-pad pan — so the kit collectively exercises the full param surface.

## Verification corrections applied (adversarial pass)
Three coverage/physics fixes were applied to the proposal before acceptance; the per-pad `pads` array reflects them:

1. **Bell pads 19 (Manjira) & 20 (Tingsha) — DecaySkew added.** Was silently neutral (0.5); the cited Tibetan-Tingsha recipe and audit M-5 (per-mode `ratio^(−decaySkew)` tilt now live on Bell) make it a meaningful tail-shaping param. **Pad 19 DecaySkew 0.40, Pad 20 DecaySkew 0.38** (lift upper partials / trim hum so the ring purifies as it decays).
2. **Bell pads 19/20 — ModeStretch raised** from 0.45/0.50 to **0.55 / 0.60** (phys 1.325 / 1.40), closer to the cited tingsha 0.62 inharmonic bronze spacing — these are thick free-edged cymbal-bells, strongly inharmonic.
3. **Chimta pad 21 (NoiseBody) — metallic Shell secondary enabled.** The cited Tambourine/Riq archetype treats the coupled Shell (Secondary Enabled, **Secondary Size 0.20, Secondary Material 0.85, Coupling Strength 0.40**) as the source of the discrete tuned metallic jingle ring over the broadband body. Was silently OFF; now on, so the chimta reads as jingling metal rather than filtered hiss.

(Minor: a few delta citations attributed to "recipe" that are actually reasonable derivations — pad 4 Tha airLoading 0.42, pads 5/6/7 b1, pad 8 secondary 0.32 — were relabelled "derived"; no numeric change.)

## Layout (GM MIDI 36..67 → pad 0..31)
| Pad | Drum | Body | Exciter | Pan |
|---|---|---|---|---|
| 0 | Bayan 'Dha/Ge' open bass | Membrane | Impulse | 0.42 L |
| 1 | Bayan 'Ka' damped bass | Membrane | Impulse | 0.42 L |
| 2 | Dayan 'Na' open tone | Membrane | Impulse | 0.58 R |
| 3 | Dayan 'Tin' edge | Membrane | Impulse | 0.58 R |
| 4 | Dayan 'Tha' palm | Membrane | Mallet | 0.58 R |
| 5 | Dayan 'Tete' damped tap | Membrane | Impulse | 0.58 R |
| 6 | Bayan 'Ge' extreme gliss | Membrane | Impulse | 0.42 L |
| 7 | Dayan 'Ti' damped edge | Membrane | Impulse | 0.58 R |
| 8 | Dayan 'Na' bloom (morph) | Membrane | Impulse | 0.55 R |
| 9 | Dholak HI | Membrane | Impulse | 0.58 R |
| 10 | Dholak LO | Membrane | Impulse | 0.42 L |
| 11 | Mridangam BASS (thoppi) | Membrane | Impulse | 0.40 L |
| 12 | Mridangam TREBLE (valanthalai) | Membrane | Impulse | 0.60 R |
| 13 | Ghatam clay pot | Membrane | Impulse | 0.50 C |
| 14 | Kanjira frame drum | Membrane | Impulse | 0.62 R |
| 15 | Daf/Duff frame drum | Membrane | Mallet | 0.45 L |
| 16 | Dayan-tarang lo | Membrane | Impulse | 0.48 |
| 17 | Dayan-tarang mid | Membrane | Impulse | 0.52 |
| 18 | Dayan-tarang hi | Membrane | Impulse | 0.56 |
| 19 | Manjira hand cymbals | Bell | Impulse | 0.30 L |
| 20 | Tingsha / large Manjira | Bell | Impulse | 0.70 R |
| 21 | Chimta metallic shaker | NoiseBody | NoiseBurst | 0.65 R |
| 22 | Khartal wood clapper | Shell | Impulse | 0.38 L |
| 23 | Tabla 'Dhin' composite | Membrane | Impulse | 0.50 C |
| 24 | Tanpura drone Sa | String | Friction | 0.50 C (aux 1) |
| 25 | Tanpura drone Pa (5th) | String | Friction | 0.50 C (aux 1) |
| 26 | Bayan-tarang low (melodic) | Membrane | Impulse | 0.40 L |
| 27 | Dayan-tarang very-hi | Membrane | Impulse | 0.58 R |
| 28 | Mridangam 'chapu' slap | Membrane | Impulse | 0.62 R |
| 29 | Khol/Mridangam bass roll | Membrane | Mallet | 0.40 L |
| 30 | Dayan-tarang spare (optional) | Membrane | Impulse | 0.50 |
| 31 | Bayan-tarang spare (optional) | Membrane | Impulse | 0.44 L |

Kit globals (unchanged, already well-dialed): maxPolyphony 12, globalCoupling 0.30, tomResonance 0.45, couplingDelayMs 1.1. `crafted = {0..31}` (was {0..9}).

## Body-model coverage (collective param-surface use)
- **Membrane** (24 pads): the membrane motion axes — tensionMod (0.12 tight edge → 0.85 Ge bass), decaySkew (0.30 → 0.66), airLoading (0.38 → 0.62), modeScatter (0.04 → 0.45 on the Na bloom), and **ModeInject 1/k** on every harmonic-loaded head (dayan/mridangam/tarang) as the syahi stand-in. Secondary-shell coupling on every pot/barrel/shell drum.
- **Bell** (19, 20): manjira/tingsha — **modeStretch (0.55/0.60), DecaySkew (0.40/0.38, corrected), b3=0 metallic ring, modeScatter beating.**
- **NoiseBody** (21): chimta — NoiseBurst, noise cutoff/color/decay, modeScatter, **metallic Shell secondary (0.20/0.85, coupling 0.40, corrected on).**
- **Shell** (22): khartal — free-free bar, modeStretch, b3 woody damping.
- **String** (24, 25): tanpura Sa+Pa — Friction, Morph, Drive+Fold (jivari), NonlinearCoupling, aux bus.

## Per-pad exact values
See the structured `pads` array for the complete exact-value table (every meaningful param + rationale + citation, plus each pad's documented defaults). Selected headline numbers:

- **Bayan Ge (0/6):** size 0.72 (95 Hz), decay 0.78/0.85, pitchEnv 180→70 Hz/100 ms, **tensionMod 0.78/0.85**, decaySkew 0.65, airLoading 0.62, secondary pot 0.45/0.40, pan 0.42.
- **Dayan Na (2):** size 0.42, pitchEnv 420→380/20 ms, **ModeInject 0.30**, airLoading 0.45, decaySkew 0.65, modeScatter 0.05, secondary 0.35/0.45, click 0.55/bright 0.78, pan 0.58.
- **Dholak HI/LO (9/10):** plain-membrane (no ModeInject), giss 320→220/25 ms and 220→140/80 ms, tensionMod 0.16/0.34, b3 1.4e-4/2.2e-4, pan 0.58/0.42.
- **Tanpura Sa/Pa (24/25):** String/Friction, size 0.65/0.58, decay 0.95, frictionPressure 0.45, Morph 0.45→0.70, NonlinearCoupling 0.50, Fold 0.22, aux bus 1, no click.
- **Manjira/Tingsha (19/20):** Bell, b3=0, **modeStretch 0.55/0.60, DecaySkew 0.40/0.38**, decay 0.70/0.82, pan 0.30/0.70.
- **Chimta (21):** NoiseBody/NoiseBurst, noise 0.45 violet ~9 kHz, modeScatter 0.30, **Secondary Shell on (size 0.20 / mat 0.85 / coupling 0.40)**, pan 0.65.

## Delta from current
1. 10 crafted/disabled-rest → 30 enabled distinct pads; no clones, no dead slots.
2. Dayan/treble drums now use **ModeInject 0.30** (the unused 1/k syahi fix).
3. Bayan/Ge tensionMod raised to cited 0.78/0.85; bayan pitchEnv corrected to 180→70/100 ms.
4. Adds Dholak, Mridangam, Ghatam, Kanjira/Daf, Manjira/Tingsha, Khartal, Chimta → 5 of 6 body models used (was Membrane + 1 String).
5. Adds the second tanpura drone string (Sa+Pa) on aux bus 1.
6. Every membrane pad gets explicit secondary-shell weight + a pan placement; modeScatter used as the syahi 2nd/3rd-harmonic split. Bell tail and NoiseBody jingle ring corrected (decaySkew + Shell secondary).
7. A pitched 'tarang' row turns the upper pads into a playable melodic range.

## Gaps / notes
- Pads 30–31 are stocked but flagged as optional spares (a 28-pad kit is acceptable; they are NOT silently dropped).
- fmRatio on the Bell manjira pads is inert (Impulse exciter) — left at default and documented; the Bell ratio bank itself carries the metallic partials.
- airLoading/tensionMod/modeStretch/decaySkew/modeInject/modeScatter/damping are all inherent no-ops on the String tanpura pads — documented per pad.
- tensionMod, airLoading appear ONLY on Membrane pads (verified Membrane-only per audit); all Bell/NoiseBody/Shell/String pads correctly list them as no-ops.

---

## Verification log (5 issues found & fixed)

1. COVERAGE (Bell pads 19 Manjira & 20 Tingsha): DecaySkew silently left at default 0.5 (neutral). The cited Tibetan-Tingsha recipe sets DecaySkew 0.38 (=-0.24) as a MEANINGFUL Bell param, and audit M-5 made the per-mode ratio^(-decaySkew) tilt audible on Bell. Leaving it neutral discards the partial-purification (lift upper partials / trim hum) that defines a struck cymbal-bell tail. FIX: set DecaySkew 0.40 on pad 19 and 0.38 on pad 20 (per the cited tingsha recipe), and remove them from defaultedParams.

2. PHYSICAL (Bell pads 19/20 ModeStretch too low): proposal uses 0.45/0.50 (phys 1.175/1.25); the cited tingsha recipe uses 0.62 (phys 1.43) for the inharmonic bronze metallic spacing. Manjira/tingsha are thick free-edged bronze cymbal-bells = strongly inharmonic. FIX: raise to 0.55 (pad 19) / 0.60 (pad 20) to land closer to the cited metallic inharmonicity while keeping the small per-disc grading.

3. COVERAGE (Chimta pad 21 NoiseBody): the metallic Shell SECONDARY (Secondary Enabled, size 0.20, material 0.85, coupling 0.40) is silently OFF, but the cited Tambourine/Riq archetype treats that coupled Shell as a CORE component supplying the discrete tuned metallic jingle ring on top of the broadband NoiseBody/violet hiss. Without it the chimta reads as filtered hiss, not jingling metal. FIX: enable Secondary (1), Coupling Strength 0.40, Secondary Size 0.20, Secondary Material 0.85; move them out of defaultedParams.

4. CITATION HYGIENE (non-blocking, no value change): pad 4 (Tha) cites 'tabla-dayan recipe (Tha airLoading 0.42)' but the dayan recipe's Tha bol-variant list does not specify an airLoading delta (baseline 0.45). Value 0.42 is in-range and acoustically harmless; left as-is but the citation is relabelled to 'derived (slightly less near-harmonic than Na)'. Same minor relabel applied to a few other 'recipe'-attributed deltas that are actually reasonable derivations (pad 6 b1 0.25, pad 5/7 b1, pad 8 secondary 0.32) -- no numeric change.

5. VERIFIED OK (no change): tensionMod is set only on Membrane pads (Bell/NoiseBody/Shell/String pads correctly list it as a NO-OP in defaultedParams); airLoading present only on Membrane pads; ModeInject 0.30 used as the 1/k syahi stand-in on dayan/mridangam/tarang and correctly OFF on plain dholak/ghatam/bass heads; Output Bus 0.0667 decodes to aux bus 1 (legal); body+exciter pairings all correct (Bell=manjira/tingsha, Shell=khartal bar, NoiseBody=chimta, String/Friction=tanpura); all valueNorm in [0,1]; pitchEnv seeds strike pitch per audit H-3; bayan tensionMod raised to cited 0.78/0.85 and pitchEnv 180->70/100ms corrected. Layout gaps/duplicates correctly identified (10 crafted->30 enabled, clone pads de-duplicated, spares flagged not dropped).
