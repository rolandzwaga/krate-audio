# Membrum Recipe — "Acoustic Maple Kick" (Acoustic Bass Drum)

**Body:** Membrane (48-mode Bessel head) · **Exciter:** Mallet · **Category:** Acoustic / Kick

A large air-loaded acoustic bass drum. Built as a clamped circular membrane whose low axisymmetric modes dominate after the inharmonic upper modes damp fast, with a real coupled maple shell, a soft beater contact, a sharp click transient, and a SHORT shallow pitch glide (160→50 Hz / 40 ms) — explicitly NOT the 808's long fully-swept self-oscillating sine.

## Physics it targets
- **Fundamental ~50-60 Hz** (tunable 50-90 Hz). Membrane f0 = 500·0.1^size → size 0.85 = **70.7 Hz** natural head pitch; the pitch-env settles the perceived boom to ~50 Hz. (iDrumTune)
- **Inharmonic Bessel modes** 1 : 1.59 : 2.14 : 2.30 : 2.65 …, struck-center (0,n) series. The (0,1) monopole radiates fast and carries little pitch; the (1,1) rings longer and carries tone. (PSU Russell; Well-Tempered Timpani)
- **Air loading** pulls the upper (m,1) modes toward the near-harmonic timpani series 1 : 1.5 : 2 : 2.44 : 2.9 → deep, less-whistly head. (Rossing 1982)
- **Short T60** — radiating attack is a fraction of a second; musical body ~150-250 ms; HF inharmonic partials die in a few ms (b3·f² damping). (Chowdhury; JASA 150:202; Reid SoS)
- **Beater click** ~1.5-2 kHz (felt/wood), a few-ms impulsive contact, separate from the head. (USPTO patent; iDrumTune/eMastered EQ)
- **Pitch glide** a couple of semitones, fast — modeled as a 160→50 Hz / 40 ms env plus a small energy-driven tension up-chirp. (Reid SoS; Avanzini-Marogna-Bank 2012)
- **vs 808:** real head↔shell coupling + air loading + contact click + short glide, not a clean long sine sweep. (arXiv:2502.07524)

## Parameters (NORMALIZED baseline → physical target)

| Param | Norm | Physical target | Why |
|---|---|---|---|
| Exciter Type | 0.20 | **Mallet** | soft beater contact |
| Body Model | 0.00 | **Membrane** | a bass drum is a circular head |
| Material | 0.35 | woody; ~0.41 s base decay | dark/woody, fast HF damping |
| Size | 0.85 | f0 = **70.7 Hz** | large head |
| Decay | 0.25 | legacy ~0.22 s (overridden by b1) | tight body |
| Strike Position | 0.30 | r/a = 0.27 | near-center pedal beater |
| Level | 0.85 | linear 0.85 | loud foundation |
| PitchEnv Start | 0.4508 | **160 Hz** | impact thump start |
| PitchEnv End | 0.199 | **50 Hz** | settle pitch |
| PitchEnv Time | 0.08 | **40 ms** | short acoustic glide (not 808) |
| PitchEnv Curve | 0.15 | fast-initial exp drop | tension relaxes fastest at impact |
| Air Loading | 0.78 | 0.78→Rossing near-harmonic upper modes | deep, less whistly |
| Body Damping b1 | 0.13 | **6.7 s⁻¹ ≈ 150 ms** floor | tight body ring |
| Body Damping b3 | 0.42 | 4.2e-4 (strong f²) | HF dies in ms, low fundamental remains |
| Tension Mod | 0.18 | subtle energy up-chirp (Membrane-only) | impact tension rise |
| Coupling Strength | 0.35 | body→shell drive 0.35 | real shell body |
| Secondary Enabled | 1.0 | shell bank ON | wooden housing weight |
| Secondary Size | 0.40 | shell f0 ≈ 0.70·head ≈ 49 Hz | deep maple shell |
| Secondary Material | 0.60 | bright-ish wood shell | audible coupled body |
| Click Mix | 0.70 | into exciter + output | beater attack definition |
| Click Contact | 0.20 | 2.6 ms | sharp short contact |
| Click Brightness | 0.45 | ~1.4 kHz bandpass | felt/wood click, warm |
| Noise Mix | 0.10 | thin LP noise | a touch of shell/beater air |
| Noise Cutoff | 0.45 | ~1.0 kHz LP | dark, low |
| Noise Color | 0.20 | **Brown** | low rumble not hiss |
| Noise Decay | 0.20 | ~46 ms | brief air accent |

## Deliberately left at default
Filter section + filter env (SVF bypassed — body damping shapes tilt); Drive/Fold = 0 (linear acoustic body); Mode Stretch / Decay Skew / Mode Scatter neutral (Bessel inharmonicity already correct, keep the sub clean); Mode Inject / Nonlinear Coupling = 0 (would read electronic; exact bypass); Material Morph off (static timbre); PitchEnv Knee/Mid/Curve2 (single-segment glide suffices); FM Ratio / Feedback / NoiseBurst Dur / Friction Pressure (no-ops under Mallet); Choke 0 / Bus 0 / Pan 0.5 (center for mono-compatible sub); Pad Enabled 1.0; all 5 macros neutral 0.5; Coupling Amount 0.5 (only audible with kit-level global coupling); Noise Resonance 0.2 (flat air bed).

## Implementation note for `tools/membrum_preset_generator.cpp`
Maps to a `Pad` with `exciterType = ExciterType::Mallet`, `bodyModel = BodyModelType::Membrane`, and the named fields above; use `toLogNorm(160)`/`toLogNorm(50)` for the pitch-env Start/End (matches the existing `acousticKit()` pad-0 idiom). This recipe is essentially the corrected/strengthened sibling of the current `acousticKit()` pad 0 (which already uses Mallet/Membrane, size 0.85, air 0.78, pitch 160→50/40 ms, secondary shell, tension 0.18) — re-grounded against the post-audit gain staging and cited acoustics.

### Caveat (honest)
In Fletcher & Rossing the air mass physically *lowers* the lowest mode; in **this engine** `kAirLoadingTargetScale` anchors (0,1) and (1,1) and scales the upper (2..5,1) modes UP toward the Rossing series — so here Air Loading shapes the upper-mode ratios (deeper/near-harmonic timbre) while **Size** sets the actual f0. The physical "deep big drum" result is achieved by Size 0.85 + Air 0.78 together, not by Air Loading dropping f0.