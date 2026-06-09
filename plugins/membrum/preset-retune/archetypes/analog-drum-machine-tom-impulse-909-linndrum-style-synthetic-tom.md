# Membrum Recipe — Analog Drum-Machine Tom (impulse)

909/LinnDrum-style synthetic tom. A punchy, short, pitch-swept sine with a click attack — snappier than the 808 boom-tom. Built as **Membrane + Impulse** (909/trap) with all acoustic-realism axes zeroed, so the body behaves like a clean tuned oscillator rather than a real drumhead.

## Body & Exciter
- **Body: Membrane (0).** Bessel circular head; f0 = 500·0.1^size. Only body with drum-register f0 + Membrane-only tension glide. Realism axes (airLoading, modeScatter, coupling) **zeroed** → behaves like a synthetic VCO, not an acoustic head.
- **Exciter: Impulse (0).** Sharp raised-cosine click rings the body — the 909/trap pulse-triggered ringing-oscillator character. *LinnDrum variant → Mallet (0.2)* for the softer sampled-acoustic attack.

## Acoustic / synthesis basis
| Aspect | Real / circuit fact | Source |
|---|---|---|
| Pitch range | 808 toms 80–220 Hz (Low/Mid/High); 909 VCO ratio 1:1.5:2.77, TUNE-swept, snappier | baratatronix; modwiggler |
| Pitch glide | 808 subtle downward sweep from diode current-starvation; synth canon = start high, fall over 50–100 ms | baratatronix; SOS |
| Decay (T60) | 808 toms 100 / 130 / 200 ms (High/Mid/Low); 909 shorter | baratatronix |
| Mode structure | acoustic tom inharmonic Bessel (1.0, 1.59, 2.14, 2.30…), no definite pitch — but machine tom is deliberately a **pure pitched oscillator** | euphonics.org; Illinois PHYS406 |
| Click | atonal noise burst at strike | SOS |
| Noise | 808 filtered-**pink** "fake reverb", env slightly longer than body; 909 Tom Noise Generator — small & dark | baratatronix |

## Baseline normalized params (mid-tom; grade across a 6-tom set)
- **Exciter 0 (Impulse)**, **Body 0 (Membrane)**
- **Material 0.35** (grade 0.20→0.55 low→high), **Size 0.5** (grade 0.65→0.28 → ~112→263 Hz steady f0), **Decay 0.28** (grade up to 0.32 on big toms)
- **Level 0.78**, **Strike Pos 0.3**, **Pan 0.5**
- **PitchEnv Start 0.559 (≈290 Hz)** → **End 0.430 (≈110 Hz)**, **Time 0.1 (50 ms)**, **Curve 0.15 (exp fast drop)** — grade Start 240→590 Hz, End 85→210 Hz across the set
- **Body Damping b1 0.30 (≈15 s⁻¹)** (grade 0.30→0.42 large→small), **b3 0.18 (mild woody HF rolloff)**
- **Click Mix 0.40 / Contact 0.3 (2.9 ms) / Brightness 0.6 (≈1.9 kHz)**
- **Noise Mix 0.05 / Cutoff 0.45 (≈740 Hz) / Color 0.30 (Pink) / Decay 0.3 (≈84 ms)** — the 808 "fake reverb" tail
- **airLoading 0.0, modeScatter 0.0, modeStretch 0.333 (neutral), decaySkew 0.5 (neutral)** — keep it a precise pitched oscillator
- **tensionModAmt 0.0** — base. **TRAP variant → 0.55** for the modern energy-driven up-then-relax bend (+ Drive ~0.18, Click brightness ~0.78).

## Per-tom grade table (drop into a kit loop)
| Tom | Size | Material | Start Hz | End Hz | b1 |
|---|---|---|---|---|---|
| Floor | 0.65 | 0.20 | 240 | 85 | 0.30 |
| Low | 0.55 | 0.28 | 290 | 100 | 0.33 |
| Low-mid | 0.48 | 0.34 | 350 | 120 | 0.36 |
| Mid | 0.40 | 0.42 | 420 | 145 | 0.38 |
| High-mid | 0.34 | 0.50 | 500 | 175 | 0.40 |
| High | 0.28 | 0.55 | 590 | 210 | 0.42 |

`toLogNorm(hz) = ln(hz/20)/ln(100)`; PitchEnv Time 0.10 (50 ms), Curve 0.15.

## Deliberate defaults (zeroed / neutral)
ToneShaper filter **bypassed** (cutoff 1.0); **Drive 0 / Fold 0** (base tom is clean); **ModeInject 0, NonlinearCoupling 0, Morph off** (static timbre — pitch env carries all motion); **coupling/secondary off** (no wood shell); **PitchEnv knee off** (single-segment glide); FM/Feedback/NoiseBurst/Friction params inert under Impulse; macros neutral 0.5 (kit may add macroPunch≈0.65); choke 0 / bus 0; enabled 1.

## Notes on post-audit semantics honored
- Body norm uses the corrected **measured-strike** normalization (N-1); Level is a true linear attenuation before the transparent hard-clip rail (H-1/H-4).
- Pitch env now drives the body on every body type, but here it stays on Membrane (H-3 fix).
- tensionModAmt rebuilt onto gain-invariant modal energy (N-1) and is **Membrane-gated** — correct for this archetype.
- decaySkew/modeStretch kept neutral; the Phase-2 stretch index/B_max fix means non-zero stretch would now audibly inharmonic-ize the tom, which we do NOT want.