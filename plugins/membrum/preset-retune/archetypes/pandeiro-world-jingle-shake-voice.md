# Membrum Recipe — Pandeiro (world): jingle/shake voice

**Body:** NoiseBody (32 plate-ratio inharmonic modes + internal noise) · **Exciter:** NoiseBurst (violet-noise bandpass contact burst)

Voices ONLY the bright metal **shake/jingle** (platinela) component of the Brazilian frame drum — a short, dry, broadband-HF rattle. Deliberately distinct from a riq-style membrane-plus-jingle patch (no membrane body here).

## Acoustic basis
- The pandeiro is **largely unpitched and percussive**; the jingling predominates and masks the head tone ([Wikipedia: Tambourine](https://en.wikipedia.org/wiki/Tambourine); [Wikipedia: Pandeiro](https://en.wikipedia.org/wiki/Pandeiro)).
- Cupped **platinelas** → **crisper, drier, less sustained** than a tambourine ([Wikipedia: Pandeiro](https://en.wikipedia.org/wiki/Pandeiro); [CapoeiraWiki](https://capoeirawiki.org/wiki/Pandeiro)).
- Jingles are thin **steel (very bright)** or **brass (darker)** disc pairs colliding chaotically → dense inharmonic metal cluster ([Grokipedia: Tambourine](https://grokipedia.com/page/Tambourine)).
- Spectrum is **broadband, noise-like, HF-transient-dominated** ([Grokipedia: Tambourine](https://grokipedia.com/page/Tambourine)). Established metallic-percussion synthesis filters white/violet noise through a high band (606/808 cymbal+hat ~7100 Hz; general hat practice 5–10 kHz) ([baratatronix](https://www.baratatronix.com/blog/606-cymbal-and-hi-hat-synthesis); [KVR](https://www.kvraudio.com/forum/viewtopic.php?t=246527); [ADSR](https://www.adsrsounds.com/fm8-tutorials/drum-synthesis-with-fm8-creating-a-hi-hat-sound/)).
- **No pitch glide** (that is a tom/membrane "kerthump", absent in jingles).

## Membrum mapping
NoiseBody supplies the inharmonic metal cluster + internal bright noise; a short NoiseBurst supplies the metal contact transient; the **parallel Noise Layer (violet, high cutoff, short decay) is the dominant shimmer carrier**; raised mode-scatter gives the chaotic many-disc detune. Pitch env, tension mod, coupling, mode-inject, drive/fold, click layer all OFF.

## Normalized baseline (every meaningful param)
| Param | Norm | Physical target | Why |
|---|---|---|---|
| Exciter Type | 0.40 | NoiseBurst | metal contact burst per stroke |
| Body Model | 0.90 | NoiseBody | inharmonic metal cluster + noise |
| Material | 0.90 | brightness 0.97, int-noise 6 kHz | bright steel/brass jingle |
| Size | 0.12 | cluster base ≈1140 Hz | unpitched; keep cluster bright |
| Decay | 0.10 | modal ≈0.32 s, int-noise ≈21 ms | dry, less-sustained cupped jingle |
| Strike Position | 0.30 | plate amp @0.3 | balanced bright cluster |
| Level | 0.72 | linear 0.72 | accent layer under the hats |
| Filter Type | 0 | LP | (filter bypassed) |
| Filter Cutoff | 1.00 | 20 kHz → BYPASS | tone done in body+noise |
| Filter Resonance | 0 | Butterworth | no-op |
| Filter Env Amount | 0.50 | 0 | no sweep |
| Drive | 0 | bypass | clean metal |
| Fold | 0 | bypass | n/a |
| PitchEnv Time | 0 | OFF | no glide |
| Mode Stretch | 0.3333 | 1.0 (neutral) | native plate inharmonicity |
| Decay Skew | 0.50 | 0 | even spectrum |
| Mode Inject | 0 | bypass | keep unpitched |
| Nonlinear Coupling | 0 | bypass | clean |
| Choke Group | 0.125 | group 1 | strokes mutually choke (hat-like) |
| NoiseBurst Duration | 0.40 | 7.2 ms | crisp articulate contact (user) |
| Noise Mix | 0.80 | dominant layer | jingle shimmer carrier |
| Noise Cutoff | 0.92 | ≈10.8 kHz LP | bright HF band (user) |
| Noise Resonance | 0.20 | q 1.24 | broadband, mild |
| Noise Decay | 0.18 | ≈42 ms | short dry tail |
| Noise Color | 0.88 | Violet | HF-tilted metal (user) |
| Click Mix | 0 | silent | burst already supplies transient |
| Air Loading | 0 | n/a | membrane-only |
| Mode Scatter | 0.45 | ~7% dither | chaotic many-disc rattle (user) |
| Pan | 0.50 | center | default placement |
| Pad Enabled | 1.0 | ON | active |

## Implementation note (preset generator `Pad`)
Closest existing template is **pad 6 (closed hat)** in a NoiseBody+NoiseBurst kit: copy that pattern and set `material=0.90, size=0.12, decay=0.10, level=0.72, noiseBurstDuration=0.40, noiseLayerMix=0.80, noiseLayerCutoff=0.92, noiseLayerColor=0.88, noiseLayerDecay=0.18, noiseLayerResonance=0.20, modeScatter=0.45, clickLayerMix=0.0, airLoading=0.0, chokeGroup=1`. Leave pitch-env, coupling, secondary, tension, morph, drive/fold at defaults.
