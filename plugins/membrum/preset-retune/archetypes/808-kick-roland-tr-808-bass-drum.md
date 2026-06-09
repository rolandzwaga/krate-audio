# Membrum Recipe — "808 Kick" (Roland TR-808 Bass Drum)

**Body:** Membrane · **Exciter:** Impulse
*A bridged-T resonant sine with an envelope-swept tuning — NOT a struck membrane. All inharmonicity / air-loading / coupling / secondary axes are zeroed so the modal bank degenerates toward a single clean decaying sub-sine.*

## Acoustic / synthesis profile (researched)
- **Source:** bridged-T bandpass network kicked into self-oscillation by a ~1 ms trigger pulse → **decaying sine wave** at its resonant frequency. (Werner et al., CCRMA; Baratatronix.)
- **Fundamental:** service manual **56 Hz**; measured as low as **48 Hz**; transfer-function calc **49.4 Hz** (G1 +14¢). Sub range ~30–80 Hz.
- **Partials/inharmonicity:** steady state is an **essentially pure single sinusoid** — no modal/inharmonic ladder. Harmonics appear only at onset (transient + slight transistor 2nd-harmonic) and fade in tens of ms. This is *why* all physics axes are zeroed.
- **Pitch glide ("snap"):** attack briefly pushes tuning up to **~130 Hz (C3 −11¢)** then relaxes to the fundamental in **~6 ms** — heard as the percussive snap. Producers widen it to a longer audible "boom" sweep.
- **Decay (T60):** **50–800 ms**, **300 ms** at center; set by feedback into the bridged-T.
- **Transient/click:** complex transient that smoothly becomes the sine; intentionally **click-light** (a lowpass *reduces* its click).
- **Noise:** **none** — deterministic resonator, no noise generator.
- **Material:** electronic, not physical → dark/sine-like (Membrum Material 0.15).

## Mapping onto Membrum
| Stage | Choice | Why |
|---|---|---|
| Exciter | **Impulse** | ~1 ms pulse kicks the resonator → clean decaying sine |
| Body | **Membrane**, all physics axes OFF | degenerates to a single low mode = the bridged-T sub-sine |
| Pitch | **PitchEnv 200 → 40 Hz, 30 ms, exp curve** + **Tension 0.30** | downward "boom" sweep; tension reinforces the upward attack-snap overshoot |
| Tone | Material 0.15, Size 0.9 (f0 ≈ 63 Hz), Decay 0.35 (~300 ms) | deep, weakly-damped near-sine ring |
| Layers | Noise **off**, Click **0.35 / 2.6 ms / ~740 Hz** | no hiss; a small dark click for attack definition only |

## Baseline normalized values (on-wire)
```
Exciter Type   = 0.0        -> Impulse
Body Model     = 0.0        -> Membrane
Material       = 0.15       -> dark / sine-like
Size           = 0.9        -> f0 = 62.9 Hz
Decay          = 0.35       -> ~300 ms boom
Strike Pos     = 0.3        -> neutral (single-mode, irrelevant)
Level          = 0.85       -> hot, off the rail (N-1)
PitchEnv Start = 0.5        -> 200 Hz
PitchEnv End   = 0.15051    -> 40 Hz
PitchEnv Time  = 0.06       -> 30 ms (enables glide)
PitchEnv Curve = 0.15       -> -0.7 (fast exp drop)
Mode Stretch   = 0.333333   -> 1.0 neutral (no inharmonicity)
Decay Skew     = 0.5        -> 0 neutral
Mode Inject    = 0.0        -> off
Nonlin Couple  = 0.0        -> off
Noise Mix      = 0.0        -> noise layer OFF
Click Mix      = 0.35       -> light click
Click Contact  = 0.2        -> 2.6 ms
Click Bright   = 0.3        -> ~740 Hz (dark thud)
Air Loading    = 0.0        -> off (electronic, not acoustic)
Mode Scatter   = 0.0        -> off (deterministic)
Coupling Str   = 0.0        -> off
Secondary En   = 0.0        -> off
Tension Mod    = 0.30       -> snap reinforcement (Membrane-only)
Pan            = 0.5        -> center
```
*All other per-pad params left at archetype defaults (filter bypassed, no drive/fold, no morph, b1/b3 sentinels, macros neutral, exciter-secondary params no-op for Impulse).*

## Sources
- Werner et al., *A Physically-Informed, Circuit-Bendable Digital Model of the Roland TR-808 Bass Drum* (CCRMA)
- Baratatronix, *808 BD Synthesis* — bridged-T, 49.4 Hz, 130 Hz/6 ms snap, 50–800 ms decay
- Kurt James Werner, ChucK TR-808 BD emulation notes
- *Harmonic & Transposition Constraints… TR-808 Bass Drum*, arXiv:2502.07524
- MusicRadar 808 tutorial; Sweetwater percussion-synthesis primer; Electronic Music Wiki TR-808