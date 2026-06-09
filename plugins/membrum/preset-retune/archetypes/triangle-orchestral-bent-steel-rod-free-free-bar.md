# Membrum Recipe — Triangle (orchestral, bent steel rod)

## Body decision (deviates from the brief)
The brief suggests **Bell**, but the corrected post-audit **Bell** body is a *tuned church-bell Chladni* series (hum 0.25 / prime 0.5 / tierce 0.6 / quint 0.75 / nominal 1.0) — a near-pitched bell. A triangle is a **bent steel ROD**, i.e. a **free-free Euler-Bernoulli bar**, whose mode ratios are **1.000 : 2.757 : 5.404 : 8.933 : 13.34 : 18.64 …** (roots of cos βL·cosh βL = 1; Fletcher & Rossing §2.14). That is exactly Membrum's **Shell** body. The measured triangle dominant partials **1539 / 4239 / 5658 / 7795 Hz** give 4239/1539 ≈ 2.75 ≈ the free-free 2nd-mode ratio 2.757 — independent confirmation. **Use Shell, not Bell.**

## Exciter
**Impulse** — a hard metal beater on a stiff steel rod is a near-impulsive contact that excites the full inharmonic mode set. (The brief's `fmRatio 0.45–0.55` is a no-op for Impulse; FM Ratio only affects the FMImpulse exciter.)

## Acoustic profile (researched)
- **Pitch:** indefinite — fundamental obscured by nonharmonic overtones (Britannica, Wikipedia).
- **Spectrum:** dense, strongly inharmonic free-free bar series; **energy concentrated above ~4 kHz**; measured dominant tones 1539 / 4239 / 5658 / 7795 Hz.
- **Decay/T60:** very long (seconds), low damping, **highs ring nearly as long as lows** → the defining shimmer.
- **Transient:** ~2–4 ms very bright broadband metal-on-metal tick.
- **Noise:** essentially none as a sustained layer.
- **Pitch glide:** none (rigid rod; "kerthump" is membrane physics).
- **Material:** steel → maximally metallic.

## Key normalized → physical settings
| Param | Norm | Physical target |
|---|---|---|
| Body Model | 0.00 | **Shell** (free-free bar) |
| Exciter | 0.00 | **Impulse** |
| Size | 0.085 | f0 ≈ 1234 Hz, modes fill 3.4/6.7/11/16 kHz |
| Material | 0.95 | brightness ≈ 0.99 (tiny high-freq damping) |
| Decay | 0.85 | ≈ 1.9 s body RT60 |
| Strike Position | 0.18 | near free-end antinode (all modes lively) |
| Mode Stretch | 0.62 | extra inharmonic dispersion (bent-rod) |
| Decay Skew | 0.40 | −0.20 → lift/sustain HIGH partials (shimmer) |
| Body Damping b3 | 0.02 | 2e-5 s → highs ring almost as long as lows |
| Click Mix | 0.95 | very bright contact tick (defines attack) |
| Click Brightness | 0.97 | ≈ 10.6 kHz sharp metallic ting |
| Click Contact | 0.10 | ≈ 2.3 ms hard contact |
| Mode Scatter | 0.12 | organic ~12% detune (irregular shape) |
| Noise Mix | 0.00 | off (no sizzle) |
| Mode Inject | 0.00 | off (would impose a pitch — wrong) |
| Nonlinear Coupling | 0.00 | off (linear steel rod) |
| Pitch Env Time / Tension Mod | 0.00 | no glide |
| Pan | 0.60 | slightly right (accent placement) |
| Level | 0.72 | present but not dominating |

## Why each "off" matters
- **Mode Inject = 0:** a 1/k integer-harmonic series would *pitch* an instrument whose defining quality is *indefinite pitch*.
- **Noise = 0:** the triangle's only noise is the contact click (Click layer), not a sustained hiss.
- **Tension Mod = 0 / Pitch Env = 0:** a rigid metal rod has no tension-modulation glide (membrane-only).
- **Secondary/Coupling = 0:** a triangle is a single resonator.

## Sources
- Fletcher & Rossing, *The Physics of Musical Instruments* §2.14 (free-free bar ratios) — encoded in `shell_modes.h`.
- Triangle indefinite pitch / nonharmonic overtones: Britannica, Wikipedia.
- Measured dominant partials 1539/4239/5658/7795 Hz & low-damping long sustain: Grokipedia / musillection (citing the dynamic-analysis experimental literature) + MDPI *Dynamic Analysis of the Musical Triangles* and JASA Express Letters (shape→sound).