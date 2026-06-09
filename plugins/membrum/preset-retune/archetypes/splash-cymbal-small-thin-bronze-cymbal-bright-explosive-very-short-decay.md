# Membrum Recipe — Splash Cymbal

**Archetype:** Splash Cymbal — the smallest, brightest, shortest member of the crash family. A thin bronze plate (8" typical) with a quick, explosive bright burst and a sub-second decay.

**Body:** `NoiseBody` (32 plate-Chladni inharmonic modes + internal white-noise layer)
**Exciter:** `NoiseBurst` (violet-noise → bandpass broadband burst)

All values below are NORMALIZED [0,1] (on-wire/preset form); the physical target each denormalizes to is given.

---

## Physics / acoustics (researched)

- **What it is:** Splash = smallest cymbal of the crash family, 8" most common (range 4"–13"), thin bronze, **free** edges. "Extremely short sustain, usually less than a second"; "smaller cymbal = higher pitch + faster decay." ([Wikipedia – Splash cymbal](https://en.wikipedia.org/wiki/Splash_cymbal), [Beatello splash vs crash](https://beatello.com/blogs/news/splash-cymbal-vs-crash-cymbal-what-s-the-difference))
- **Partial structure / inharmonicity:** strongly inharmonic, >100 modes designated (m nodal diameters, n nodal circles); modal frequencies follow the modified Chladni power law **f = C·(m+2n)^P**, with **P = 1.4–2.4 for cymbals/bells** (flat-plate value 2). ([Chladni's law](https://en.wikipedia.org/wiki/Chladni's_law); [Rossing, Am.J.Phys. 1982](https://ui.adsabs.harvard.edu/abs/1982AmJPh..50..271R/abstract); [Normal modes of cymbals](https://www.researchgate.net/publication/289898852_The_normal_modes_of_cymbals)). Membrum's corrected post-audit plate table uses **(m+2n)^1.7** — squarely inside this band.
- **Spectral shape / noise content:** "the first moments of a cymbal crash sound a lot like white noise"; established synthesis = **filtered noise driving a large bank of inharmonic resonators**. Stowell gives a **splash band of 3,500–20,000 Hz** with a brief noise build (vs 300–20 kHz for a crash). ([Stowell cymbal synthesis](https://mcld.co.uk/cymbalsynthesis/))
- **Per-band decay (T60):** highs ring shortest, lows longest; splash overall is the fastest of the family — practical voiced T60 ≈ **0.3–0.5 s**.
- **Attack / nonlinear cascade:** energy first hits LOW modes then **nonlinearly cascades UP** into a dense high-frequency cluster ("rapid increase in HF energy, subharmonic generation, chaotic noise" at large amplitude). For a splash this build is very fast → instant bright "splash." ([Fletcher, Order from Complexity](https://acoustics.asn.au/journal/2012/2012_40_3_Fletcher.pdf); [Schedin/Gren/Rossing transient holography](https://www.ioa.org.uk/system/files/proceedings/s._schedin_p.o._gren_and_t.d._rossing_transient_wave_propagation_in_a_cymbal.pdf))
- **No tuned pitch glide.** Cymbals have no tom "kerthump"; pitch change is only the amplitude-dependent nonlinear effect → PitchEnv left OFF.

## Mapping rationale
NoiseBody is the cymbal palette (plate-Chladni inharmonic cluster + internal noise). NoiseBurst is the canonical "filtered-noise driver." The "small + short" splash is reached by **low Size** (high f0 ≈ 903 Hz at size 0.22), **low Decay** (T60 ≈ 0.44 s), **fast Noise Decay** (~63 ms), max-bright **Material/Noise Color/Noise Cutoff**, and **outputBus 1**. This recipe is derived from the corrected Crash voicing (audit Phase-4 ready): same body/exciter, scaled smaller and shorter — the shortest of the crash family.

---

## Parameters (normalized → physical)

| Param | Norm | Physical target | Why |
|---|---|---|---|
| Exciter Type | 0.40 | NoiseBurst (idx 2) | Filtered-noise driver = canonical cymbal excitation |
| Body Model | 1.00 | NoiseBody (idx 5) | Inharmonic plate-Chladni cluster + internal noise |
| Material | 0.95 | brightness ≈0.99; noise cutoff 6.25 kHz | Maximally metallic bronze; bright top |
| Size | 0.22 | f0 ≈ 903 Hz | Smallest cymbal → high pitch (spec) |
| Decay | 0.28 | ×0.46 → T60 ≈ 0.44 s | Sub-second sustain, shortest of family (spec) |
| Strike Position | 0.35 | off-bell Chladni strike | Bow/edge strike energizes the broad mode set |
| Level | 0.70 | linear 0.70 | Bright accent, sits under backbeat |
| Filter Cutoff | 1.00 | 20 kHz → SVF bypassed | Body/noise own the brightness |
| Mode Stretch | 0.55 | phys ≈1.33 (extra dispersion) | Cymbal partials are non-integer / dispersive |
| Mode Scatter | 0.50 | ~7.5% freq dither | Dense irregular mode set → organic shimmer |
| Body Damping b1 | 0.30 | ≈15 s⁻¹ flat floor | Short overall ring even in lows |
| Body Damping b3 | 0.00 | no f² roll-off | Metal keeps its high-partial shimmer |
| Air Loading | 0.00 | off | Membrane-only correction; n/a to plate |
| Noise Mix | 0.55 | 0.55 parallel noise | The defining splash "whoosh" |
| Noise Cutoff | 0.92 | ≈11.3 kHz LP | Splash band 3.5–20 kHz, brightest hiss |
| Noise Resonance | 0.20 | Q ≈1.24 | Wideband sizzle, no pitched peak |
| Noise Decay | 0.25 | ≈63 ms | Fast bright burst dies quickly (spec) |
| Noise Color | 0.85 | Violet (+6 dB/oct) | Highest centroid; brightest cymbal |
| Click Mix | 0.30 | stick "thwack" | 1 ms impact transient = attack definition |
| Click Brightness | 0.85 | ≈6.9 kHz BP | Crisp bright stick tick |
| NoiseBurst Duration | 0.40 | ≈7.2 ms burst | Brief broadband splash excitation |
| Output Bus | 0.0667 | aux bus 1 | Dedicated cymbal send (spec) |
| Brightness (macro) | 0.70 | +0.08 cutoff / +0.06 modeInject | Gentle push to the explosive top |
| Pan | 0.50 | center | Kit-arrangement decision, neutral default |
| Pad Enabled | 1.00 | on | — |

## Deliberate defaults (physical reasons)
- **ToneShaper filter / Drive / Fold:** bypassed — clean bright metal; body+noise carry the spectrum.
- **PitchEnv (all six):** Time=0 → off; cymbals have no tuned glide.
- **Mode Inject = 0:** a synthetic 1/k harmonic series would impose tonal pitch on an inharmonic noise body.
- **Nonlinear Coupling = 0:** the real nonlinear cascade already lives in the dense modal+noise body; kept off for a clean transient (raise slightly if more velocity-brightening is wanted).
- **Material Morph off:** one fixed bright metallic material.
- **Choke Group = 0:** free accent (kit-level, not archetype).
- **FM Ratio / Feedback / Friction:** other-exciter no-ops under NoiseBurst.
- **Coupling Strength / Secondary / Tension Mod:** shell/membrane concepts — a cymbal has no shell and ignores tension (Membrane-gated).
- **Tightness / Body Size / Punch / Complexity macros:** neutral 0.5 to preserve the explicitly-voiced base params.

## Sources
- [Wikipedia – Splash cymbal](https://en.wikipedia.org/wiki/Splash_cymbal)
- [Stowell – Cymbal synthesis tutorial](https://mcld.co.uk/cymbalsynthesis/)
- [Chladni's law (Wikipedia)](https://en.wikipedia.org/wiki/Chladni's_law)
- [Rossing – Chladni's law for vibrating plates, Am.J.Phys. 1982](https://ui.adsabs.harvard.edu/abs/1982AmJPh..50..271R/abstract)
- [The normal modes of cymbals](https://www.researchgate.net/publication/289898852_The_normal_modes_of_cymbals)
- [FEM study of crash & splash cymbals](https://www.researchgate.net/publication/359611258_A_Detailed_FEM_Study_on_the_Vibro-acoustic_Behaviour_of_Crash_and_Splash_Musical_Cymbals)
- [Schedin, Gren, Rossing – Transient wave propagation in a cymbal](https://www.ioa.org.uk/system/files/proceedings/s._schedin_p.o._gren_and_t.d._rossing_transient_wave_propagation_in_a_cymbal.pdf)
- [Fletcher – The Sound of Music: Order from Complexity](https://acoustics.asn.au/journal/2012/2012_40_3_Fletcher.pdf)
- [Understanding cymbal vibration modes](https://oemcymbal.com/understanding-cymbal-vibration-modes/)
- [Beatello – Splash vs Crash](https://beatello.com/blogs/news/splash-cymbal-vs-crash-cymbal-what-s-the-difference)