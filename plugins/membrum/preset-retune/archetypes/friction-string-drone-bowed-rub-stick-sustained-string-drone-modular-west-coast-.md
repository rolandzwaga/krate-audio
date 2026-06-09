# Membrum Recipe — Friction String Drone (bowed / rub-stick sustained drone)

**Body:** String (WaveguideString, Extended Karplus-Strong / Smith digital waveguide)
**Exciter:** Friction (BowExciter — McIntyre–Schumacher–Woodhouse stick-slip)
**Output:** aux bus 1 · **No click** · centered pan
Covers the *modular west-coast drone* and the *tanpura-style sustained drone*.

---

## Why this body + exciter
A drone is a **sustained, harmonically-rich PITCHED string** with a multi-second, slowly-darkening tail. Only the **String waveguide** produces a continuous string drone with the natural **1/n (sawtooth/Helmholtz) harmonic spectrum** — the modal bodies give struck decays. The **Friction exciter** kicks the loop into a Helmholtz-like motion via a short (~46 ms) rosin-jitter stick-slip transient; the *drone itself* is the **long waveguide decay (T60 ≈ 5 s)**, since Phase-2 friction is a struck-bow, not continuous bowing.

> **Important architectural fact:** the String body **bypasses the shared modal bank**, so `Mode Stretch`, `Decay Skew`, `Mode Inject`, `Mode Scatter`, `Air Loading`, per-mode `Damping b1/b3` and `Tension Mod` are **inherent no-ops** here. The drone's inharmonicity/brightness/evolution come from the **waveguide loss filter (driven by Material + Material Morph)**, the **ToneShaper filter envelope**, **Nonlinear Coupling**, and **Fold/Drive** — not from stretch/skew knobs.

## Physics → params (the load-bearing numbers)
- **Pitch f0 ≈ 192 Hz** (Size 0.62). Tanpura Sa runs ~110–220 Hz; this low-tenor drone is clearly pitched with room for overtone evolution.
- **T60 ≈ 5.4 s** (Material 0.5 → base 2.25 s × Decay 0.90 multiplier 2.39), via `rho = 10^(-3/(T60·f0))`. Highs die first → the drone darkens as it rings.
- **1/n harmonic slope** is the steady bowed-string (Helmholtz) spectrum; the bright waveguide loss filter approximates it. Bow force is the **dominant brightness control** → `Friction Pressure 0.45` (firm bow, inside the Schelleng playable wedge).
- **No pitch glide** (`PitchEnv Time 0`); only a subtle bow-force flattening exists in reality, not an iconic glide.
- **No click** — a bowed onset is a *scrape*, carried by the Friction exciter's stick-slip jitter.

## Evolution (the "living drone")
- **Material Morph ON**, Start 0.45 → End 0.70 over ~1.2 s: loop-filter brightness opens after a darker scrape attack (tanpura overtones blooming).
- **Filter Env** sweeps the LP cutoff **up ~+0.7 oct** (slow 83 ms atk / 333 ms dec / 0.55 sus) — west-coast evolving timbre.
- **Nonlinear Coupling 0.5** = louder→brighter sustained waveshaping (bow force / jivari amplitude-dependent buzz).
- **Light Fold (0.22·π) + Drive (≈2.6×, flavour)** = literal west-coast harmonic enrichment.
- **Brown noise bed** (mix 0.16, ~800 Hz LP, ~1.2 s decay) = bow-rosin friction air under the pitch.

## Gain / routing
`Level 0.62` (sustained always-on voices are dense; trim under the −1 dBTP bus limiter). `Output Bus 1` (own pre-master send for reverb). `Pan` centered. No choke (drones overlap).

## Sources
Bowed string Helmholtz/1/n spectrum & bow-force brightness: euphonics.org 11-3-0 & 9-2; Sawtooth (Wikipedia); Violinist's Sound Palette. Tanpura/jivari sustain & overtones: Tanpura/Jivari (Wikipedia), Darbar, PureTones, ScienceDirect tanpura-drone. Waveguide/EKS: Karplus–Strong & Digital-waveguide (Wikipedia), Smith CCRMA. West-coast drone/wavefolding: PsychoSynth, Perfect Circuit, DAFx17 Buchla 259.