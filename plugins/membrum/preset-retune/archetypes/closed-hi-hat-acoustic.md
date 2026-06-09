# Membrum Recipe — Closed Hi-Hat (acoustic)

**Body:** NoiseBody (off1 = norm **1.0**, enum 5) · **Exciter:** NoiseBurst (off0 = norm **0.4** → idx 2)

## Why this archetype maps this way
Two cymbals clamped shut, struck with a stick tip, produce a short, dense, **inharmonic, high-frequency "chick"** with no definite pitch. Cymbal modes are "far from harmonic" with hundreds of energised modes; at high amplitude the spectrum is effectively noise (Fletcher & Rossing, *The Physics of Musical Instruments* ch.20; Fletcher 2012 *Order from Complexity*; *Sound on Sound* "Analysing Metallic Percussion"). The clamp kills sustain → the TR-808 service manual fixes its closed-hat decay at **50 ms** (vs 350–1200 ms open/cymbal). The classic electronic emulation (TR-808/606) builds the metal tone from **six inharmonically-tuned square oscillators** (606: 246/308/367/418/440/627 Hz) bandpass-filtered (~3.4 kHz body, ~7.1 kHz shimmer), high-passed, and gated short — the direct analogue of Membrum's **32 plate-ratio inharmonic modes (NoiseBody) + a bright violet-noise bed + a short envelope** struck by a 3 ms NoiseBurst.

**NoiseBody** is chosen over Plate because it bundles the inharmonic plate-ratio modal stack *and* an internal bright noise layer — exactly the dense metallic shimmer + hiss of a clamped cymbal. **NoiseBurst** is chosen because the excitation is a short filtered-noise stick contact, not a tonal impulse.

## Baseline (normalized → physical)
| Param | Norm | Physical target |
|---|---|---|
| Body Model | 1.0 | NoiseBody (enum 5) |
| Exciter Type | 0.4 | NoiseBurst |
| Material | 0.85 | brightness 0.955, noise-bias 5.75 kHz, reso 0.94 — bright bronze |
| Size | 0.05 | f0 ≈ 1337 Hz; modal stack ~1.3–12 kHz |
| Decay | 0.10 | noise env ≈ 23 ms; modal tail choked → ~40–90 ms |
| Strike Position | 0.6 | off-center bow strike, brighter modes |
| Level | 0.72 | sits under kick/snare |
| Mode Stretch | 0.5 | mild extra inharmonicity |
| Mode Scatter | 0.25 | ~3–4% organic detune |
| Choke Group | 0.125 | group 1 (closed/pedal/open mutual mute) |
| NoiseBurst Duration | 0.077 | 3 ms contact burst |
| Noise Mix | 0.85 | heavy bright noise bed |
| Noise Cutoff | 0.90 | ~9.8 kHz lowpass (5–10 kHz sparkle) |
| Noise Resonance | 0.2 | Q 1.24 (gentle, non-pitched) |
| Noise Decay | 0.10 | ~32 ms tail |
| Noise Color | 0.85 | Violet (+6 dB/oct) |
| Click Mix | 0.15 | small stick tick |
| Click Brightness | 0.85 | ~6.4 kHz bandpass tick |
| Pan | 0.62 | slightly right (kit image) |

All other params are deliberately left at default (see deferred list): pitch-env (no glide on a clamped cymbal), ToneShaper filter/drive/fold (bypassed), Mode Inject / Nonlinear Coupling / Morph (would add unwanted tonality/sustain), secondary shell + tension mod (no coupled body, membrane-only glide), and the macros (neutral, left as live offsets).

## Kit-brightness variation (same instrument)
- **Bright jazz/rock hat:** Material → ~0.95, Noise Cutoff → ~0.93 (~12 kHz), Noise Color Violet.
- **Warm/wood/dark hat:** Material → ~0.70, Noise Cutoff → ~0.78 (~4.6 kHz), Noise Color White (~0.7), Noise Mix → ~0.70.
- **Tighter/looser closed:** Decay & Noise Decay across 0.07–0.15.

## Citations
Baratatronix 606 & 808 hi-hat synthesis; Fletcher (2012) *Order from Complexity*; Fletcher & Rossing *Physics of Musical Instruments* ch.20; SoS "Analysing Metallic Percussion"; Musical-U / emastered / musicguymixing hi-hat EQ; MOD Wiggler 808 decay thread (50 ms closed-hat); Wikipedia Hi-hat.
