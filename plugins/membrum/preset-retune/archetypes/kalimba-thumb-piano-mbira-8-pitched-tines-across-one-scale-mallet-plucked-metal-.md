# Membrum Recipe — "Kalimba" (Thumb-Piano / Mbira), Mallet archetype

**Body:** Bell · **Exciter:** Mallet · **8 pitched pads = one instrument across a scale (C4→E5)**

All values below are NORMALIZED [0,1] (preset/on-wire representation). Physical target follows each.

## Why Bell + Mallet (acoustics)

A kalimba/mbira *tine* is a thin metal lamella that vibrates as a **clamped-free (cantilever) Euler-Bernoulli beam with an intermediate simply-supported bridge** (King, *JASA* 131(2):1289, 2012 — [PMID 22280717](https://pubmed.ncbi.nlm.nih.gov/22280717/)). Its overtones are **inharmonic and non-integer** — `f2/f1`, `f3/f1` are not whole numbers and shift with bridge position. An ideal cantilever rings at **1 : 6.27 : 17.55 : 34.39** ([Penn State / D. Russell, flexural bar](https://www.acs.psu.edu/drussell/Demos/Flexural-Bar/flexural.html)); a free-free bar (glockenspiel = Membrum's *Shell* body) rings at **1 : 2.756 : 5.404 : 8.933** (Fletcher & Rossing Ch.2/Ch.21). The bridge pulls the real tine's upper ratios between these.

The mbira's inharmonic partials are **"strongest in the attack and die out rather quickly, leaving an almost pure tone"** ([Wikipedia, Mbira](https://en.wikipedia.org/wiki/Mbira)). So the right model is: a **fundamental-dominated, strongly inharmonic** spectrum with **bright transient upper partials** and a **clean pure ring**. Membrum's **Bell** body (sub-nominal hum at 0.25·f0 + non-integer 0.5/0.6/0.75/1.0/1.5/2.0 partials) reproduces that inharmonic-attack-then-pure character (and the box weight) better than the regular *Shell* series. The **Mallet** exciter is the soft thumb-pluck contact; the bright metallic onset "ping" comes from the always-on **Click** layer.

Higher tines **sustain less** than lower tines ([vibratekalimba](https://vibratekalimba.com/the-ultimate-guide-of-kalimba/)); a clean kalimba is near-noiseless, with an optional **mid-band buzz/wah** from sympathetic tines and box rattlers. There is **no pitch glide** (stiff metal).

## Per-pad pitch grade (Bell nominal f0 = 800·0.1^Size)

| Pad | Note | f0 (Hz) | Size | Material | Decay | Pan |
|----|------|---------|------|----------|-------|-----|
| 1 | C4 | 261.6 | 0.485 | 0.40 | 0.60 | 0.50 |
| 2 | D4 | 293.7 | 0.435 | 0.46 | 0.56 | 0.38 |
| 3 | E4 | 329.6 | 0.385 | 0.52 | 0.52 | 0.62 |
| 4 | G4 | 392.0 | 0.310 | 0.58 | 0.47 | 0.30 |
| 5 | A4 | 440.0 | 0.260 | 0.63 | 0.43 | 0.70 |
| 6 | C5 | 523.3 | 0.185 | 0.69 | 0.38 | 0.25 |
| 7 | D5 | 587.3 | 0.135 | 0.74 | 0.34 | 0.75 |
| 8 | E5 | 659.3 | 0.085 | 0.80 | 0.30 | 0.50 |

(Material rises and Decay falls with pitch; Pan alternates centre-out like real tine layout. All other params are the shared baseline below.)

## Shared baseline params (every pad)

| Param | Norm | Physical target | Why |
|-------|------|-----------------|-----|
| Exciter Type | 0.20 | Mallet | Soft thumb-pluck contact |
| Body Model | 0.80 | Bell | Inharmonic, fundamental-dominated tine spectrum |
| Strike Position | 0.15 | azimuth ≈0.24 rad, near antinode | Pluck near free tip → strong fundamental + present partials |
| Level | 0.72 | ×0.72 linear | Headroom for overlapping melodic notes |
| Mode Stretch | 0.40 | phys 1.1 (mild dispersion) | Extra stiff-beam inharmonicity above neutral |
| Decay Skew | 0.62 | +0.24 per-mode tilt | Upper partials strong in attack, then die → bias sustain low |
| Mode Scatter | 0.08 | ~8% dither | Hand-tuned tines never perfectly on-ratio |
| Body Damping b3 | 0.10 | 1e-4 s | Metal: weak f²-damping, long highs |
| Body Damping b1 | 0.18 | ~9.2 s⁻¹ | Moderate metallic RT60 floor |
| Click Mix | 0.42 | ~−18 dBFS, ½ to body | Bright onset "ping" / pluck definition |
| Click Contact | 0.25 | ~2.75 ms | Crisp tine contact |
| Click Brightness | 0.78 | ~4200 Hz | Metallic "ting", not a thud |
| Noise Mix | 0.10 | light | Sympathetic-tine / box buzz (kept low) |
| Noise Cutoff | 0.60 | ~1500 Hz LP | Mid-band buzz, not hiss |
| Noise Resonance | 0.30 | Q≈1.7 | Tonal "wah" buzz |
| Noise Decay | 0.20 | ~45 ms | Buzz dies fast (attack-concentrated) |
| Noise Color | 0.65 | White | Neutral-bright buzz |
| Filter Cutoff | 1.00 | 20 kHz = bypass | Body carries the timbre |
| Filter Env Amount | 0.50 | 0 = no mod | No filter sweep |
| Drive / Fold | 0.00 / 0.00 | bypass | Clean tine tone |
| PitchEnv Time | 0.00 | disabled | No glide on a stiff tine |
| Mode Inject | 0.00 | bypass | Don't fight the body's inharmonic partials |
| Nonlinear Coupling | 0.00 | bypass | Tine ~linear at playing level |
| Tension Mod | 0.00 | off | Membrane-only; absent in metal |
| Air Loading | 0.00 | n/a on Bell | Avoid implying membrane physics |
| Choke Group | 0.00 | no choke | Tines ring/overlap freely |
| Pan | 0.50 (graded 0.25–0.75) | equal-power | Tine spread across the box |

## Defaulted (neutral) — coverage policy

- **Filter Env A/D/S/R, PitchEnv Start/End/Curve/Knee/Mid/MidFrac/Curve2** — inert because the filter is bypassed and PitchEnv Time=0.
- **Morph (Enabled/Start/End/Duration/Curve)** — a plucked tine's timbre is set at onset; no intra-note sweep.
- **FM Ratio / Feedback / NoiseBurst Duration / Friction Pressure** — no-ops for the Mallet exciter.
- **Coupling Amount (0.5), Coupling Strength / Secondary (off)** — tine+box already one resonator pair; the light noise buzz stands in for sister-tine sympathy.
- **All 5 macros (0.5)** — neutral so the explicit per-pad voicing is not perturbed.
- **Output Bus 0, Pad Enabled 1.0.**

## Implementation note (preset generator)

Mirror `glassBellGardenKit()` (`tools/membrum_preset_generator.cpp`): loop `i=0..7`, set `bodyModel=Bell`, `exciterType=Mallet`, and grade `size`, `material`, `decay`, `pan` per the pitch table; apply the shared baseline to the rest. `bodyDampingB3=0.10`, `bodyDampingB1=0.18`, `modeStretch=0.40`, `decaySkew=0.62`, `modeScatter=0.08`, low `noiseLayer*`, bright `clickLayer*`, `tsPitchEnvTime=0`, `morphEnabled=0`. Subdir: a tuned/world category. (No file was written — recipe only.)
