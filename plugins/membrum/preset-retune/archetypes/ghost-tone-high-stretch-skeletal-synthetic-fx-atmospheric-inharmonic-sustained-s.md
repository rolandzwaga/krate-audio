# Membrum Recipe — "Ghost Tone (high-stretch skeletal)" (synthetic-fx)

Hollow, bone-like, strongly inharmonic SUSTAINED tones for atmospheric sound design. Body alternates **Bell / String**; exciter cycles **Mallet / FMImpulse / Impulse** with periodic **Friction**. The signature is **very high modeStretch (0.65–0.89)** — the most aggressive use of the widened B_max inharmonicity axis — plus **high decaySkew (0.78+)**, an **HP filter**, **long decays**, **per-note Material morph**, and **output-bus spread**. Implementation target: `ghostBonesKit()` in `tools/membrum_preset_generator.cpp`.

## Physics → why these choices
- **No single pitch (by design).** Bell f0 = 800·0.1^size; size 0.20–0.75 → f0 ≈ 505→142 Hz (eerie low-mid register).
- **Bell partials are inharmonic** — hum (0.5), prime (1.0), **tierce 1.2 = minor third** (the plaintive, melancholy bell signature), quint (1.5), nominal (2.0). Membrum's `kBellRatios` (0.25,0.5,0.6,0.75,1.0,…) encode this; **high modeStretch then warps them off-ratio** via the corrected `sqrt(1+B·(k+1)²)` law (B = 0.0065–0.0089 at norm 0.65–0.89, B_max=0.01) — a stretched, non-pitched, bone-like cluster. Compare the stiff-string law `f_n = n·f0·sqrt(1+B·n²)` (real piano-bass B ~2e-3; Membrum exaggerates to ~0.01).
- **Long T60, low partials linger.** Decay norm 0.8 (×1.93 multiplier), b1 ≈17 s⁻¹, **b3=0** (metallic — high partials ring), **decaySkew +0.6** tilts the tail toward the low hum/prime, reproducing the real-bell "hum lingers longest" settle.
- **Soft / no transient.** Mallet beater + faint click (or bowed Friction with none) → hollow, not percussive.
- **Atmosphere.** Quiet dark Brown-noise bed; slow Material morph (≈1.4 s, 0.45→0.80) evolves the spectrum; amplitude-driven NonlinearCoupling brightens the tail; global sympathetic coupling + aux-bus spread enlarge the image.

## Body / Exciter
- **Body:** Bell (norm 0.8, even pads) / String (norm 0.6, odd pads).
- **Exciter:** Mallet 0.2 / FMImpulse 0.8 / Impulse 0.0, with Friction 0.6 on every 5th pad. FM c:m inharmonic (fmRatio ≈2.2); bow pressure moderate (frictionPressure ≈0.3).

## Baseline normalized values (meaningful params)
| Param | Norm | Physical target |
|---|---|---|
| Exciter Type | 0.2 (cycled) | Mallet / FMImpulse 0.8 / Impulse 0.0 / Friction 0.6 |
| Body Model | 0.8 / 0.6 | Bell / String |
| Material | 0.45 | Bell brightness 0.84, base decay ≈0.85 s |
| Size | 0.45 | f0 ≈284 Hz (kit 142–505 Hz) |
| Decay | 0.8 | ×1.93 on base → multi-second ring |
| Strike Position | 0.3 | strike azimuth ≈27° |
| Level | 0.65 | linear, headroom for bus limiter |
| Filter Type | 0.5 | **Highpass** |
| Filter Cutoff | 0.22 | ≈76 Hz HP corner (kit 55–270 Hz) |
| Filter Resonance | 0.3 | Q ≈3.5 |
| Filter Env Amount | 0.45 | −0.10 (slight down-sweep) |
| Filter Env Dec | 0.3 | 54 ms |
| **Mode Stretch** | **0.77** | **phys 1.66, B=0.0077 — strong inharmonicity (kit 0.65–0.89)** |
| **Decay Skew** | **0.81** | **+0.62, low-partial tilt (kit 0.78–0.90)** |
| Mode Inject | 0.0 | bypass (no harmonic series) |
| Nonlinear Coupling | 0.32 | env-level brightening |
| Morph Enabled | 1.0 | per-note Material sweep on |
| Morph Start/End | 0.45 / 0.80 | Material 0.45→0.80 |
| Morph Duration | 0.7 | ≈1403 ms |
| Morph Curve | 0.4 | linear |
| FM Ratio | 0.4 | c:m 2.2 (FMImpulse pads) |
| Friction Pressure | 0.3 | bow bias +0.15 (Friction pads) |
| Mode Scatter | 0.45 | ~7% dither (shimmer) |
| Noise Mix | 0.12 | faint dark bed |
| Noise Cutoff | 0.35 | ≈320 Hz LP |
| Noise Color | 0.2 | Brown |
| Noise Decay | 0.85 | ≈1100 ms |
| Click Mix | 0.12 | whisper of contact |
| Click Brightness | 0.6 | ≈1900 Hz |
| Body Damping b1 | 0.35 | ≈17.6 s⁻¹ (long tail) |
| Body Damping b3 | 0.0 | metallic, long highs |
| Coupling Amount | 0.85 | high sympathetic ring (global coupling on) |
| Output Bus | 0 / 1 | aux spread on ~¼ of pads |
| Pan | 0.5 (spread) | center baseline; spread L–R per pad |
| Tension Mod | 0.0 | off (Membrane-only anyway) |
| Choke Group | 0 | none (tails ring free) |

## Left at default (with reason)
- **Drive / Fold (0):** would add warmth/aggressive harmonics — fights the sparse hollow skeleton.
- **PitchEnv family (Time=0):** a ghost tone holds pitch; no 808 glide.
- **Feedback Amount / NoiseBurst Duration:** no-ops unless those exciters are selected.
- **Tension Mod:** Membrane-only; no-op on Bell/String.
- **Punch/Tightness macros (0.5):** no transient punch on a sustained drone (Brightness/Complexity nudged up per-pad).
- **Secondary coupling (off):** the inharmonic body stands alone; a minority of pads add a light shell.

## Sources
- Bell partials & inharmonic tierce: [Strike tone (Wikipedia)](https://en.wikipedia.org/wiki/Strike_tone), [Hibberts — strike note](http://www.hibberts.co.uk/strike.htm), [Chladni's law in church bells](https://oro.open.ac.uk/40358/7/40358.pdf)
- Inharmonicity / stiff-string B: [Inharmonicity (Wikipedia)](https://en.wikipedia.org/wiki/Inharmonicity), [Wave equation for stiff strings (arXiv)](https://arxiv.org/pdf/1603.05516)
- FM bell (inharmonic c:m, high index): [FM synthesis (Wikipedia)](https://en.wikipedia.org/wiki/Frequency_modulation_synthesis)
- Eerie bowed-metal/glassy texture: [Soundiron Cymbology](https://soundiron.com/products/cymbology), [Sonixinema Bowed Metals](https://www.sonixinema.com/products/bowed-metals)