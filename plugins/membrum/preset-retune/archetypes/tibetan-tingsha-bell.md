# Membrum Recipe — Tibetan Tingsha (Bell)

**Body:** Bell &nbsp;|&nbsp; **Exciter:** Impulse &nbsp;|&nbsp; **Layers:** bright Click only, NO noise

A pair of small (2.25–4 in), very thick cast-bronze cymbal-bells struck rim-to-rim: a high, pure, inharmonic metallic tone that rings 10–20+ seconds and purifies toward its principal partials as it decays. The two near-identical cymbals couple sympathetically, extending and shimmering the ring.

## Acoustic profile (researched)
- **Pitch / fundamental:** high — vendor & sound-healing sources put the dominant tone "above 2000 Hz," 2500 Hz quoted for one instrument. Small thick disc ⇒ modes sit high (Himalayas Shop; Wikipedia).
- **Partials / inharmonicity:** free-edged thick metal disc/bell hybrid ⇒ **inharmonic** partials (the metallic signature), sparser and more tonal than a large crash; cymbal modal data gives bronze inharmonicity α≈0.3–0.8 (oemcymbal/arborea; Rossing "normal modes of cymbals").
- **Decay / T60:** the defining trait — authentic tingsha "linger 10–15 s," some "20 s–1 min." Low/principal partials carry the long tail; high partials decay faster ⇒ the tone purifies (healing-sounds; highvibrationstation).
- **Transient:** short, **bright metallic tink** from bronze rim-on-rim contact, then the sustained ring.
- **Noise:** essentially none — tonal modes, not broadband noise.
- **Pitch glide:** none (no 808 boom). Mild amplitude-nonlinearity (harder = brighter) only.

## Why Bell body + Impulse
Bell gives the tuned, sparse, inharmonic bronze partial set (`f0_nominal = 800·0.1^size`; hum at 0.25·f0). At **size 0.12 → 607 Hz nominal**, with upper partials reaching ~7.3 kHz — the high, pure band a tingsha occupies. Impulse is the hard, abrupt strike that excites all modes at once.

## Key normalized values (→ physical)
| Param | Norm | Physical target |
|---|---|---|
| Body Model | 0.80 | Bell (idx 4) |
| Exciter Type | 0.00 | Impulse |
| Size | 0.12 | f0_nominal 607 Hz; partials 152 Hz–7.3 kHz |
| Material | 0.95 | brightness 0.985 (very metallic, min b3) |
| Decay | 0.92 | ~2.66× on 1.35 s base; long tail |
| Body Damping b1 | 0.08 | 4.2 s⁻¹ flat damping (tens-of-sec ring) |
| Body Damping b3 | 0.04 | 4e-5 s (metallic, long highs) |
| Mode Stretch | 0.62 | phys 1.43 → inharmonic metallic spacing |
| Decay Skew | 0.38 | −0.24 → lift upper partials, trim hum |
| Nonlinear Coupling | 0.22 | velocity → brighter struck-metal |
| Mode Scatter | 0.12 | ~1.8% detune (paired-cymbal shimmer) |
| Strike Position | 0.15 | azimuth near meridional antinode |
| Noise Mix | 0.00 | noise layer OFF |
| Click Mix | 0.40 | bronze contact tink |
| Click Contact | 0.20 | 2.6 ms |
| Click Brightness | 0.85 | ~7.2 kHz center (sharp metal tick) |
| Level | 0.72 | clean pre-limiter level |
| PitchEnv Time | 0.00 | pitch env disabled |
| Filter Cutoff | 1.00 | 20 kHz → ToneShaper bypassed |

## Deliberately default
PitchEnv (off), Morph (off), all 5 macros (neutral 0.5), Drive/Fold (0), Air Loading (0, no-op on Bell), Choke/Output (kit routing), all exciter-secondary params (FM/Feedback/NoiseBurst/Friction — no-op for Impulse), Secondary shell + Tension Mod (no shell/membrane on a tingsha; the two-cymbal coupling is modeled via Mode Scatter + long decay), Noise filter params (no-op while Noise Mix=0), Pan (center).

## preset_generator.cpp field mapping
```cpp
pads[p].exciterType = ExciterType::Impulse;   // 0
pads[p].bodyModel   = BodyModelType::Bell;     // 4
pads[p].material = 0.95; pads[p].size = 0.12; pads[p].decay = 0.92;
pads[p].strikePosition = 0.15; pads[p].level = 0.72;
pads[p].modeStretch = 0.62; pads[p].decaySkew = 0.38;
pads[p].modeInjectAmount = 0.0; pads[p].nonlinearCoupling = 0.22;
pads[p].modeScatter = 0.12; pads[p].airLoading = 0.0;
pads[p].bodyDampingB1 = 0.08; pads[p].bodyDampingB3 = 0.04;
pads[p].noiseLayerMix = 0.0;
pads[p].clickLayerMix = 0.40; pads[p].clickLayerContactMs = 0.20; pads[p].clickLayerBrightness = 0.85;
pads[p].tsFilterCutoff = 1.0; pads[p].tsPitchEnvTime = 0.0;
// pan/enabled/macros/exciter-secondary left at struct defaults
```

## Sources
Wikipedia Tingsha · Himalayas Shop (2500 Hz listing + guide) · healing-sounds.com (resonance/sympathetic coupling) · highvibrationstation / royalfurnish (10–20+ s sustain) · oemcymbal / arborea (cymbal modal inharmonicity α≈0.3–0.8) · ResearchGate "The normal modes of cymbals" · Sound on Sound "Synthesizing Bells".