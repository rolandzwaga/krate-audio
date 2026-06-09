# Membrum Recipe — "Generic NoiseBody Cymbal-slot (uncrafted filler)"

**Acoustic Studio Kit cymbal/perc slots: physically-correct cymbal voicing, then DISABLED (`enabled=0`).** Not audible — recorded for completeness so the byte pattern in the disabled slots is a meaningful, well-formed cymbal config rather than garbage.

## What this archetype is
In `membrum_preset_generator.cpp::acousticKit()`, the cymbal pad indices `{13,15,16,17,19,21,23}` are configured as a NoiseBurst+NoiseBody cymbal (the `cymbalPads` loop) and the misc-perc indices `{1,3,18,20,22,24,25,26,27,28,29,30,31}` as Mallet+Plate. Neither set appears in `k.crafted = {0,2,4,5,7,9,11,12,14,6,8,10,13}`, so `disableUncraftedPads()` sets `pads[p].enabled = 0.0`. The voice therefore early-returns in `VoicePool::noteOn` and never sounds or consumes CPU. This recipe documents the cymbal (NoiseBody) variant.

## Body + Exciter
- **Body: NoiseBody** (offset 1, norm 1.0). 32-mode plate-Chladni bank + internal filtered-noise layer; `f0 = 1500*0.1^size`.
- **Exciter: NoiseBurst** (offset 0, norm 0.4). Violet-noise bandpass burst — the standard broadband cymbal driver.

## Physics it targets
Cymbals are strongly inharmonic (~300 modes in a 16" cymbal, Rossing), noise-dominant, with shimmer concentrated above ~3 kHz (modal density ∝ f²) and very long, frequency-dependent decay (lows ring longest). Hard strikes go quasi-periodic→chaotic, dissolving modal lines into broadband wash. No pitch glide. Bronze = bright, low damping, long sustain.

## Key normalized values (physical target)
| Param | Norm | Physical |
|---|---|---|
| Exciter Type | 0.4 | NoiseBurst |
| Body Model | 1.0 | NoiseBody |
| Material | 0.95 | brightness 0.985, noise cutoff 6.25 kHz |
| Size | 0.3 | f0 ≈ 752 Hz |
| Decay | 0.8 | ~1.9× body decay; noise dec 116 ms |
| Mode Scatter | 0.55 | ~8% line dither (chaos) |
| Air Loading | 0.0 | metallic, not air-loaded |
| Body Damping b3 | 0.0 | pure flat damping (long metallic highs) |
| Body Damping b1 | 0.3 | 15.1 s⁻¹ flat floor |
| Noise Mix | 0.5 | strong broadband wash |
| Noise Cutoff | 0.9 | ~9.7 kHz LP |
| Noise Color | 0.8 | Violet (HF-tilted) |
| Noise Decay | 0.7 | ~590 ms tail |
| Click Mix | 0.2 | light bright thwack |
| Click Brightness | 0.85 | ~7.3 kHz |
| Level | 0.72 | kit balance |
| **Pad Enabled** | **0.0** | **DISABLED — never sounds** |

## Why these
- **NoiseBody + NoiseBurst**: the inharmonic-plate-bank-plus-noise pairing is Membrum's cymbal body and matches established cymbal synthesis (resonator bank driven by broadband noise; Stowell).
- **Material 0.95 / b3 0.0**: bronze brightness + zero f²-damping keeps shimmer ringing.
- **Scatter 0.55**: smears clean plate lines toward the chaotic crash spectrum.
- **Air loading 0**: explicitly NOT a membrane.
- **Violet noise, high cutoff, long noise decay**: the >3 kHz HF-weighted wash.
- **No pitch env, no tension mod, no mode inject**: cymbals have no glide and no integer fundamental.

## Defaults left alone
All ToneShaper filter/drive/fold, pitch-envelope, morph, macros, secondary-shell, coupling, choke/bus, and the three other exciters' params (FM/Feedback/Friction — no-ops with NoiseBurst) are at neutral/bypass defaults. Full reasons in `defaultedParams`.

## Sources
- Rossing, *The Normal Modes of Cymbals* — modal count/inharmonicity. https://scholarship.rollins.edu/cgi/viewcontent.cgi?article=1020&context=stud_fac
- Stowell, *Cymbal Synthesis Tutorial* — noise-driven resonator banks, 300 Hz–20 kHz, thwack attack. https://mcld.co.uk/cymbalsynthesis/
- *Chaotic vibrations & acoustic properties of cymbals*, ScienceDirect 2022. https://www.sciencedirect.com/science/article/pii/S2590123022000895
- *Crash cymbal* (mode density ∝ f², >3 kHz shimmer). https://grokipedia.com/page/Crash_cymbal
- Sound On Sound, *Analysing Metallic Percussion*. https://www.soundonsound.com/techniques/analysing-metallic-percussion
- ASA, *Vibrational modes of cymbals and Chladni's law*. https://www.auditory.org/asamtgs/asa96haw/5pMU/5pMU8.html
