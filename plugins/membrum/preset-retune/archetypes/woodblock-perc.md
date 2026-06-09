# Membrum Recipe — Woodblock (perc)

**Body:** Plate (free-plate Chladni bank) **Exciter:** Impulse (raised-cosine click)

## Why this archetype maps this way
A woodblock is a single piece of hardwood (teak/rosewood/maple) with one or two longitudinal **slit cavities** that act as a resonator ([Wikipedia](https://en.wikipedia.org/wiki/Woodblock_(instrument))). It is struck with a hard wooden stick and produces a **"dry, short, bright, sharply defined… more tok than ring"** crack with **"fast and clean"** release and **"no rattle, buzz, or loose after-sound"** ([rareinstrument](https://rareinstrument.com/woodblock-drum/)). It has **indefinite/relative pitch** — hi/lo is a size difference — but a clearly perceived bright resonant tok with a couple of strong low modes.

Struck wood vibrates as a **free body**, so its partials are strongly **inharmonic** — the canonical free-free bar gives ~**1 : 2.76 : 5.40** ([Euphonics 3.3](https://euphonics.org/3-3-marimbas-and-xylophones/); [ResearchGate – xylophone bar physics](https://www.researchgate.net/publication/243492369_Basic_physics_of_xylophone_and_marimba_bars)). Untuned wood is "naturally discordant"; a woodblock is **not** undercut like a tuned xylophone bar, so it keeps that raw inharmonic spread. The slit cavity "lowers the perceived body" and concentrates the energy into a couple of strong low modes.

Membrum mapping:
- **Plate body** — the post-audit free-plate `(m+2n)^P` Chladni spectrum, with **Mode Stretch ≈ 0.50** pushing partials sharp toward the inharmonic free-bar fingerprint. (Chosen over Shell, whose strict 1:2.76:5.40 ladder is the *tuned-bar* idealisation; the slit-cavity woodblock is low-mode-dominant + inharmonic, which Plate+stretch captures.)
- **Impulse exciter** — hard wooden stick = near-instant hard contact; no beater softness, no friction, no FM.
- **Short flat damping (b1 ≈ 25 s⁻¹ → ~40 ms floor)** for the fast dry "tok"; **light b3 (1e-4 s)** so the highs still ring bright instead of dulling to a thud.
- **Bright sharp Click** (≈4.9 kHz, ~2.4 ms) for the defining wooden crack; **Noise OFF** (the woodblock's signature is the *absence* of sustained noise/buzz).
- **No pitch glide, no coupling, no morph** — a static, dry single hit.

## Baseline (NORMALIZED [0,1]) — meaningful params
| Param | Norm | Physical target |
|---|---|---|
| Exciter Type | 0.0 | Impulse |
| Body Model | 0.0* | Plate (*selector is body-relative; intended body = Plate) |
| Material | 0.32 | brightness 0.66, woody tilt that still rings |
| Size | 0.18 | f0 ≈ 529 Hz (hi); lo variant 0.28 → ~420 Hz |
| Decay | 0.18 | 0.45× body-base; floor set by b1 override |
| Strike Position | 0.30 | off-centre face strike |
| Level | 0.74 | per-pad gain under the bus limiter |
| Filter Cutoff | 1.0 | 20 kHz → filter bypassed |
| Filter Env Amount | 0.5 | 0 = no modulation |
| Drive | 0.0 | bypass |
| Fold | 0.0 | bypass |
| PitchEnv Time | 0.0 | pitch env DISABLED |
| Mode Stretch | 0.50 | physical 1.25 → inharmonic free-bar spread |
| Decay Skew | 0.5 | neutral |
| Mode Inject | 0.0 | bypass (inharmonic body, no integer series) |
| Nonlinear Coupling | 0.0 | bypass |
| Morph Enabled | 0.0 | off |
| Choke Group | 0.0 | none |
| Output Bus | 0.0 | main |
| Noise Mix | 0.0 | noise layer OFF |
| Click Mix | 0.78 | prominent bright stick crack |
| Click Contact | 0.12 | ~2.4 ms tight contact |
| Click Brightness | 0.78 | ~4.9 kHz bandpass centre |
| Body Damping b1 | 0.50 | ≈25 s⁻¹ (~40 ms floor) — short dry decay |
| Body Damping b3 | 0.10 | 1e-4 s — light wood HF tilt, keeps it bright |
| Air Loading | 0.0 | Membrane-only; n/a on Plate |
| Mode Scatter | 0.20 | ~3% dither — organic grain |
| Coupling Strength | 0.0 | off (solid single body) |
| Secondary Enabled | 0.0 | off |
| Tension Mod | 0.0 | off (no glide; Membrane-only) |
| Pad Enabled | 1.0 | on |
| Pan | 0.5 | centre |

## Hi / Lo variants
- **Hi block:** Size 0.18 (~529 Hz), Click Brightness 0.80, Decay 0.16.
- **Lo block:** Size 0.28 (~420 Hz), Material 0.30, Decay 0.20, Click Brightness 0.74.
Optionally pan hi/lo slightly L/R (Pan 0.42 / 0.58) when assembling a kit.

## Defaulted (with reason)
Filter Type/Resonance (filter bypassed); all PitchEnv Start/End/Curve/Knee/Mid/Frac/Curve2 (pitch env off); Filter Env ADSR (env amount 0); Morph Start/End/Duration/Curve (morph off); FM Ratio / Feedback Amount / NoiseBurst Duration / Friction Pressure (wrong exciter — Impulse selected); Noise Cutoff/Reso/Decay/Color (noise mix 0); Secondary Size/Material (secondary off); Coupling Amount (no kit coupling); all 5 macros at 0.5 neutral (preserve the explicit baseline).

## Sources
- [Woodblock — Wikipedia](https://en.wikipedia.org/wiki/Woodblock_(instrument))
- [Woodblock guide — rareinstrument](https://rareinstrument.com/woodblock-drum/)
- [Euphonics 3.3 — marimbas & xylophones (free-bar ratios)](https://euphonics.org/3-3-marimbas-and-xylophones/)
- [Basic physics of xylophone and marimba bars — ResearchGate](https://www.researchgate.net/publication/243492369_Basic_physics_of_xylophone_and_marimba_bars)
- [Temple blocks — Wikipedia](https://en.wikipedia.org/wiki/Temple_blocks) / [Organology](https://organology.net/instrument/temple-blocks/)
