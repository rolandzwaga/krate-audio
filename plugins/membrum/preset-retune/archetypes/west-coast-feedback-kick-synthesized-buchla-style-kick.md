# Membrum Recipe — West Coast Feedback Kick

A Buchla-style **synthesized** kick: a dark Membrane body whose fundamental is swept **220 Hz → 45 Hz** by the pitch envelope, driven by the **Feedback** exciter (self-oscillating body↔exciter loop) and run through a **heavy wavefold (0.30)**, with a **metallic secondary shell (size 0.45 / material 0.85)**, **high tension glide (0.45)** and **nonlinear coupling (0.40)**. The feedback exciter and amplitude-driven nonlinear coupling grow inharmonic overtones on top of the swept sub — a chaotic/complex synth kick, not an acoustic one.

> All values below are **NORMALIZED [0,1]** (the on-wire/preset representation). Physical targets are the denormalized values from `voice_pool.cpp::applyPadConfigToSlot`. Maps 1:1 onto the `Pad` struct in `tools/membrum_preset_generator.cpp` (this is essentially the corrected `modularWestCoastKit()` pad 0, re-stated against the post-audit semantics).

## Body & Exciter
| | Value | Physical target |
|---|---|---|
| **Exciter Type** | `1.0` | **Feedback** (enum 5) — sustained chaotic self-oscillation |
| **Body Model** | `0.0` | **Membrane** (Bessel, 48 modes) — tensionMod legal, carries the swept sub |

## Core body
| Param | Norm | Physical | Why |
|---|---|---|---|
| Material | `0.25` | brightness 0.25 (dark/woody), base decay ~0.31 s | dark body; brightness comes from fold/feedback |
| Size | `0.85` | natural f0 ≈ **71 Hz** | large/low body in the kick register |
| Decay | `0.40` | ~0.21 s flat (b1 override active) | medium-short kick body |
| Strike Position | `0.30` | r/a = 0.27 (off-center) | low-mode weight + some attack |
| Level | `0.80` | linear 0.80 pre-rail | standard loudness under −6 dBFS body budget |
| Body Damping b1 | `0.30` | ~15 s⁻¹ flat | tight kick thump, not a ring |
| Body Damping b3 | `0.30` | 3e-4 s f²-damping | tames top modes (woodier) |
| Air Loading | `0.0` | pure Bessel | synthetic, not acoustic membrane |
| Mode Scatter | `0.0` | pure ratios | chaos comes from fold/feedback, not detune |
| Mode Stretch | `0.45` | physical 1.175 (mild inharmonic) | slight metallic push |
| Decay Skew | `0.50` | 0 (neutral) | natural damping sets balance |

## West-Coast nonlinear / chaos stages
| Param | Norm | Physical | Why |
|---|---|---|---|
| **Feedback Amount** | `0.45` | drive floor → ≤0.85 cap | sustained self-oscillation energy → inharmonic overtones |
| **Fold Amount** | `0.30` | **0.94 rad** sine fold | heavy West-Coast wavefold; dense odd-harmonic brightness |
| **Nonlinear Coupling** | `0.40` | env-level drive, louder=brighter | Buchla loudness↔brightness link, sustained |
| Drive Amount | `0.0` | bypassed | fold+feedback already supply saturation |
| **Tension Mod** | `0.45` | up to ~+2 st energy-driven attack bump | "kerthump" reinforcing the sweep (Membrane-only) |

## Pitch glide (the punch)
| Param | Norm | Physical | Why |
|---|---|---|---|
| **PitchEnv Start** | `0.5207` | **220 Hz** | Buchla-hot start above natural f0 |
| **PitchEnv End** | `0.1761` | **45 Hz** | sub in the 40–50 Hz sweet spot |
| **PitchEnv Time** | `0.06` | **30 ms** | classic 808 pitch-decay (20–70 ms) = click/thump |
| PitchEnv Curve | `0.15` | −0.7 (fast drop) | exponential-style 808 decay |

