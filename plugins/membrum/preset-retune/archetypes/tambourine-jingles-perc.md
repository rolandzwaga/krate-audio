# Membrum Recipe — Tambourine (jingles), perc

## Archetype summary
A tambourine's voice is dominated by its **zils (jingles)** — pairs of small thin brass/steel/bronze discs that behave like tiny free-edged cymbals. Struck or shaken, the discs collide and radiate a **broadband, noise-like, HF-dominated shimmer** (energy concentrated 5–10 kHz) with **inharmonic** metallic ring and **no definite pitch**; a faint head tap supplies a low component above ~200 Hz. The attack is a sharp metallic contact tick; a single hit is short (~150–250 ms), though rolls/shakes sustain. This maps to **NoiseBody + NoiseBurst**, with a bright violet noise layer for the shimmer, a click layer for the tap, high mode-scatter for the multi-disc cluster, and a **metallic Shell secondary** for the discrete inharmonic jingle ring.

## Body & exciter
- **Body: NoiseBody** — plate-ratio inharmonic modal bank (32 metallic modes) + internal noise layer; the right model for a no-definite-pitch broadband idiophone.
- **Exciter: NoiseBurst** — short violet-noise bandpass burst = many discs rattling; HF-weighted contact, not a clean impulse.
- **Secondary: Shell (enabled)** — small (secondarySize 0.20), bright/metallic (secondaryMaterial 0.85), driven at coupling 0.40, so a discrete inharmonic jingle ring sits on top of the broadband body.

## Physical targets (key denormalizations)
| Param | Norm | Physical |
|---|---|---|
| Body Model | 1.0 | NoiseBody |
| Exciter | 0.4 | NoiseBurst |
| Size | 0.20 | f0 ≈ 946 Hz |
| Material | 0.85 | brightness 0.955, internal noise cutoff ≈ 5.75 kHz |
| Decay | 0.22 | ~0.46× body base (short) |
| Noise Mix | 0.78 | dominant parallel noise |
| Noise Cutoff | 0.92 | ≈ 11.9 kHz lowpass |
| Noise Color | 0.92 | Violet (brightest) |
| Noise Decay | 0.22 | ≈ 55 ms |
| Click Mix / Brightness | 0.30 / 0.85 | tick at ≈ 6.7 kHz |
| Mode Scatter | 0.55 | ~8% dither (disc cluster) |
| b1 / b3 | 0.32 / 0.0 | ~16 s⁻¹ flat / no f² roll-off (metallic highs) |
| Secondary (en/size/mat) | 1 / 0.20 / 0.85 | shell f0 ≈ 804 Hz, bright metal |
| NoiseBurst Duration | 0.30 | ≈ 6 ms |
| Macro Complexity | 0.65 | thicken cluster |
| Level / Pan | 0.72 / 0.5 | accent, centred |

## Deliberate defaults
ToneShaper filter/Drive/Fold (off), all pitch-envelope params (idiophone, no glide), Mode Stretch/Decay Skew (neutral — scatter+ratios already inharmonic), Mode Inject & Nonlinear Coupling (≈0 — no imposed pitch / dynamic brightening), Morph (static timbre), Tension Mod (Membrane-only), Air Loading (Membrane-only), Choke/Bus (none), and the exciter-specific FM/Feedback/Friction params (no-ops for NoiseBurst).

## Sources
- Sound on Sound — *Analysing Metallic Percussion* (energy migrates upward to a few kHz; HF modes shortest-lived)
- Grover Pro — *Tambourine Jingles* (brass/copper/bronze/beryllium-copper, brightness, sustain)
- SoundShock Audio — *How to EQ a Tambourine* (5–10 kHz presence/harshness, head>200 Hz)
- Wikipedia — *Tambourine*, *Inharmonicity*, *Harmonic series*; HandWiki *Overtone* (cymbals/tam-tams inharmonic, no implied pitch)
- MusicRadar — alternative drum synthesis (noise-based metallic perc)

> All values above are NORMALIZED [0,1] preset/on-wire values; physical targets per the corrected post-audit (2026-06-07) semantics. This recipe matches the post-audit reference voicing of the Percussion-kit Tambourine pad and is voiced against the corrected gain-staging, measured-strike body norm, plate Chladni ratios, and per-mode decaySkew/mode_inject 1/k behaviour.