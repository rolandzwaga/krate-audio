# Membrum Recipe — "Bodhran" (World)

**Body:** Membrane (Bessel circular head, 48 modes) · **Exciter:** Impulse (hard wooden tipper)

The Irish goatskin frame drum (35–45 cm head, 9–20 cm wooden frame, open back, hand pressed on the inside of the head) struck with a lathe-turned double-ended wooden *tipper*. It must read as a **deep, round membrane bass note with a pronounced wooden beater contact** and an accent **pitch-bend** — voiced distinctly (more click, more head/shell coupling, a tension glide) from the soft frame-drum tap.

## Why these choices
- **Membrane + Impulse**: a stretched goatskin head is a circular membrane; only the Membrane body runs the Bessel mapper and consumes `airLoading`/`tensionMod`. A hard wooden tipper is a sharp impulse, not a soft mallet.
- **Size 0.72** → f0 = 500·0.1^0.72 ≈ **95 Hz** ((0,1) anchor); the perceptual (1,1) fundamental ≈ **152 Hz** — the deep bass note of a ~16″ frame drum.
- **Air Loading 0.78** pulls the (m,1) modes 78% toward Rossing's timpani-like series **1 : 1.5 : 2 : 2.44 : 2.9**, deepening the head and giving it a clearer sense of pitch (open frame + hand load) — the hallmark "bass note."
- **Head↔shell coupling (Strength 0.32, Secondary ON, Size 0.42 → ~65 Hz, Material 0.30)**: the goatskin head drives the wooden frame, adding a felt low **wooden undertone** beneath the head fundamental.
- **Click forward (Mix 0.65, Contact ~2.5 ms, Bright ~2.5 kHz)**: the defining wooden-tipper tick — *more click than the soft tap*.
- **Tension Mod 0.30** (Membrane-only): the hand-on-head + high goatskin tension give the audible accent **"kerthump" pitch bend**, energy-driven and relaxing as the body decays.
- **Damping b1 ≈ 15 s⁻¹ / b3 = 1e-4**: a controlled medium RT60 with woody (not metallic) high-mode roll-off — matching the taped/hand-damped bodhran.
- **Noise low & dark (Mix 0.18, Pink, ~540 Hz LP)**: modest skin/air rustle, not snare hiss.

## Key normalized values
| Param | Norm | Physical |
|---|---|---|
| Exciter / Body | 0 / 0 | Impulse / Membrane |
| Material | 0.45 | woody-warm, base decay ~0.31 s |
| Size | 0.72 | f0≈95 Hz, (1,1)≈152 Hz |
| Strike Pos | 0.40 | r/a≈0.36 |
| Air Loading | 0.78 | 78% → Rossing timpani series |
| Decay / b1 / b3 | 0.40 / 0.30 / 0.10 | b1≈15 s⁻¹, b3=1e-4 (medium, woody) |
| Decay Skew | 0.45 | −0.10 (slight high tilt) |
| Click Mix / Contact / Bright | 0.65 / 0.18 / 0.62 | hot, ~2.5 ms, ~2.5 kHz |
| Noise Mix / Color / Cutoff | 0.18 / 0.40 / 0.45 | low, Pink, ~540 Hz LP |
| Coupling Str / Secondary / Size / Mat | 0.32 / 1.0 / 0.42 / 0.30 | shell f0≈65 Hz wooden undertone |
| Tension Mod | 0.30 | accent pitch-bend |
| Mode Scatter | 0.20 | organic detune |
| Level / Pan | 0.80 / 0.50 | front, centered |

Filter, Drive/Fold, PitchEnv, Morph, all 5 macros, FM/Feedback/NoiseBurst/Friction, choke/bus → **left at default** (bypassed or wrong-exciter no-ops; pitch motion delegated to Tension Mod).

## Implementation note
This maps onto the existing `cajonFramesKit()` Bodhran pad (`tools/membrum_preset_generator.cpp` pad index 10). The values above match/refine it and are voiced against the **post-audit corrected** chain (linear gain-staging, measured-strike body norm, air-loading on Membrane, energy-driven tension glide, click-into-exciter feed).

## Sources
- [Bodhrán — Wikipedia](https://en.wikipedia.org/wiki/Bodhr%C3%A1n) (dimensions, goatskin, tipper woods, hand pitch control)
- [Roosebeck — What is a Bodhran](https://roosebeck.com/blogs/learn/what-is-a-bodhran) · [InstrumentHeritage](https://instrumentheritage.com/bodhran-irish-frame-drum/) (deep bass, tape damping, tone range)
- [Tronel/Errede NAH frame-drum study, UIUC PHYS406](https://courses.physics.illinois.edu/phys406/sp2017/NSF_REU_Reports/2010_reu/Gregoire_Tronel/Gregoire_Tronel_Final_Report.pdf) ((0,n) mode peaks)
- [Rossing air-loading / Well-Tempered Timpani](https://wtt.pauken.org/chapter-3/air-loading-2) (timpani 1:1.5:2:2.44:2.9 series)
- [Ceolas bodhran tuning](https://www.ceolas.org/instruments/bodhran/santin/Tuning.htm) · [thesession.org pitch bend](https://thesession.org/discussions/16047)