## Secondary metallic shell (the overtone cloud)
| Param | Norm | Physical | Why |
|---|---|---|---|
| Secondary Enabled | `1.0` | shell bank ON | high inharmonic 'machine' ring |
| Coupling Strength | `0.20` | feedforward 0.20·bodyOut | light head→shell drive |
| Secondary Size | `0.45` | shell f0 ≈ 0.66·head | metallic ring above the fundamental |
| Secondary Material | `0.85` | bright metallic shell | inharmonic overtones |

## Attack click + noise grit
| Param | Norm | Physical | Why |
|---|---|---|---|
| Click Mix | `0.30` | 0.30 (also into exciter feed) | beater click defines attack |
| Click Contact | `0.18` | 2.5 ms | short sharp contact |
| Click Brightness | `0.55` | ~2.2 kHz bandpass | mid-bright beater thud |
| Noise Mix | `0.20` | 0.20 lowpass bed | low grit, not snare hiss |
| Noise Cutoff | `0.50` | ~849 Hz LP | low-mid grit under the sub |
| Noise Decay | `0.30` | ~92 ms | attack-tied, gone before the tail |
| Noise Color | `0.50` | Pink | neutral broadband texture |

## Macros
| Param | Norm | Why |
|---|---|---|
| **Punch** | `0.65` | deeper/faster pitch drop + shorter attack |
| **Complexity** | `0.85` | more coupling + nonlinear brightening + injected modes (chaos intent) |
| Tightness / Brightness / Body Size | `0.5` | neutral — underlying params set directly |
| Pan | `0.5` | dead-center kick |

## Deliberately at default (no-ops or not wanted)
Filter section (Type/Cutoff/Res/Env Amount/Env ADSR) — SVF **bypassed** (Cutoff 1.0 = 20 kHz, Env Amount 0.5 = 0). Mode Inject 0 (fold/feedback/secondary already rich). Morph off. Choke 0, Output Bus 0 (main). FM Ratio / NoiseBurst Duration / Friction Pressure — **no-ops** (wrong exciter). Coupling Amount 0.5 (only audible with kit-level coupling). Noise Resonance 0.2. PitchEnv Knee/Mid/MidFrac/Curve2 — single-segment glide. Pad Enabled 1.0.

## Physics & synthesis rationale
A synthesized kick is one pitched body with a downward pitch glide: 40–50 Hz steady is the kick sweet spot, and a 20–70 ms pitch decay from a higher start gives the trademark click/punch ([Sweetwater](https://www.sweetwater.com/insync/synthesizing-and-creating-your-own-percussion-samples/), [NSLogic 808 tutorial](https://nslogictutorials.wixsite.com/mysite/single-post/2018/03/27/sound-sythesis-tutorial-808-kick-drum), [MusicRadar](https://www.musicradar.com/how-to/how-to-recreate-classic-analogue-drum-sounds-in-your-daw-and-with-hardware)). The West-Coast character is synthesis-derived, not acoustic: a **wavefolder** is an extremely nonlinear transfer that folds the signal back on itself, adding dense (mostly odd) harmonics and a 5th-harmonic-heavy spectrum ([CCRMA wavefolder](https://ccrma.stanford.edu/~jatin/ComplexNonlinearities/Wavefolder.html), [PerfectCircuit waveshapers](https://www.perfectcircuit.com/signal/learning-synthesis-waveshapers), [Kassutronics](https://kassu2000.blogspot.com/2021/11/wavefolder.html)); the **feedback** complex-oscillator path self-oscillates into a chaotic energy cascade ([PerfectCircuit: West Coast](https://www.perfectcircuit.com/signal/what-is-west-coast-synthesis)); and the **low-pass-gate / nonlinear-plate** behavior links loudness to brightness, modeled here by env-level NonlinearCoupling. On Membrane, energy-dependent tension modulation adds the attack pitch bump (Avanzini-Marogna-Bank, JASA 2012). The Buchla 259 wavefolder's nonlinear behavior is characterized in the [DAFx17 virtual-analog paper](https://www.dafx17.eca.ed.ac.uk/papers/DAFx17_paper_82.pdf).