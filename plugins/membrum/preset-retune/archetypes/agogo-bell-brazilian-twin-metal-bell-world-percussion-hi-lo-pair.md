# Membrum Recipe — Agogo Bell (world, hi/lo pair)

**Body:** Bell (church-bell Chladni bank, 16 modes; nominal f0 = 800·0.1^size, hum at 0.25·f0)
**Exciter:** FMImpulse (Chowning bell-FM; modulator ratio = 1 + 3·fmRatio, index 0.5–3.0 rad velocity-driven)

> All values below are **normalized [0,1]** (the on-wire / preset representation). Physical denormalized target is given for each. Voiced against the **post-audit corrected** signal path (measured-strike body norm / N-1, `mode_inject` 1/k, per-mode decaySkew tilt on all bodies, L-3 FM index fix, M-9 per-pad pan).

## Why Bell + FMImpulse
The agogo is the **highest-pitched instrument of the samba bateria** — a struck conical sheet-iron bell with a **clear, definite pitch** ([Wikipedia](https://en.wikipedia.org/wiki/Agog%C3%B4); [Center for World Music](https://centerforworldmusic.org/2015/02/world-music-instruments-agogo/)). That rules out the dense free-plate Chladni wash of a cymbal (Plate) and calls for the pitched Bell partial set. The established **electronic** synthesis of this family (TR-808/CR-78 cowbell, agogo) is **two metal-timbre sources at a non-integer interval** — Sound On Sound gives the cowbell as 587 Hz + 845 Hz, **ratio 1:1.44**, warning "even small deviations destroy the illusion" ([SoS](https://www.soundonsound.com/techniques/synthesizing-cowbells-claves)). FMImpulse delivers exactly this inharmonic metallic clang. The agogo is **brighter, higher, and clearer-pitched than the cowbell**, captured by pushing the FM modulator ratio well above 1.44 into ~2.6–3.2.

## Pair layout (two pads)
| | HI bell | LO bell |
|---|---|---|
| **FM Ratio** | 0.72 → mod ratio **3.16** | 0.55 → mod ratio **2.65** |
| **Size** | 0.14 → f0 ≈ **580 Hz** | 0.22 → f0 ≈ **480 Hz** |
| **Decay** | 0.28 (shorter) | 0.35 (slightly longer) |
| **Pan** | 0.42 (slightly L) | 0.58 (slightly R) |

The hi/lo **Size** offset realizes the ~major-third interval between the twin bells (interval per [organology.net](https://organology.net/instrument/agogo/) / Wikipedia; Brazilian practice favors individuality over exact tuning).

## Baseline params (HI bell shown; LO deltas above)
| Param | Norm | Physical target | Rationale |
|---|---|---|---|
| Exciter Type | 0.80 | FMImpulse (enum 4) | Canonical agogo/cowbell metallic FM transient |
| Body Model | 0.80 | Bell (enum 4) | Clear pitched bell, not cymbal Plate |
| FM Ratio | 0.72 | mod ratio 3.16 | Bright inharmonic clang, above cowbell's 1.44 |
| Size | 0.14 | f0 ≈ 580 Hz | Small thin conical bell → high pitch |
| Material | 0.85 | brightness ≈ 0.955, base decay ≈ 1.25 s | Thin sheet iron → metallic, bright, low b3 |
| Decay | 0.28 | ≈ 0.42× base → few-hundred-ms ring | Internal muffling stickers → short pitched ring |
| Body Damping b1 | 0.30 | ≈ 15 s⁻¹ flat damping | Moderate RT60 floor for a short pitched bell |
| Body Damping b3 | 0.00 | b3 = 0, no f² roll-off | Metal sustains high partials → bright shimmer |
| Strike Position | 0.30 | azimuth 0.47 rad, |cos(m·θ)| weighting | Beater near rim soundbow, balanced partials |
| Level | 0.75 | linear gain 0.75 | Bright cutting percussion |
| Click Mix | 0.55 | 0.55·vel·env | Hard beater on sheet iron → metallic ting |
| Click Brightness | 0.85 | BP center ≈ 5.6 kHz | Sharp metallic tick |
| Click Contact | 0.30 | ≈ 2.9 ms | Tight hard-beater contact |
| Noise Mix | 0.00 | layer bypassed | Clean pitched metal — no hiss |
| Air Loading | 0.00 | (no-op on Bell) | Not an air-loaded drumhead |
| Mode Stretch | 0.45 | physical 1.175 | Small conical bell slightly stretched/inharmonic |
| Decay Skew | 0.40 | bipolar −0.2, lifts upper partials | Brighten the metallic top |
| Mode Scatter | 0.12 | ~1.8% dither | Hand-rolled imperfection, keeps clear pitch |
| Pan | 0.42 | slightly left | Twin-bell stereo spread (LO 0.58) |
| Macro Brightness | 0.65 | +cutoff/+modeInject delta | Emphasize bright metallic top |

## Deliberately defaulted (physical reasons)
- **Tension Mod = 0** — agogos have static pitch; tension glide is Membrane-only anyway.
- **Pitch Env (all) — Time = 0** — no 808-style glide on a struck bell.
- **Drive / Fold / Mode Inject / Nonlinear Coupling = 0** — FM + Bell bank already supply the metallic spectrum; keep the hit clean and exact-bypassed.
- **Coupling Strength / Secondary = off** — a single resonating bell, no separate coupled shell.
- **Morph = off, Choke = 0, Output Bus = 0, macros neutral** — explicit body/exciter params define the voice.
- **Noise Cutoff/Res/Decay/Color, NoiseBurst Dur, Friction Pressure, Feedback Amt** — no-ops for this body/exciter/layer config.

## Sources
[Agogô — Wikipedia](https://en.wikipedia.org/wiki/Agog%C3%B4) · [Agogo Bells — Wikipedia](https://en.wikipedia.org/wiki/Agogo_Bells) · [Organology: Agogô](https://organology.net/instrument/agogo/) · [Kalango Sambapedia: Agogô](https://kalango.com/en/samba-service/sambapedia/instruments/agogo/) · [Center for World Music](https://centerforworldmusic.org/2015/02/world-music-instruments-agogo/) · [Sound On Sound — Synthesizing Cowbells & Claves](https://www.soundonsound.com/techniques/synthesizing-cowbells-claves) · [CCRMA — FM Synthesis 2/2](https://ccrma.stanford.edu/software/clm/compmus/clm-tutorials/fm2.html) · [Hibberts — Identifying Bell Partials](https://www.hibberts.co.uk/identifying-bell-partials/) · [Partial Frequencies and Chladni's Law in Church Bells (Open Univ.)](https://oro.open.ac.uk/40358/7/40358.pdf) · [Plugin Boutique — FM Synthesis Cookbook](https://www.pluginboutique.com/articles/1873-FM-Synthesis-Cookbook-Five-Classic-FM-Sounds-and-How-They-Work)