# Membrum Recipe — "Sub-Bell Perc (modular)" (west-coast tuned FM-bell)

A low FM-bell percussion voice with gritty, evolving metallic timbre — a Buchla-style tuned perc element. **Bell body + FMImpulse exciter**, driven inharmonic via a high FM ratio and made gritty/evolving via amplitude-driven nonlinear waveshaping + waveshape drive.

## Archetype summary
| | |
|---|---|
| **Body** | Bell (16-mode church-bell Chladni bank, Simpson ratios hum0.25/prime0.5/tierce0.6/quint0.75/nominal1.0) |
| **Exciter** | FMImpulse (Chowning bell-FM operator pair) |
| **Character** | Low tuned strike pitch (~357 Hz nominal, ~89 Hz hum sub), medium ~0.7 s ring, bright metallic ping attack, inharmonic/clangorous, evolving brightness |

## Acoustic & synthesis basis
- **Bell partials are inharmonic with the minor-third tierce.** A well-tuned bell rings at 0.25/0.50/0.60/0.75/1.00 × nominal (Simpson true-harmonic tuning — Hibberts; Perrin/Charnley/DePont 1983; Open U. Chladni study). The 0.60 tierce is a minor third — the bell's signature. Membrum's `kBellRatios` already encodes this, so the Bell body **is** a real bell.
- **Strike pitch = nominal partial**; the hum two octaves below (0.25×) supplies sub weight (Hibberts, strike.htm). Size 0.35 → nominal ≈ 357 Hz, hum ≈ 89 Hz.
- **Low partials ring long, high partials are intense but short** — modeled by a slight negative Decay-Skew tilt (lifts/lengthens upper partials for shimmer while the sub hum/prime carry the tail).
- **Chowning FM bell:** inharmonic c:m ratio with the modulation-index envelope decaying faster than the amplitude envelope gives a bright→pure metallic strike. Here we use a *high* ratio (3.16) — far from the tame 1.4 cast-bell — for a dense, clangorous west-coast spectrum (CCRMA FM tutorial; Cycling '74).
- **West-coast grit & evolution** come from adding harmonics (FM + waveshaping) and amplitude-dependent timbre, the Buchla complex-oscillator idiom (learningmodular; Perfect Circuit).

## ⚠ Architecture note — "feedback 0.30" cannot route through FMImpulse
`FMImpulseExciter::prepare()` hard-codes `carrier_.setFeedback(0)` / `modulator_.setFeedback(0)`, and the per-pad **Feedback Amount** param (offset 33) is forwarded **only** to the `FeedbackExciter` (`exciter_bank.h:144-148`). With FMImpulse selected, Feedback Amount is an **exact no-op**. The requested "feedback / gritty / evolving" character is therefore delivered the physically-honest way:
- **High FM Ratio 0.72** (→ modulator ratio 3.16) for dense inharmonic FM grit,
- **NonlinearCoupling 0.40** — the audit-correct env-level waveshaping (louder/early = more odd harmonics, mellowing through the ring),
- **Drive 0.30** — west-coast ReciprocalSqrt waveshaping (harmonics, not level, after M-2 makeup).

## Normalized baseline (every meaningful param)
| Param | Norm | Physical target | Why |
|---|---|---|---|
| Exciter Type | 0.80 | FMImpulse (idx 4) | Chowning bell-FM strike |
| Body Model | 0.80 | Bell (idx 4) | true church-bell Chladni bank |
| Material | 0.60 | brightness 0.88, base decay 1.0 s | cast metal, low b3 |
| Size | 0.35 | nominal ≈357 Hz, hum ≈89 Hz | low "sub" bell |
| Decay | 0.40 | ring ≈0.68 s | medium tuned-perc decay |
| Strike Position | 0.20 | azimuth 0.31 rad (near antinode) | hard clapper, full partials |
| Level | 0.80 | linear 0.8 | default loudness |
| FM Ratio | 0.72 | mod ratio 3.16 | dense inharmonic grit |
| Mode Stretch | 0.45 | phys 1.175 (mild stretch) | rougher alloy, off-just upper partials |
| Decay Skew | 0.42 | phys −0.16 | upper-partial shimmer, sub tail |
| Nonlinear Coupling | 0.40 | env-level waveshape drive | gritty, evolving (feedback substitute) |
| Drive Amount | 0.30 | internalDrive 3.7 | west-coast waveshaping warmth |
| Click Mix | 0.50 | attack transient | metallic clapper ping |
| Click Brightness | 0.80 | ≈4750 Hz bandpass | sharp metallic tick |
| Click Contact | 0.20 | 2.6 ms | crisp hard contact |
| Noise Mix | 0.12 | quiet | tonal bell, slight air |
| Noise Cutoff | 0.75 | ≈4900 Hz LP | high airy metal |
| Noise Color | 0.85 | Violet | bright metallic hiss |
| Noise Decay | 0.25 | ≈67 ms | early sizzle only |
| Pan | 0.50 | center | tuned lead element |
| Air Loading | 0.00 | (Bell ignores) | membrane-only, explicitly off |
| Body Damping b1 | −1.0 | sentinel → Decay knob | let Decay set ring |
| Body Damping b3 | −1.0 | sentinel → Material 0.88 | metallic long upper partials |

## Left at default (coverage policy)
- **Feedback Amount / NoiseBurst Duration / Friction Pressure** — wrong-exciter no-ops for FMImpulse.
- **ToneShaper filter + filter env** — bypassed (cutoff 1.0, env amt 0.5); spectrum shaped by Material/FM.
- **Fold** — off; folding smears a tuned bell's pitch.
- **Pitch envelope (all 12 fields, Time=0)** — a bell holds its strike pitch; no membrane glide. FM index env supplies the spectral sweep.
- **Mode Inject** — off (exact bypass); a harmonic series fights the bell's inharmonicity.
- **Mode Scatter** — 0; keep Simpson ratios exact (deliberate inharmonicity comes from Stretch).
- **Material Morph (5 fields)** — off; evolution comes from NonlinearCoupling.
- **Coupling Amount 0.5 / Secondary shell (4 fields) off / Tension Mod 0** — single tuned bell, no second body, tension is membrane-only.
- **All 5 macros at 0.5** — neutral; baseline lives in the explicit params.
- **Choke 0 / Output Bus 0 (main) / Pad Enabled 1 / Noise Resonance 0.2** — routing & defaults.

## Sources
- Chowning, *The Synthesis of Complex Audio Spectra by Means of Frequency Modulation* (1973) — bell c:m & exponential index/amp envelopes.
- CCRMA FM tutorial; Cycling '74 FM synthesis tutorial — inharmonic c:m → bell spectra, index→brightness.
- Hibberts, *Sound of Bells* (basic-principles-of-bell-tuning, strike.htm, identifying-bell-partials) — Simpson ratios, strike pitch = nominal.
- Perrin, Charnley & DePont (1983) JSV 90(1); Open U. *Partial Frequencies and Chladni's Law in Church Bells* — normal-mode census, partial decay order.
- Learning Modular / Perfect Circuit — west-coast/Buchla complex-oscillator FM+waveshaping.
- Vienna Symphonic Library Glockenspiel — metallic attack/ring of tuned metal perc.