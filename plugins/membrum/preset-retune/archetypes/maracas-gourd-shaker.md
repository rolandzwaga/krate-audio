# Membrum Recipe — Maracas (Gourd Shaker)

**Body:** NoiseBody (plate-ratio cavity + internal noise) · **Exciter:** NoiseBurst (stochastic seed-impact grains)

## Physical rationale
A maraca is a dried gourd holding ~20-40 seeds. It has **no definite pitch and no harmonic partial series** — the sound is a dense stochastic stream of seed-on-wall impacts, each a broadband click, colored by the gourd's air/shell resonance. Perry Cook's PhISEM/STK model is the authoritative synthesis reference:

- **~25 beans** (STK `MARACA_NUM_BEANS=25`), Poisson-distributed collisions, each emitting a **short exp-decaying white-noise grain** (`MARACA_SOUND_DECAY=0.95`, a few ms).
- **One gourd resonance at 3200 Hz**, pole radius **0.96** (`MARACA_RADII`) — a fairly focused, moderately high-Q peak, not a broadband sizzle.
- Long overall shake envelope (`MARACA_SYSTEM_DECAY=0.999`, several hundred ms of decay per shake), soft AR attack (no hard click).

**Maraca vs. cabasa (the distinguishing cue):** the cabasa in the same model sits at 3000 Hz but with pole radius only **0.70** and **512 beads** — far more, smaller, metallic impacts and a broader/brighter low-Q resonance => the cabasa is a dense bright sizzle, the maraca a sparser, **darker, more focused 'shh'**. We reproduce 'darker' both by fewer/longer impacts (longer NoiseBurst) and by keeping the audible band lower/narrower (Pink color, ~1.3 kHz lowpass) vs the bright 3 kHz cabasa sizzle.

The TR-808 maracas confirm the recipe shape: **highpassed white noise -> VCA with an AR envelope** (rise ~3/4 of total), low end removed, ~25-35 ms — pure noise + band-shaping + soft envelope, no body resonator and **no pitch glide**.

## Parameters (all values NORMALIZED [0,1])

| Param | Norm | Physical target |
|---|---|---|
| Exciter Type | 0.40 | NoiseBurst |
| Body Model | 0.80 | NoiseBody |
| Material | 0.35 | internal-noise resonance ~3.25 kHz (gourd peak), reso 0.74 |
| Size | 0.40 | cavity f0 ~597 Hz (thin/high) |
| Decay | 0.45 | ~unity body decay, internal noise ~56 ms |
| Strike Position | 0.50 | neutral plate excitation |
| Level | 0.70 | supporting-percussion level |
| Mode Scatter | 0.50 | ~7.5% dither -> de-tonalize the cavity |
| NoiseBurst Duration | 0.45 | ~8.1 ms impact-stream burst |
| Noise Mix | 0.60 | dominant 'shh' (maraca ~100% noise) |
| Noise Cutoff | 0.55 | lowpass ~1.3 kHz (below cabasa 3 kHz) |
| Noise Resonance | 0.35 | Q ~1.95 (focus, no ring) |
| Noise Decay | 0.40 | ~130 ms AR-ish tail |
| Noise Color | 0.50 | **Pink** (darker/softer than cabasa White/Violet) |
| Click Mix | 0.10 | near-silent — no hard contact tick |
| Click Contact | 0.30 | ~2.9 ms (inert at low mix) |
| Click Brightness | 0.50 | dull ~1.5 kHz if audible |
| Pan | 0.50 | center |
| Pad Enabled | 1.00 | on |

## Deliberately defaulted
ToneShaper filter/drive/fold (clean noise, no saturation); entire **PitchEnv family** (PitchEnv Time=0 — no glide); Mode Stretch / Decay Skew (neutral — no tonality to tilt); Mode Inject & Nonlinear Coupling (exact-bypass at 0 — adding tonal harmonics would re-pitch the gourd); Material Morph (static timbre); Choke/Output Bus (no pairing/routing); FM/Feedback/Friction secondary params (no-ops vs NoiseBurst); Body Damping b1/b3 (sentinel-derive); Air Loading & Tension Mod (Membrane-only no-ops); Secondary shell (no head/shell structure).

## Sources
- STK `Shakers.cpp` (PhISEM maraca/cabasa constants): https://github.com/thestk/stk/blob/master/src/Shakers.cpp
- Cook, *Model of the Maracas* (CCRMA): https://ccrma.stanford.edu/~juanig/articles/wadi-icmc/Model_Maracas.html
- Cook, *Principles for Designing Computer Music Controllers* (PhISEM, NIME 2001): https://www.nime.org/proceedings/2001/nime2001_003.pdf
- TR-808 maracas synthesis (highpass noise + AR env): https://www.baratatronix.com/blog/808-maracas-synthesis
- RTcmix MSHAKERS (PhISEM p-fields): http://sites.music.columbia.edu/cmc/RTcmix/OLD/RTcmix-3.6/docs/instruments/MSHAKERS.html