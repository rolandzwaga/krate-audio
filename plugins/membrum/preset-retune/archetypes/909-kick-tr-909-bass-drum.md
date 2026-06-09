# Membrum Recipe — "909 Kick" (TR-909 Bass Drum)

**Body:** Membrane (Bessel modal bank, used as a low-mode analog-sine surrogate)
**Exciter:** Impulse (raised-cosine beater = the 909 attack section)

## Why this archetype is built this way

The TR-909 bass drum is an **analog voice, not a sample**: a bridged-T self-oscillator that produces a triangle, saturates it to a hex/quasi-square, then low-pass-filters it back to a near-**sine**, whose pitch is pulled **up** at note-on and relaxes down. A separate **attack section** sums a short **impulse + LP-filtered noise burst** through its own fast VCA. The panel "Tune" actually sets the pitch-envelope decay *time*; "Decay" sets amp decay; "Attack" sets the click level. (analog-synth.de 909 BD PCB; gearspace 909-synthesis thread; firstpr TR-909 mods PDF.)

The defining 909-vs-808 differences, and how each maps:

| 909 trait | Membrum mapping |
|---|---|
| Settle fundamental ~47-55 Hz, but the attack **rolls** down from a higher pitch | ToneShaper **PitchEnv 180 → 55 Hz** (norm 0.4771 → 0.2197) |
| **Faster** glide than the 808 (punch in the up-sweep) | **PitchEnv Time 20 ms** (norm 0.04) + **Punch macro 0.85** |
| **Click-forward / punchier** attack (impulse + noise) | **Click Mix 0.55**, **brightness ~1.9 kHz** (0.55), **contact 2.4 ms** (0.12) + **Impulse** exciter |
| **Harder** transient grit | **Drive 0.20** (internal 2.8×, timbre-only after M-2 makeup) |
| Near-pure analog sine, no metallic/inharmonic content | **Material 0.18** (dark/woody), **all physics axes zeroed** |
| Shorter body decay than the 808 | **Decay 0.22** (~0.5× base) + **b1 21 s⁻¹** tight floor; **b3 3e-4** damps upper harmonics |
| No sustained noise (noise is in the click) | **Noise Mix 0.05** (near-off) |

## Exact normalized baseline (post-audit semantics)

| Param | Norm | Denormalized target |
|---|---|---|
| Exciter Type | 0.0 | Impulse |
| Body Model | 0.0 | Membrane |
| Material | 0.18 | brightness 0.18 (dark), baseDecay 0.267 s |
| Size | 0.78 | f0 83 Hz natural (env overrides to 55 Hz) |
| Strike Position | 0.3 | r/a 0.27 |
| Level | 0.85 | linear 0.85 pre-rail |
| Drive Amount | 0.20 | internal 2.8×, unity small-signal |
| PitchEnv Start | 0.4771 | 180 Hz |
| PitchEnv End | 0.2197 | 55 Hz |
| PitchEnv Time | 0.04 | 20 ms |
| PitchEnv Curve | 0.15 | curveAmount −0.7 (fast exp drop) |
| Click Mix | 0.55 | 0.55 |
| Click Contact | 0.12 | 2.36 ms |
| Click Brightness | 0.55 | ~1901 Hz bandpass |
| Body Damping b1 | 0.42 | 21.1 s⁻¹ (overrides Decay flat term) |
| Body Damping b3 | 0.30 | 3.0e-4 s |
| Noise Mix | 0.05 | near-off |
| Tension Mod | 0.10 | subtle energy-glide (Membrane-only) |
| Air Loading | 0.0 | pure Bessel (no air loading) |
| Mode Stretch | 0.333333 | physical 1.0 (neutral) |
| Decay Skew | 0.5 | 0.0 (neutral) |
| Mode Scatter | 0.0 | pure ratios |
| Coupling Strength / Secondary Enabled | 0.0 / 0.0 | off |
| Pan | 0.5 | center |
| Macro Punch | 0.85 | deeper+faster glide, shorter contact |
| Macro Body Size | 0.45 | slightly tighter/higher |

## Deliberately left at default
ToneShaper filter (bypassed, cutoff 1.0) and its envelope; Fold; PitchEnv knee/mid (single-segment glide); ModeInject (would muddy the sine); NonlinearCoupling (plate/shell effect — grit comes from Drive); Material Morph; Choke/Output routing; FM/Feedback/NoiseBurst/Friction params (no-ops under Impulse); Coupling Amount; Tightness/Brightness/Complexity macros; noise shaping (no-op at Mix 0.05); secondary size/material (no-op); Pad Enabled (on).

## Sources
- analog-synth.de — TR-909 bass-drum PCB / circuit
- gearspace — "Synthesizing a TR-909 kick drum"
- firstpr.com.au — TR-909 BD/SD/HH sound mods (PDF)
- mixedbymarcmozart — 808/909/sub-kick frequency analysis (909 fundamental "rolls", 808 "solid")
- musicradar — processing TR-909 drum sounds (punchy kick, responds to analogue drive)
- Roland TR-909 service schematics
- Perfect Circuit — kick-drum synthesis primer