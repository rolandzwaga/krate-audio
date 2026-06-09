# Membrum Recipe — "LinnDrum Snare"

**Archetype:** Vintage sample-based drum-machine snare (Linn LM-2 / LinnDrum, 1982–85)
**Body:** Membrane · **Exciter:** NoiseBurst · **Physics:** OFF (single-layer, no secondary metallic body)

All values below are NORMALIZED [0,1] (preset/on-wire form). Physical target given alongside.

---

## 1. What the LinnDrum snare actually is

The LinnDrum snare is an **8-bit companded (µ-law, AM6070 DAC, ~12–13-bit effective dynamic range) digital SAMPLE of a real ~14″ wood snare** struck by session drummer Art Wood, recorded at **28–35 kHz** ([oramics LM-2](https://oramics.github.io/sampled/DM/LM-2/), [gearspace](https://gearspace.com/gear/linn-electronics/linndrum-lm-2)). It is famously a bit **distorted** (the companding grit) and reads as **tight, dry and distinctly early-80s** — thinner and simpler than the 808 (two-oscillator + long noise) or 909 (noise-heavy) synth snares ([MusicRadar](https://www.musicradar.com/drums/electronic-drums/much-like-its-predecessor-the-909-was-also-a-commercial-flop-and-arguably-more-so-than-the-808-how-the-drum-machine-grew-into-a-viable-alternative-to-real-drummers)).

**Acoustic target** (the real snare it sampled):
- **Fundamental / pitch:** 14″ snare body ≈ **170–220 Hz** (low ~160–170, high ~200) ([idrumtune](https://www.idrumtune.com/snare-drum-tuning/), [musicalinstrumentworld](https://www.musicalinstrumentworld.com/archives/16179)). A brief stick-energy **head-tension crack** (~240 Hz) relaxes to the tuned ~170 Hz body → the spec's pitchEnv **240→170 Hz** (a small crack, NOT an 808 boom).
- **Mode structure:** two-headed, air-coupled; INHARMONIC Bessel membrane modes (weakly pitched). Mode *pairs* at low freq, heads decouple at HF ([Rossing & Bork, JASA](https://pubs.aip.org/asa/jasa/article/85/S1/S33/730120/Modes-of-vibration-and-sound-radiation-from-a)).
- **Noise (the defining feature):** snare-wire buzz **3–5 kHz**, head/stick contact **6–10 kHz**, broadband and bright ([iZotope](https://www.izotope.com/en/learn/5-mixing-tips-for-better-snare-drums.html), [sonicbids](https://blog.sonicbids.com/the-ultimate-eq-cheat-sheet-for-every-common-instrument), [Madsen, Physics 406](https://courses.physics.illinois.edu/phys406/sp2017/Student_Projects/Spring16/Robert_Madsen_Physics_406_Final_Report_Sp16.pdf)).
- **Decay / T60:** snares-on is FAST — **64% of peak lost by 0.08 s** (vs 24% snares-off). Short, dry, gated-sounding.
- **Transient:** a bright stick **CRACK** in the 6–10 kHz contact band → spec's crack click **0.88**.
- **Pitch glide:** minimal (just the small head crack); no sustained sub glide.

## 2. Mapping → Membrum

| Param | Norm | Physical |
|---|---|---|
| Exciter Type | 0.40 | **NoiseBurst** (broadband snare excitation) |
| Body Model | 0.00 | **Membrane** (struck drumhead, single layer) |
| Material | 0.34 | woody/dark body, base decay ~0.24 s |
| Size | 0.40 | f0 ≈ **199 Hz** body |
| Decay | 0.33 | body ring ~0.22 s |
| Strike Position | 0.35 | r/a 0.315 (off-center) |
| Level | 0.92 | near-unity linear gain |
| **PitchEnv Start** | **0.5396** | **240 Hz** |
| **PitchEnv End** | **0.4647** | **170 Hz** |
| PitchEnv Time | 0.11 | 55 ms glide (env ON) |
| PitchEnv Curve | 0.15 | exp drop (−0.7) |
| Drive Amount | 0.12 | mild grit (internalDrive 2.08), flavour not level |
| **Noise Mix** | **0.82** | dominant wire/head noise |
| Noise Cutoff | 0.82 | ~6 kHz LP (wire band) |
| Noise Resonance | 0.15 | Q ~1.0 |
| Noise Decay | 0.20 | ~49 ms (fast, tight) |
| Noise Color | 0.65 | White (bright broadband) |
| **Click Mix** | **0.88** | bright stick crack |
| Click Contact | 0.30 | 2.9 ms |
| Click Brightness | 0.85 | ~7.6 kHz (6–10 kHz contact) |
| Body Damping b1 | 0.28 | ~14 s⁻¹ (tight floor) |
| Body Damping b3 | 0.18 | mild HF damping (wood) |
| Air Loading | 0.55 | air-loaded head (deeper, less whistly) |
| Filter Cutoff | 1.00 | **bypassed** (clean) |
| Pan | 0.50 | center |

**Physics OFF (per spec):** Mode Scatter 0, Nonlinear Coupling 0, Mode Inject 0, Mode Stretch neutral (0.3333), Decay Skew neutral (0.5), **Secondary Enabled 0 / Coupling Strength 0** (no secondary metallic body), Tension Mod 0 (the explicit pitchEnv covers the head crack).

### Pitch-env math (denorm `hz = 20·100^norm`)
- 240 Hz → `ln(12)/ln(100) = 0.5396`
- 170 Hz → `ln(8.5)/ln(100) = 0.4647`

## 3. Deliberately defaulted

Filter Type/Resonance/Env-ADSR (filter bypassed), Fold (alien to acoustic snare), PitchEnv Knee/Mid/MidFrac/Curve2 (single-segment glide), Morph (no material sweep), FM Ratio / Feedback / Friction Pressure (wrong-exciter no-ops), NoiseBurst Duration (default ~8.5 ms; click layer supplies the sharp tick), Secondary Size/Material (bank off), Coupling Amount + all 5 macros (neutral — params set directly).

---

### How this differs from the 808/909 snare voicing in the same generator
The 808/909 snares in this codebase use **two body sources + a long noise tail** to fake the dual-oscillator metallic ring. This LinnDrum snare is deliberately **single-layer**: one woody Membrane body, **no secondary shell**, a **short** noise tail (~49 ms), and a small head-crack pitch env — exactly the thinner, drier, gated sample-machine character.