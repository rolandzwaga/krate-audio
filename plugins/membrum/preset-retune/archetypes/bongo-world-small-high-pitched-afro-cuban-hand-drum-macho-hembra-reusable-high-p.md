# Membrum Recipe — "Bongo" (world)

Small, high-pitched Afro-Cuban hand drum played in pairs (**macho** = smaller/higher, **hembra** = larger/lower-by-~an-octave). A tight, pitched skin-head tone with a sharp fingertip/slap attack and a short ring. Also reusable as a generic **high-perc filler** in other percussive kits. Voiced against the **corrected post-audit signal path** (linear voice + measured-strike body norm + bus limiter; airLoading & tension are Membrane-only; per-pad pan).

## Body & exciter
- **Body Model: Membrane** (norm 0.0) — Bessel circular drumhead, 48 modes, `f0 = 500·0.1^size`. Only body that honors `airLoading` and `tensionModAmt`, both of which a real hand drum needs.
- **Exciter: Impulse** (norm 0.0) — bare-finger / edge strike = a hard, very short, broadband raised-cosine click. The bright **Click layer** carries the fingertip/slap "pop".

## Acoustic targets (researched)
| Property | Bongo reality | Source |
|---|---|---|
| Head size | macho ~20 cm/8 in, hembra ~25 cm/10 in; calfskin or synthetic on a wood shell | Wikipedia |
| Pitch (open tone) | macho high — "~2 octaves above middle C" (B4–D5 nominal; sustained ~G4–B4, ≈390–500 Hz); hembra ~an octave below | get-tuned.com, SoundCy |
| Mode ratios | inharmonic Bessel: 1.00 : 1.59 : 2.14 : 2.30 : 2.65 : 2.92 … (no clear pitch by itself; tightness+air-loading focus it) | Wikipedia (circular membrane) |
| Decay | short — quick "tok" (open) to a muted slap; no long sustain | SoundCy |
| Transient | sharp, bright fingertip/edge contact (~2–3 ms); slap = a "pop" | SoundCy |
| Noise | almost none (tuned skin, unlike a snare) | — |
| Pitch glide | small downward "kerthump" as strike-tension relaxes | Avanzini et al. 2012 |

## Pitch via the pitch envelope (the load-bearing numbers)
`toLogNorm(hz)=ln(hz/20)/ln(100)`. With **PitchEnv Time > 0**, the body fundamental is seeded at Start and settles to End, so **Size sets character, the env sets pitch**:
- **Macho (hi):** Start 420 Hz (norm **0.6585**) → End 350 Hz (norm **0.6175**), Time 20 ms (norm 0.04), fast-fall curve (norm 0.15).
- **Hembra (lo) variant:** Start 340 Hz (norm **0.6131**) → End 280 Hz (norm **0.5719**); Size 0.40, Decay 0.32 (everything else as macho).

This keeps the pair the conventional ~a 4th apart and inside the measured macho/hembra band.

## Key normalized baseline (macho)
Exciter Impulse (0.0) · Body Membrane (0.0) · Material **0.55** · Size **0.32** (~239 Hz natural) · Decay **0.28** · StrikePosition **0.30** · Level 0.80 · PitchEnv 420→350 Hz / 20 ms / curve 0.15 · TensionMod **0.22** · AirLoading **0.42** · ModeScatter 0.10 · b1 **0.30** (15 s⁻¹) · b3 **0.10** (woody) · Click mix **0.55** / contact 0.15 / brightness **0.72** (~2.9 kHz) · Noise mix **0.10** (Pink) · Coupling 0.25 + Secondary ON (size 0.30, mat 0.40) · Pan center.

## Variants
- **Hembra:** see pitch numbers above (Size 0.40, slightly longer Decay).
- **Slap:** StrikePosition → ~0.10 (edge), Decay → ~0.18, Click mix → ~0.85 / brightness → ~0.85, Macro Punch → ~0.85.
- **Lean high-perc filler** (latinPercKit pad 13): drop Secondary/coupling/tension, Size 0.25, Decay 0.18, StrikePosition 0.20, Click mix 0.65, b1 0.32 — a cheaper, drier high tick.

## What's deliberately left at default
ToneShaper filter (bypassed, cutoff 1.0), Drive/Fold (0.0), ModeStretch/DecaySkew (neutral — native Bessel inharmonicity + b3 carry the timbre), ModeInject & NonlinearCoupling (exact bypass), Morph (off), Choke 0, Output bus main, all four exciter-secondary params (FM/Feedback/NoiseBurst/Friction — no-ops for Impulse; the *bramido* friction-rub is a specialty, not the core voice), all five macros neutral (so explicit values pass through), PitchEnv Knee off.

Body = **Membrane**, Exciter = **Impulse**. Anchored to the established post-audit `handDrumsKit` Bongo hi/lo (pads 6/8) and `latinPercKit` filler (pad 13).