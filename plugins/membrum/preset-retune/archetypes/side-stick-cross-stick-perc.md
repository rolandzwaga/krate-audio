# Membrum Recipe — Side Stick / Cross-Stick (perc)

**GM "Side Stick" (MIDI 37).** The hollow woody **"tok"** of a stick laid across the head and struck against the rim — a *woodblock-type* sound (not a wired snare hit). A short, pitched, click-dominated transient with almost no sustain and no snare-wire hiss.

## Archetype identity (research)
- **Pitch / body band:** tonal click energy ~0.6–1.5 kHz (woody body 500–1000 Hz + HF snap above). The 808 rimshot uses two bridged-T tones at **455 Hz + 1667 Hz**; the 909 uses three. Confirms "short pitched click."
- **Decay / T60:** the defining trait — dies almost instantly (808/909 env ~**10 ms**); cross-stick is "always a lot softer" than a rimshot. Target body RT60 ≈ **130 ms**, highs damped first (wood).
- **Inharmonicity:** struck wooden bar → free-free ratios **1 : 2.757 : 5.404 : 8.933** (F&R §2.14), plus slight irregularity → modest mode-scatter.
- **Noise:** essentially **none** — soft cross-stick barely engages the wires.
- **Pitch glide:** none (rigid bar, no tension modulation).

## Mapping → Membrum
- **Body:** **Shell** (free-free Euler-Bernoulli bar = woodblock/rim-click). On-wire norm **0.4** (Shell = enum 2, idx=round(0.4×5)=2).
- **Exciter:** **Impulse** (norm 0.0) — clean rigid contact; the Click layer carries the audible snap.
- **f0:** Size **0.20** → `1500·0.1^0.20 ≈ 946 Hz` (between the 808's body and snap tones).

## Key normalized baseline
| Param | Norm | Denormalized target | Why |
|---|---|---|---|
| Exciter Type | 0.00 | Impulse | rigid wood-on-wood contact |
| Body Model | 0.40 | Shell (enum 2) | free-free bar = woody tok |
| Material | 0.30 | brightness 0.895, base ~0.34 s | woody (highs short), not metallic |
| Size | 0.20 | f0 ≈ 946 Hz | tonal click in the woody-tok band |
| Decay | 0.16 | RT60 ≈ 0.13 s | dies almost instantly |
| Strike Position | 0.30 | bar pos 0.30 | off-node, balanced hollow timbre |
| Level | 0.78 | linear 0.78 | soft accent, just below default |
| Body Damping b1 | 0.42 | ~21 s⁻¹ | mid-tight flat decay floor |
| Body Damping b3 | 0.10 | 1e-4 s | woody f² damping (highs die first) |
| Mode Scatter | 0.50 | ~6–8% detune | organic stick/block irregularity |
| Click Mix | 0.88 | dominant click | the contact tok IS the sound |
| Click Contact | 0.10 | 2.3 ms | sharp short tick |
| Click Brightness | 0.62 | ~1.9 kHz BP center | woody snap, not metallic |
| Noise Mix | 0.00 | layer bypassed | no snare-wire hiss |
| Filter Cutoff | 1.00 | 20 kHz (bypass) | body+click already shape it |
| Pan | 0.58 | ~16% R | perc accent off the dead-center snare |

## Deliberately default
Filter section (bypassed), Drive/Fold (clean), **all PitchEnv** (rigid bar — no glide; Time=0), ModeInject/NonlinearCoupling (exact bypass), Morph (off), Choke/Bus (none/main), secondary-exciter params (no-op under Impulse), **AirLoading/TensionMod** (Membrane-only no-ops on Shell), Secondary/Coupling resonator (single struck bar), Mode Stretch / Decay Skew (free-free ratios + b1/b3 already correct), macros (neutral, recipe sets params directly).

## Notes (post-audit semantics)
- Voiced against the **corrected** chain: linear voice + measured-strike body norm (N-1) + −1 dBTP bus limiter, so Level/Click mixes are real (un-clipped) controls.
- Two factory reference pads already use this Shell+Impulse, short-decay, click-dominated, no-noise, modeScatter 0.40–0.55 voicing — this recipe consolidates and grounds them.

## Sources
Wikipedia *Rimshot*; Audiosquid *Side stick methods*; Baratatronix *808 Rimshot*; SyntherJack; network-909.de; Music Guy Mixing *Snare EQ*; iDrumTune drum frequencies; Bowser Audio; Wikipedia *TR-909*.