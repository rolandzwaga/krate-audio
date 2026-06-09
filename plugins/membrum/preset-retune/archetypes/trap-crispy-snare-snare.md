# Membrum Recipe — "Trap Crispy Snare" (snare)

**Body:** Membrane (air-loaded inharmonic drumhead)  **Exciter:** NoiseBurst (violet-noise bandpass burst)

A very bright modern trap snare: a tight ~204 Hz drumhead with a short wood-shell box under it, swamped by dominant violet-noise wire buzz and a hard high-frequency stick crack, shaped by a fast downward LP filter envelope and a 50 ms 250->170 Hz body thump. Engineered to cut a dense trap mix with a crispy HF snap.

## Acoustic / synthesis basis
- **Pitch ~200-250 Hz.** Acoustic snare head fundamental is ~180-250 Hz; bright/crisp tuning pushes toward ~250 Hz with tight heads (iDrumtune, tune-bot, Chris Brush). Size 0.39 -> 500*0.1^0.39 ~= 204 Hz.
- **Inharmonic head 1 : 1.5 : 2 : 2.4** (Rossing 1982; Madsen UIUC). Membrane body + airLoading 0.55 morphs the Bessel modes onto this real coupled-head/shell series, not a 1:2:3 ladder.
- **Noise is the headline.** The 808 snare = 2 bridged-T tone oscillators (~238/476 Hz, or the 180/330 Hz "0,1 mode" pair) + a HIGH-PASSED white-noise generator with a "Snappy" decay control (SoS, TR-808 service manual / Wikipedia). Trap snares push that noise crispier and higher and stack a bright clap (bandpassed upper-mid noise). Here: noise mix 0.8, **violet** color, ~13.3 kHz LP top, ~130 ms snappy tail.
- **Hard HF transient.** The stick "thwack" is a 1.5-2.5 kHz-and-up contact (SoS, musicalinstrumentworld); trap exaggerates it into a sharp ~10.6 kHz click (brightness 0.97, ~2 ms contact).
- **Fast decay.** Snares-engaged: ~64% gone by 80 ms (Madsen); desired musical decay ~0.5 s (Chowdhury). Body knock kept short (Decay 0.32 -> ~0.18 s); the tail lives in the noise layer.
- **Downward body thump 250->170 Hz / 50 ms** — the decaying 808/909 tone droop, a fast-initial-drop curve.

## Body, exciter & key normalized values
| Param | Norm | Physical target |
|---|---|---|
| Exciter | 0.40 | NoiseBurst |
| Body | 0.20 | Membrane |
| Material | 0.42 | woody-tight body, ~0.42 s base decay |
| Size | 0.39 | head f0 ~204 Hz |
| Decay | 0.32 | body knock ~0.18 s |
| Strike Pos | 0.45 | off-center (r/a~0.41), brighter |
| Level | 0.85 | loud, cuts the mix (under bus limiter) |
| Noise Mix | 0.80 | dominant wire/clap noise |
| Noise Cutoff | 0.95 | ~13.3 kHz LP top |
| Noise Color | 0.96 | Violet (+6 dB/oct) |
| Noise Decay | 0.42 | ~130 ms snappy tail |
| Noise Reso | 0.10 | flat (~Q 0.77) |
| Click Mix | 0.85 | prominent stick crack |
| Click Bright | 0.97 | ~10.6 kHz bandpass center |
| Click Contact | 0.05 | ~2 ms sharp tick |
| NoiseBurst Dur | 0.18 | ~4.3 ms sharp burst |
| Filter Type | 0.00 | Lowpass |
| Filter Cutoff | 0.85 | ~7.6 kHz base |
| Filter Reso | 0.18 | ~Q 2.4 |
| Filter Env Amt | 0.70 | +0.4 (~+1.2 oct upward sweep) |
| Filter Env Atk/Dec/Sus/Rel | 0 / 0.22 / 0 / 0.10 | instant / ~21 ms / 0 / 2 ms |
| PitchEnv Start | 0.548 | 250 Hz |
| PitchEnv End | 0.465 | 170 Hz |
| PitchEnv Time | 0.10 | 50 ms |
| PitchEnv Curve | 0.18 | fast initial drop |
| Air Loading | 0.55 | inharmonic snare head |
| Mode Scatter | 0.30 | ~4.5% detune dither |
| Coupling Strength | 0.55 | shell-box drive (brief value) |
| Secondary Enabled | 1.0 | shell bank on |
| Secondary Size | 0.60 | shell f0 ~112 Hz |
| Secondary Material | 0.45 | short woody box |
| Pan | 0.50 | center |

## Deliberate defaults
Drive/Fold bypassed (0) — crispness is bright noise/click, not saturation. ModeStretch/DecaySkew neutral — air-loaded head already carries the correct inharmonicity. ModeInject 0 and NonlinearCoupling 0 — no synthetic harmonic tone, no amplitude brightening (the bright noise is fixed). Morph off, Choke 0, Bus main. Tension Mod 0 — the spec calls for a downward pitch-env thump, not an upward tension crack. b1 sentinel / b3 material-derived. All 5 macros neutral 0.5 so they don't shift the hand-voiced baseline. PitchEnv Knee off (single-segment glide). FM/Feedback/Friction params are no-ops for NoiseBurst.

## Sources
Sound on Sound — Practical Snare Drum Synthesis; Wikipedia / TR-808 service manual; Madsen (UIUC Physics 406) snare acoustics; iDrumtune & tune-bot & Chris Brush tuning; MusicRadar trap tricks; musicalinstrumentworld snare frequencies; Baratatronix 808 snare; Chowdhury modal drum-fixing.