# Membrum Recipe — Bell Tree (bell archetype)

**Body:** Bell (church-bell Chladni partials) · **Exciter:** NoiseBurst (violet-noise bandpass burst)

A bell tree is a vertical stack of 14–28 nested inverted metal bowls / small tuned bells, struck with a mallet in a downward **glissando** to produce a bright shimmering metallic cascade of overlapping, microtonally-detuned rings ([Wikipedia: Bell tree](https://en.wikipedia.org/wiki/Bell_tree)). The constituent objects are crotale-/small-bell-like bronze/brass bodies — "rather like a small tuned bell, only with a much brighter sound and a much longer resonance" ([Wikipedia: Crotales](https://en.wikipedia.org/wiki/Crotales)).

## Acoustic profile → param drivers

| Trait | Physics | Membrum lever |
|---|---|---|
| Tuned-bell partials | hum/prime/tierce/quint/nominal = 0.250:0.500:0.600:0.750:1.000 (minor-3rd tierce is the signature) ([Hibberts](https://www.hibberts.co.uk/identifying-bell-partials/); [Perrin/Charnley/DePont 1983](https://www.hibberts.co.uk/wp-content/uploads/2022/01/perrin_charnley_depont_1982.pdf)) | **Body = Bell** (`kBellRatios`) |
| High register | small bowls/crotales, principal partials ~1–4 kHz | **Size 0.30** → f0_nominal ≈ 401 Hz, upper partials to ~4 kHz |
| Hard metal, very bright | bronze/brass, high partials prominent | **Material 0.95** → brightness 0.985 |
| **Inharmonic / microtonal** ("no formal scale") | bowls detuned bowl-to-bowl; reads as shimmer not melody | **Mode Stretch 0.55** (partials off canonical ratios) + **Mode Scatter 0.55** (~8% dither) |
| Long, metallic ring, bright high partials sustain | crotales = "much longer resonance"; low f²-damping | **Decay 0.85** (~2.4 s) + **b3 = 0** (no high-freq roll-off) + **b1 0.30** (~15 s⁻¹ floor) |
| Bright top of cascade | energy weighted to upper partials | **Decay Skew 0.78** (+0.56; per-mode upper-partial tilt) |
| Dense overlapping mallet contacts | many bowls hit in quick succession | **NoiseBurst exciter** (spreads strike over all modes) + **Click 0.55 / brightness 0.92** sharp metallic ticks |
| Cascade dims as beater passes | timbral, not pitch, evolution | **Material Morph ON 0.85→0.55** over ~1.1 s, linear |
| Airy sizzle | many simultaneous metal contacts | **Noise overlay 0.42**, violet, ~13 kHz LP, ~1.1 s decay |
| **No pitch glide** | rigid metal — no membrane tension | Pitch env disabled; Tension Mod 0 (Membrane-only anyway) |

## Key normalized baseline (all values 0–1, on-wire)

```
Exciter Type     0.40  -> NoiseBurst
Body Model       0.80  -> Bell
Material         0.95  -> brightness 0.985, base decay 1.35 s
Size             0.30  -> f0_nominal 401 Hz
Decay            0.85  -> ~2.4 s ring
Strike Position  0.30
Level            0.70
Mode Stretch     0.55  -> phys 1.325 (inharmonic)
Decay Skew       0.78  -> +0.56 (upper-partial tilt)
Mode Scatter     0.55  -> ~8% detune
Morph Enabled    1.0 ; Start 0.85 ; End 0.55 ; Dur 0.55 (1.1 s) ; Curve 0.3 (lin)
Noise  Mix 0.42 ; Cutoff 0.92 (13 kHz) ; Color 0.85 (violet) ; Decay 0.85 (1.1 s)
Click  Mix 0.55 ; Brightness 0.92 (9.4 kHz)
Body Damping b1  0.30 (15 s^-1) ; b3 0.0 (pure metallic, no high roll-off)
Air Loading      0.0 (Bell no-op)
```

## Deliberately defaulted
Pitch envelope (rigid metal, no glide), Mode Inject & Nonlinear Coupling (bell already inharmonic/near-linear — exact bypass at 0), Drive/Fold (bypassed, would muddy the clean shimmer), Secondary shell + Tension Mod (no separate shell body / Membrane-only), all five macros neutral 0.5, Pan centered, Choke 0 (rings must overlap freely). **Note:** `fmRatio 0.55` from the brief is a no-op here because NoiseBurst (not FMImpulse) is the exciter.

Sources: [Bell tree](https://en.wikipedia.org/wiki/Bell_tree) · [Crotales](https://en.wikipedia.org/wiki/Crotales) · [Hibberts bell partials](https://www.hibberts.co.uk/identifying-bell-partials/) · [Perrin et al. 1983](https://www.hibberts.co.uk/wp-content/uploads/2022/01/perrin_charnley_depont_1982.pdf) · [Keltek strike note](https://www.keltektrust.org.uk/sob04.html) · [SOS Synthesizing Bells](https://www.soundonsound.com/techniques/synthesizing-bells) · [Inharmonicity](https://en.wikipedia.org/wiki/Inharmonicity)