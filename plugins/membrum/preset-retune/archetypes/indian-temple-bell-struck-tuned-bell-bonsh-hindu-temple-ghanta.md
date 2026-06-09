# Membrum Recipe — Indian Temple Bell (bell)

**Archetype:** Indian Temple Bell (struck tuned bell — bonshō / Hindu temple ghanta)
**Body:** Bell (offset 1, norm 0.8) — church/temple-bell Chladni partial bank, Simpson-tuned hum/prime/tierce/quint/nominal = 0.25/0.50/0.60/0.75/1.00.
**Exciter:** Mallet (offset 0, norm 0.2) — soft beam/mallet contact (ImpactExciter).

> All values below are NORMALIZED [0,1] (preset/on-wire). The physical target each denormalizes to is given alongside.

## Acoustic profile (researched)
- **Pitch / fundamental.** Bells are inharmonic; the perceived strike note sits near the NOMINAL with a prominent HUM a (nominal) double-octave below. Temple/Buddhist bells are warm and low; Indian temple-bell summaries cite a bright strike component ~500–700 Hz with overtones ~1400/2100/2600 Hz, but the carrying hum is far lower (~60–130 Hz for a mid bell). **Size 0.50 → f0_nominal = 800·0.1^0.5 = 253 Hz** → hum 63 Hz, prime 127, tierce 152 (the bell minor third), quint 190 Hz. Deliberately fuller/lower than crotales/tingsha (Size 0.16–0.25).
- **Partials / inharmonicity.** Partials are FRACTIONAL multiples of the nominal, not an integer series. Membrum's Bell ratios already encode the canonical Simpson tuning (Hibberts/Perrin/Rossing). A mild **modeStretch (norm 0.40 → physical 1.10)** spreads the upper partials for the real, not-perfectly-tuned bell spectrum.
- **Decay / T60.** The signature is a very long ring: bonshō hum (*oshi*) up to ~10 s, final decay (*okuri*) up to a minute; Indian temple bells ~6–10 s ('~7 s' symbolically). Low partials ring longest. **Decay 0.92 + low b1 (9.2 s⁻¹) + very low b3 (6e-5)** → multi-second hum bounded by the bank's 5 s clamp.
- **Transient.** Soft 'atari' beam-on-bronze contact — clean but not a sharp metallic tick. **Low Click Mix 0.28, dull-mid Click Brightness ~1 kHz, 3.4 ms contact.**
- **Noise.** Nearly pure-tonal — almost no broadband content. **Noise Mix ~0.04 (near-muted).**
- **Pitch glide.** None (no membrane tension); the only motion is slow beating between near-degenerate partials → **modeScatter 0.06**. tensionMod is Membrane-only and inert on Bell.
- **Material.** High-tin bell-metal bronze (~78/22 Cu/Sn, or panchaloha) — very metallic, low loss. **Material 0.82, b3 6e-5** → long-lived upper partials.

## Parameter baseline
| Param (offset) | Norm | Physical target | Why |
|---|---|---|---|
| Exciter Type (0) | 0.20 | Mallet | Soft beam/mallet strike |
| Body Model (1) | 0.80 | Bell | Tuned-bell Chladni bank |
| Material (2) | 0.82 | brightness 0.95, base decay 1.17 s | Bell-metal bronze, low loss |
| Size (3) | 0.50 | f0_nom 253 Hz, hum 63 Hz | Mid temple bell, warm hum |
| Decay (4) | 0.92 | 2.5× → ~3 s ring | Long bell sustain |
| Strike Position (5) | 0.18 | θ 0.28 rad, near antinode | Fixed soundbow, full low partials |
| Level (6) | 0.78 | linear 0.78 | Headroom for the long tonal tail |
| Mode Stretch (21) | 0.40 | physical 1.10 | Authentic upper-partial spread |
| Decay Skew (22) | 0.78 | +0.56 | Long warm hum, fast-fading brightness |
| Body Damping b1 (50) | 0.18 | 9.2 s⁻¹ | Long RT60 floor (low-loss bronze) |
| Body Damping b3 (51) | 0.06 | 6e-5 s | Metal: upper partials live long |
| Mode Scatter (53) | 0.06 | ~6% dither | Cast-bell beating/doublets |
| Click Mix (47) | 0.28 | soft atari | Understated contact transient |
| Click Contact (48) | 0.45 | 3.4 ms | Padded beam contact |
| Click Brightness (49) | 0.42 | 1015 Hz | Dull-mid thud, not a tick |
| Noise Mix (42) | 0.04 | near-muted | Pure-tonal bell |
| Pan (64) | 0.50 | center | Single bell, centered |

## Deliberate defaults
PitchEnv (no glide), Mode Inject (would add a contradictory integer series), Nonlinear Coupling (linear regime), Drive/Fold (clean tone), Material Morph (static timbre), ToneShaper filter (transparent — body spectrum is already correct), Secondary shell (the bell *is* the resonator), Tension Mod (Membrane-only), all macros neutral. **fmRatio/feedback/noiseBurst/friction are exciter-specific no-ops with Mallet** — the task's 'fmRatio 0.30' is inert for a Mallet-excited bell and is therefore left at the harmless struct default rather than cited as meaningful.

## Sources
Bonshō (atari/oshi/okuri, ~10 s hum, beam strike, high-tin bronze) — en.wikipedia.org/wiki/Bonshō · Hibberts 'The Sound of Bells' (Simpson tuning, partial identification, strike note) · Perrin/Charnley/DePont 1983 (normal modes) · Open University church-bell partial/Chladni papers · Maharashtra/Indian temple-bell acoustical analysis · temple-bell metallurgy (panchaloha / 78-22 bronze).