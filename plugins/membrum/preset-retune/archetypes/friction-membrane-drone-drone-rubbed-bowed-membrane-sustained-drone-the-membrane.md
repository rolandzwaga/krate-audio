# Membrum Recipe — Friction Membrane Drone (drone)

A rubbed/bowed-membrane **sustained drone**: the membrane analog of the string drone it alternates with in the drone kit. Where the String drone is harmonic and bright, this is **inharmonic, low, dark and round** — the growling, vocal "large-mammal" voice of a friction drum (cuíca / rommelpot / lion's roar).

## Body + Exciter
- **Body: Membrane** (offset 1 = `0.0`). 48-mode Bessel drumhead. Intrinsically inharmonic; air-loadable toward the near-octave timpani series; the only body where `tensionModAmt` (the cuíca pitch glide) is live.
- **Exciter: Friction** (offset 0 = `0.6` → idx 3). BowExciter stick-slip junction drives the body with a **sawtooth-like Helmholtz force** (all integer harmonics, amplitude ∝ 1/n, −6 dB/oct).

## Physics → params (why it sounds like a friction drum)
- **Low growling fundamental** → `Size 0.5` → f0 ≈ **158 Hz**. Friction drums emphasize low-frequency "growling resonance."
- **Inharmonic, round membrane** → Membrane Bessel modes + **Air Loading 0.5** (depresses the lowest modes toward the Rossing near-octave series → deeper, less whistly) + **b3 0.23** (HF roll-off) + **decaySkew 0.85** (energy tilted to the low modes).
- **Sustained drone, not a hit** → **Decay 0.92** (longest ring) + **b1 0.31** (moderate damping floor so it rings cleanly) + **sustained filter ADSR** (Sus 0.55, long Atk/Dec/Rel) + Friction's repeated bow re-excitation.
- **Sawtooth bowed spectrum** → **Mode Inject 0.18** reinforces the 1/k (−6 dB/oct) integer series the stick-slip drive physically produces.
- **Nonlinear, amplitude-brightening friction** → **Nonlinear Coupling 0.45** (louder = brighter, sustained) + **Friction Pressure 0.45** (firmer rub, richer harmonics).
- **Resonant sound-box** → **Secondary Enabled** + **Coupling 0.18**, shell f0 ≈ 0.63·head (the friction drum's coupled box that amplifies the low growl).
- **Cuíca pitch bend** → **Tension Mod 0.4** (energy-dependent upward glide, Membrane-only; cuíca spans ~2 octaves by tension).
- **Soft scrape onset, dark noise** → **Click Mix 0** (no beater click) + **Noise Mix 0.2 / Pink / ~850 Hz LP / long decay** (rosin scrape, dark).
- **Wobbly, human** → **Mode Scatter 0.18** organic detune.

## Darker/rounder than the String sibling
Same kit slot voicing (Decay 0.92, Level 0.62, LP cutoff 0.45) but: Membrane body (inharmonic vs harmonic waveguide), lower air-loaded modes, stronger low-mode skew, HF b3 damping, and the membrane-only tension glide — a darker, rounder, more vocal drone alternating against the brighter string.

## Coverage notes
Drive/Fold/Morph/PitchEnv off by design (the tension glide *is* the pitch motion; nonlinear coupling *is* the brightening). FM/Feedback/NoiseBurst secondary params are no-ops under the Friction exciter. Choke 0, Pad Enabled. Pan center at baseline (the kit spreads the 8 drone voices L/R at build time).

## Sources
Friction drum & cuíca stick-slip → sawtooth drive, low growl, vocal timbre, ~2-octave tension pitch (Wikipedia/Grokipedia Friction drum; Grokipedia/BYU Cuíca; Bart Hopkin "Adventures in Friction"). Membrane Bessel inharmonicity & air loading (Wikipedia Vibrations of a circular membrane; wtt.pauken.org Air Loading / Rossing). Sawtooth = all integer harmonics ∝1/n, −6 dB/oct (handwiki). Helmholtz bowed-string stick-slip & harmonic content vs bow force (JASA 2022; McGill bowed-string thesis).