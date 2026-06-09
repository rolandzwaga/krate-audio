# Membrum Recipe — FM-Bell Percussion (synthetic) [bell]

**Body:** Bell  **Exciter:** FMImpulse

A configurable metallic-bell perc voice — the 808 kit's "perc" filler slots (only one audible). An inharmonic FM-bell ping with no single real-world referent; voiced against the post-audit corrected signal path.

## Acoustic / synthesis profile (researched)

- **No single referent.** Closest *circuit* referent = the **TR-808 cowbell**: two square oscillators at **800 Hz & 540 Hz**, ratio **1.48** (a *detuned perfect fifth*) — deliberately inharmonic so the partials buzz/beat = "metallic"; two-stage decay (fast snap → short clangorous tail, ~100–400 ms), bandpassed. ([outputchannel](http://outputchannel.com/post/tr-808-cowbell-web-audio/), [baratatronix](https://www.baratatronix.com/blog/606-cymbal-and-hi-hat-synthesis-nnfnz))
- **Synthesis referent = Chowning FM bell:** inharmonic carrier:modulator ratio near **1:1.4** (≈1:√2), modulation **index decays bright→pure**, long **exponential amplitude decay** — the three together make "metallic bell." ([CCRMA CLM](https://ccrma.stanford.edu/software/clm/compmus/clm-tutorials/fm2.html), [NI](https://blog.native-instruments.com/what-is-fm-synthesis/))
- **Bell body partials** (canonical, already correct in the audit): hum 0.25, prime 0.50, **tierce 0.60** (the plaintive metallic minor-third), quint 0.75, nominal 1.00, then 1.5/2.0/2.6/3.2/4.0…12.0. ([Hibberts](https://www.hibberts.co.uk/the-musical-sound-quality-of-church-bells/), [Strike tone](https://en.wikipedia.org/wiki/Strike_tone))

## Two stacked inharmonic spectra

1. **Bell BODY** — nominal f0 = 800·0.1^size = **450 Hz** at size 0.25 (hum 112 Hz, top ~5.4 kHz). Already inharmonic; **leave Stretch neutral**.
2. **FMImpulse EXCITER** — carrier 400→2500 Hz (velocity), modulator ratio **2.2** at fmRatio 0.4 (between Chowning 1.4 and cowbell 1.48), index 0.5→3.0 rad over ~30 ms, amp over ~80 ms.

## Baseline (NORMALIZED → physical)

| Param | Norm | Physical |
|---|---|---|
| Exciter Type | 0.80 | FMImpulse (idx 4) |
| Body Model | 0.80 | Bell (idx 4) |
| Material | 0.70 | bell brightness 0.91; base decay 1.10 s |
| Size | 0.25 | nominal f0 450 Hz |
| Decay | 0.20 | body T60 ≈ 0.37 s (short ping) |
| Strike Position | 0.30 | azimuth 0.47 rad |
| Level | 0.75 | ×0.75 linear |
| **FM Ratio** | **0.40** | **mod ratio 2.2 (inharmonic)** |
| Body Damping b3 | 0.00 | b3=0 → pure flat (metal) |
| Click Mix | 0.15 | clapper tick |
| Click Brightness | 0.70 | bandpass ~3.4 kHz |
| Noise Mix | 0.00 | off (clean tonal ping) |
| Air Loading | 0.00 | no-op (membrane-only) |
| Mode Stretch | 0.333 | 1.0 = canonical bell |
| Decay Skew | 0.50 | 0 = none |
| Pan | 0.50 | center |
| PitchEnv Time | 0.00 | disabled (no glide) |

## Deliberately defaulted
ToneShaper filter & filter-env (cutoff 1.0 = bypass), Drive/Fold (0), all PitchEnv (Time=0), Mode Inject (0), Nonlinear Coupling (0), Material Morph (off), Body Damping b1 (sentinel→Decay), Mode Scatter (0), all coupling/secondary (off), Tension Mod (0, Membrane-only), all 5 macros (0.5 neutral), Feedback/NoiseBurst/Friction params (wrong-exciter no-ops), Noise sub-params (Noise Mix=0), Click Contact (default), Choke/Bus (0), Pad Enabled (1).

> Kit-level: the *other* perc slots are silenced via the crafted list, not per-voice voicing — this recipe is the one audible pad.
