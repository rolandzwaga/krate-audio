# Membrum Recipe - Concert Bass Drum (Orchestral Gran Cassa)

**Body:** Membrane (48-mode Bessel circular drumhead)  
**Exciter:** Mallet (soft felt beater)  
**Character:** A very large, low-tuned, air-loaded double-headed drum struck with a single heavy soft felt mallet. Low broadband thud with no defined pitch, a slow descending boom-glide, and a long resonant bloom - longer and softer than a kit kick.

## Acoustic basis (cited)
- ~40 in x 20 in double-headed drum, "a note of low definite or **indefinite pitch**," played with a single heavy felt mallet (Wikipedia / Gran cassa).
- Ideal circular-membrane modes are **inharmonic** (no definite pitch): (0,1)=1.000, (1,1)=1.593, (2,1)=2.135, (0,2)=2.295, (1,2)=2.917, (0,3)=3.598 (Russell, PSU). Audible orchestral modes are the low diametric family.
- Example bass-drum low modes 50 / 93 / 136 / 179 Hz, ~43 Hz fundamental; on strike pitch **rises briefly then falls a couple semitones** as head tension relaxes; HF partials last only "a few ms" after impact (Sound on Sound).
- Thin-shell, free-suspended, calf/coated heads chosen for "**lush decay / long sustain / broad resonant bloom**"; soft mallet = "big, fluffy tone" (rareinstrument concert-bass-drum guide).

## Mapping rationale
- **Membrane + Size 0.90** -> f0 ~= 63 Hz natural ( (1,1) ~= 100 Hz ) = low, indistinct pitch.
- **Air Loading 0.80** depresses the lowest Bessel modes (Rossing 1982) for a deeper, less-whistly, real-double-head feel.
- **Pitch env 110 -> 40 Hz / ~120 ms, exponential drop** renders the descending boom; **Tension Mod 0.12** adds the gentle energy-driven upward "kerthump" (Membrane-only, Avanzini 2012) - small because the beater is soft.
- **Material 0.30 + b3 0.55** = woody/skin spectral tilt (highs die first, no metal shimmer). **Mode Stretch / Decay Skew / Mode Inject / Nonlinear / Drive left neutral-or-off** to keep it a clean, pitchless skin head (stretching or injecting would falsely give it pitch or a metallic edge).
- **Mallet exciter + dull click** (brightness 0.20 ~= 453 Hz center, contact ~3.3 ms, mix 0.35) = soft felt "whump," not a stick tick.
- **Dark Brown noise** (mix 0.18, cutoff ~280 Hz, ~190 ms) = subtle low air/body rumble.
- **Secondary shell OFF** (per spec), **Pan center**.

## Key normalized values
| Param | Norm | Physical |
|---|---|---|
| Exciter | 0.20 | Mallet |
| Body | 0.00 | Membrane |
| Material | 0.30 | woody skin |
| Size | 0.90 | ~63 Hz f0 |
| Decay | 0.55 | medium-long bloom |
| Strike Pos | 0.35 | off-center r/a~0.31 |
| Level | 0.85 | loud single stroke |
| Air Loading | 0.80 | strong Rossing depression |
| PitchEnv Start | 0.370 | ~110 Hz |
| PitchEnv End | 0.150 | ~40 Hz |
| PitchEnv Time | 0.24 | ~120 ms (enables glide) |
| PitchEnv Curve | 0.15 | exp drop (-0.7) |
| Tension Mod | 0.12 | gentle upward kerthump |
| Click Mix | 0.35 | soft beater contact |
| Click Contact | 0.42 | ~3.3 ms |
| Click Brightness | 0.20 | ~453 Hz (dull) |
| Noise Mix | 0.18 | low rumble |
| Noise Cutoff | 0.30 | ~280 Hz dark |
| Noise Color | 0.10 | Brown |
| Noise Decay | 0.45 | ~190 ms |
| Body Damping b3 | 0.55 | wood/skin HF damping |
| Secondary Enabled | 0.00 | off |
| Pan | 0.50 | center |

All other params left at archetype defaults (see defaulted list): mode-stretch/skew/inject/nonlinear/drive/fold off, tone-shaper filter bypassed, macros neutral, b1 derived from Decay, FM/feedback/friction/noiseburst exciter params inert for the Mallet exciter, choke/bus default.

## Sources
- https://en.wikipedia.org/wiki/Gran_cassa
- https://www.soundonsound.com/techniques/synthesizing-drums-bass-drum
- https://rareinstrument.com/concert-bass-drum/
- https://www.acs.psu.edu/drussell/Demos/MembraneCircle/Circle.html
- https://wtt.pauken.org/chapter-2/vibrating-circular-membranes
- https://www.idrumtune.com/mixing-drums-know-your-drum-frequencies/