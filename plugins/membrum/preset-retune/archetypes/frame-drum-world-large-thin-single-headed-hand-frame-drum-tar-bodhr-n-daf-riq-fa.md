# Membrum Recipe — Frame Drum (World)

**Archetype:** Large thin single-headed hand frame drum — tar / bodhrán / daf / riq family. Broad low tone, short hand-damped sustain dominated by the air-loaded (0,1) mode.

**Body:** Membrane (Bessel circular-membrane bank, 48 modes) · **Exciter:** Mallet (soft beater/tipper)

---

## Physics → why these choices

| Property | Real instrument | Membrum lever |
|---|---|---|
| Fundamental | Diameter-set; 18" tar E2–A2 (~82–110 Hz), 20–22" → B1–E2 (~62–82 Hz), bodhrán low D–G ([Cooperman](https://www.coopermanframedrums.com/blogs/news/about-tuning-your-cooperman-drums)) | **Size 0.80 → 79 Hz (≈E2)** via f0 = 500·0.1^size |
| Mode spectrum | Ideal Bessel 1:1.59:2.14:2.30:2.65:2.92; **air loading** drops the low modes toward the near-harmonic 1:1.5:2:2.44:2.9 ([Rossing / WTT](https://wtt.pauken.org/chapter-3/air-loading-2)) | **Air Loading 0.82** (Membrane-only freq correction) |
| Dominant voice | Center-struck (0,1) carries the deep tone; edge brings up higher modes ([iDrumtune](https://www.idrumtune.com/drumhead-vibration-and-the-science-of-sound/)) | **Strike Position 0.32** (r/a≈0.29, near-center) |
| Decay | Short–medium: shallow/open back, hand-damped goatskin, no sealed kettle ([Bodhrán](https://en.wikipedia.org/wiki/Bodhr%C3%A1n)) | **Decay 0.42 (~0.3 s)** + **Decay Skew +0.24** (highs die first) + **b3 0.55** |
| Material | Thin skin on wood frame — woody, not metallic | **Material 0.32** (low brightness) |
| Attack | Soft "clicky" beater/tipper, not a stick crack | **Mallet exciter** + light **Click 0.38**, dull center ~950 Hz |
| Noise | Faint skin/beater/breath contact, dark | **Noise Mix 0.10**, Brown, ~700 Hz, 78 ms |
| Pitch glide | Subtle energy-driven tension "kerthump" ([JASA 150:202](https://pubs.aip.org/asa/jasa/article/150/1/202/606515/The-evolution-of-drum-modes-with-strike-intensity)) | **Tension Mod 0.12** (light, <1 st) |

All recipe values are voiced against the **post-audit corrected** signal path (measured-strike body norm, airLoading membrane-only freq table, decaySkew per-mode tilt, energy-driven tensionMod, linear voice + bus limiter, Click/Noise re-balanced ~−18 dBFS under the body).

---

## Baseline parameters (NORMALIZED [0,1])

| Param | Norm | Physical target | Why |
|---|---|---|---|
| Exciter Type | 0.20 | Mallet | soft beater weights low modes |
| Body Model | 0.00 | Membrane | struck circular skin |
| Material | 0.32 | brightness 0.32, base ~0.36 s | woody skin/frame |
| Size | 0.80 | **79 Hz ≈ E2** | large thin head, tar register |
| Decay | 0.42 | ~0.30 s body T60 | short hand-damped ring |
| Strike Position | 0.32 | r/a ≈ 0.29 | near-center, (0,1) dominant |
| Level | 0.85 | linear 0.85 | headroom for a low voice |
| **Air Loading** | **0.82** | low modes → preferred series | **the defining frame-drum lever** |
| Mode Stretch | 0.333 | 1.0 (unstretched) | membrane keeps physical ratios |
| Decay Skew | 0.62 | +0.24 (low-mode tilt) | broad low tone, fast highs |
| Mode Scatter | 0.12 | ~1.8% dither | natural-skin imperfection |
| Body Damping b3 | 0.55 | 5.5e-4 (f² damping) | highs die before fundamental |
| Tension Mod | 0.12 | small energy pitch bump | subtle membrane kerthump |
| Click Mix | 0.38 | light contact transient | soft tipper attack |
| Click Contact | 0.40 | 3.2 ms | rounded beater contact |
| Click Brightness | 0.40 | ~950 Hz | dull clop, not a tick |
| Noise Mix | 0.10 | faint, under body | skin/breath contact |
| Noise Cutoff | 0.40 | ~700 Hz LP | dark contact noise |
| Noise Decay | 0.25 | ~78 ms | attack-only |
| Noise Color | 0.20 | Brown | dark tilt |
| Coupling Amount | 0.25 | light global sympathetic | hits breathe together |

## Deliberately at default
ToneShaper filter/drive/fold (transparent — body shapes the tone) · PitchEnv (Time 0 = no 808 sweep) · Mode Inject 0 (don't overwrite the air-loaded spectrum) · Nonlinear Coupling 0 (thin head is linear) · Morph off · Choke 0 / Bus main / Pan center · all 5 Macros neutral 0.5 (recipe sets underlying params directly) · Body Damping b1 sentinel (Decay knob is enough) · **Secondary/shell coupling OFF** (a frame drum has a rim, not a resonant shell) · FM/Feedback/NoiseBurst/Friction params (no-ops for Mallet) · Pad Enabled on.

**Sources:** Cooperman frame-drum tuning ranges · Rossing air-loading & timpani membrane modes (Well-Tempered Timpani) · JASA 150:202 strike-intensity mode evolution · Bodhrán (Wikipedia) · iDrumtune drumhead vibration · bodhran-info compressor tuning.