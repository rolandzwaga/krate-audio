# Membrum Recipe — Trap Sub 808 Kick

**Archetype:** Trap Sub 808 Kick (kick) · **Body:** Membrane · **Exciter:** Impulse

A long-sustaining, 808-derived tonal sub-bass kick: a near-pure low sine with an exaggerated, slow downward pitch glide and a long ring, used as a tuned bassline element. Maps directly onto Membrum's `trapModernKit()` pad[0] and is voiced against the CORRECTED post-audit semantics (linear voice + N-1 measured-strike body norm + −1 dBTP bus limiter; tension glide on the gain-invariant modal-energy driver).

## The real sound (research)

- **Source:** A TR-808 bass drum is a **bridged-T bandpass filter self-oscillating** when hit by a ~1 ms trigger pulse → a **decaying pure sine**. Fundamental ~**48–56 Hz** (manual 56 Hz; computed 49.4 Hz = G1+14¢). ([baratatronix](https://www.baratatronix.com/blog/808-bd-synthesis), [Wikipedia](https://en.wikipedia.org/wiki/Roland_TR-808))
- **Trap usage:** tune the sub to the key in the **30–60 Hz** window, **extend the decay to 1–3 s** so it sustains like a bass note, and **glide** between notes (30–60 ms quick, 100–200 ms slow). ([liveschool](https://blog.liveschool.net/808-tutorial/), [wtmh](https://wtmhstudio.com/trap-mixing-advanced-techniques/))
- **Transient:** the 808's own attack is a ~6 ms barely-noticeable pitch snap (≈49→130 Hz); modern trap adds a brighter **click** for small-speaker attack. ([baratatronix](https://www.baratatronix.com/blog/808-bd-synthesis))
- **Harmonics:** near-pure sine; 'distorted 808' adds **odd harmonics via saturation** so the note reads on phones without a fundamental. ([songmixmaster](https://songmixmaster.com/how-to-mix-an-808-bass-in-trap-hip-hop-music))

## Body / Exciter choice

- **Membrane** — a large-Size Bessel head rings as a clean low sine, and it's the **only** body the tension glide is gated to (and an extreme glide defines this archetype).
- **Impulse** — the digital analogue of the 808's 1 ms trigger kick; clean in-phase energy, no added character.

## Parameters (NORMALIZED → physical)

| Param | Norm | Physical | Why |
|---|---|---|---|
| Exciter Type | 0.0 | Impulse | 808 trigger-kick analogue |
| Body Model | 0.0 | Membrane | sub sine + only body with tension glide |
| Material | 0.10 | brightness 0.10, base decay 0.215 s | dark = clean sub, no overtones |
| Size | 0.95 | f0 = 56.1 Hz | 808 fundamental range / long ring |
| Decay | 0.65 | ~1.34× decay mult | long sustaining tail |
| Strike Position | 0.30 | r/a = 0.27 | fundamental-dominant strike |
| Level | 0.92 | linear 0.92 | loud bassline element (under −6 dB body budget) |
| Filter Cutoff | 1.0 | 20 kHz = bypass | no post-body filtering |
| Drive Amount | 0.18 | drive 2.62, unity makeup | small-speaker harmonics (flavour, not level) |
| PitchEnv Start | 0.5484 | 250 Hz | exaggerated boom start |
| PitchEnv End | 0.1215 | 35 Hz | tuned sub settle |
| PitchEnv Time | 0.06 | 30 ms | slow musical 808 glide (enables env) |
| PitchEnv Curve | 0.15 | curveAmt −0.7 | exp fast-drop (classic 808 shape) |
| Decay Skew | 0.45 | phys −0.10 | keep lows dominant through the tail |
| Click Mix | 0.42 | mix 0.42 | attack tick for small speakers |
| Click Contact | 0.18 | 2.5 ms | tight, defined click |
| Click Brightness | 0.32 | ~760 Hz | mid-low click, not a sharp tick |
| Body Damping b1 | 0.30 | 15.1 s⁻¹ → t60≈0.46 s | sets the long tail length |
| Body Damping b3 | 0.30 | 3e-4 s | kill upper modes → pure sine |
| Air Loading | 0.0 | pure Bessel | clean synthetic sub |
| **Tension Mod** | **0.85** | energy glide, cap ~+3 st, Membrane-only | extreme nonlinear 'kerthump' movement |
| Noise Mix | 0.0 | layer off | 808 has no hiss |
| Pan | 0.5 | center | mono sub for translation |

## Defaulted (no-op or neutral for this archetype)

Filter Type/Resonance/Env-ADSR (filter bypassed); Morph (static timbre); Choke (must not be cut); Output Bus (main); FM/Feedback/NoiseBurst/Friction secondary params (Impulse selected); Coupling Amount + Secondary Size/Material (coupling off); macros Tightness/Brightness/Body Size/Punch/Complexity (neutral 0.5 — explicit per-param voicing ships, macros stay overlays); Noise Cutoff/Reso/Decay/Color (noise off); PitchEnv Knee/Mid/MidFrac/Curve2 (single-segment glide).

## Key denorm math (verified)

- `tsPitchEnvStart = ln(250/20)/ln(100) = 0.5484` → 250 Hz; `End = ln(35/20)/ln(100) = 0.1215` → 35 Hz; `Time = 0.06·500 = 30 ms`.
- `Size`: f0 = 500·0.1^0.95 = 56.1 Hz; `b1` = 0.2 + 0.30·49.8 = 15.1 s⁻¹.
