# Membrum Recipe — Dholak (World) — Membrane + Impulse, two-pitch hand drum

A North-Indian double-headed barrel hand drum modelled as **two pads**: **HI** (goat-skin treble head, sharp notes) and **LO** (masala-coated buffalo bass head, the sliding "giss"). Both pads use **Body = Membrane**, **Exciter = Impulse**, voiced against the **post-audit (2026-06-07) corrected** signal path (measured-strike body norm, −6 dBFS strike budget, layers ~−18 dBFS, live pitch-env on all bodies, per-mode decaySkew tilt, energy-driven tension glide, per-pad pan).

## Why these choices
- **Membrane, not Bell/Plate/tabla-harmonic.** The dholak head is a *plain* (un-syahi) stretched skin → inharmonic **Bessel** spectrum (1, 1.59, 2.14, 2.30, 2.65…), partly pulled toward 1:1.5:2:2.4 by air-loading and the barrel cavity. This is explicitly *not* the loaded-membrane harmonic drum (tabla/mridangam, C.V. Raman 1920) — the dholak's bass masala lowers pitch and damps highs but does not tune the head to a 1:2:3 series. Membrane is also the only body where **airLoading** and **tensionModAmt** are live, both essential here.
- **Impulse exciter.** Bare fingered/palm contact = short broadband strike; softness/brightness of the "tup" is set by the **Click layer**, the skin "shh" by the **Noise layer**.
- **Two giss mechanisms.** (1) ToneShaper **pitch envelope** rescales the whole modal bank for the audible downward slide (HI 320→220 Hz / 25 ms; LO 220→140 Hz / 80 ms, per brief); (2) **tensionModAmt** adds the Membrane-only *energy-dependent upward* bend (harder hits bend up) — more on the bass head, which players bend most.
- **Secondary shell coupling** gives the wooden (sheesham/mango) barrel body weight (brief: "coupling with secondary").

## Pitch math (Membrane f0 = 500·0.1^size; pitch env sets the audible struck pitch)
- HI size 0.55 → natural f0 ≈ **141 Hz** (sets ratio spacing/ring); pitch-env start 320 Hz drives the heard pitch.
- LO size 0.65 → natural f0 ≈ **112 Hz**; pitch-env start 220 Hz drives the heard pitch.
- `toLogNorm(hz)=ln(hz/20)/ln(100)`: 320→0.6021, 220→0.5207, 140→0.4226.

## Parameter table (normalized → physical)
| Param (offset) | HI norm → phys | LO norm → phys |
|---|---|---|
| Exciter (0) | 0.0 Impulse | 0.0 Impulse |
| Body (1) | 0.0 Membrane | 0.0 Membrane |
| Material (2) | 0.42 (bright skin) | 0.33 (dark masala) |
| Size (3) | 0.55 → 141 Hz | 0.65 → 112 Hz |
| Decay (4) | 0.30 (~0.46×) | 0.40 (~0.69×) |
| Strike Pos (5) | 0.62 → edge (rim) | 0.30 → centre |
| Level (6) | 0.80 | 0.85 |
| Filter Type/Cutoff (7,8) | LP / 1.0 bypass | LP / 0.82 ≈ 6.4 kHz |
| PitchEnv Start/End/Time (13,14,15) | 320 / 220 / 25 ms | 220 / 140 / 80 ms |
| PitchEnv Curve (16) | 0.15 (−0.7 fast drop) | 0.15 |
| Decay Skew (22) | 0.58 (+0.16) | 0.66 (+0.32) |
| Nonlin Coupling (24) | 0.12 | 0.18 |
| Noise Mix/Cutoff/Decay/Color (42,43,45,46) | 0.14 / 560 Hz / 49 ms / Pink | 0.10 / 230 Hz / 49 ms / Brown |
| Click Mix/Contact/Bright (47,48,49) | 0.42 / 2.9 ms / 2.0 kHz | 0.32 / 3.65 ms / 660 Hz |
| Body Damping b1/b3 (50,51) | 0.34 / 1.4e-4 | 0.28 / 2.2e-4 |
| Air Loading (52) | 0.45 | 0.60 |
| Mode Scatter (53) | 0.10 | 0.10 |
| Coupling Str / Sec En (54,55) | 0.30 / on | 0.40 / on |
| Secondary Size/Material (56,57) | 0.45 / 0.35 | 0.55 / 0.35 |
| Tension Mod (58) | 0.16 (~+0.4 st) | 0.34 (~+1 st) |
| Pan (64) | 0.58 (slight R) | 0.42 (slight L) |
| Mode Stretch (21) | 0.333 physical | 0.333 physical |

## Left at default (physical reason)
Filter Resonance/Env (9,10,17–20): no filter motion on a skin drum. Drive/Fold (11,12): no synth saturation. PitchEnv Knee/Mid/Frac/Curve2 (60–63): the giss is one smooth glide. Morph (25–29): material static per hit. **Mode Inject (23): leaving it off keeps the authentic inharmonic Bessel spectrum** instead of imposing a tabla-like 1/k harmonic series. Choke (30) / Output Bus (31): hand drums ring freely, main bus. FM/Feedback/NoiseBurst/Friction (32–35): no-ops for Impulse. Coupling Amount (36): no global coupling engine in this kit. Macros (37–41) neutral 0.5: all underlying params set explicitly. Noise Resonance (44): broadband contact noise, no pitched peak. Pad Enabled (59): both on.

## Sources
- Dholak construction, heads, masala, "giss": en.wikipedia.org/wiki/Dholak; india-instruments.com; carvedculture.com; blogv.it.com; musicwala.wordpress.com
- Membrane inharmonicity / air-loading: en.wikipedia.org/wiki/Vibrations_of_a_circular_membrane; Fletcher & Rossing, *Physics of Musical Instruments*; arxiv math-ph/0001030
- Why dholak ≠ harmonic loaded drum (contrast): C.V. Raman, *Nature* 104:500 (1920), ed.iitm.ac.in/~raman/1920Nature.pdf; croor.wordpress.com
- Damping law (b1 + b3·f²): Chaigne & Askenfelt 1994 (per audit §3-A). Tension glide: Avanzini-Marogna-Bank 2012 (per audit N-1).