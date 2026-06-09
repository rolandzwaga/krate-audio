# Membrum Recipe — Vintage Wood-Shell Kick

**Archetype:** warm, low-tuned acoustic bass drum with a darker wood-shell color and light vintage wavefold saturation. Distinguished from a brighter maple/rock kick by (a) low secondary-shell material (darker shell modes), (b) light wavefolding adding vintage harmonics, and (c) strong HF body damping (wood "thump", not metallic ring).

**Body:** Membrane (air-loaded Bessel drumhead) · **Exciter:** Mallet (soft beater)

---

## Physics summary (cited)

- **Fundamental:** acoustic kicks read 40–80 Hz; a tuned 22" measured F0=67 Hz, F1=94.5 Hz (overtone ratio ~1.4–1.6) [idrumtune, modeaudio]. We voice the steady pitch to **55 Hz** via the pitch env (natural body f0 at size 0.78 is 83 Hz; pitchEnv overrides it).
- **Modes / inharmonicity:** circular membrane is strongly inharmonic (1 : 1.59 : 2.14 : 2.30…); two-head **enclosed-air coupling depresses the low modes** and splits them into parallel/antiparallel pairs → deep felt-pitch thump [circularscience; Fletcher & Rossing; Rossing JASA 85:S33]. Realized via **airLoading 0.70** (membrane-only Bessel→air-loaded morph).
- **Decay (T60):** deliberately damped, short punchy ring; lows longest, highs gone first. **b1=17.1 s⁻¹ (T60≈0.40 s)**, **b3=1e-4** for fast f² HF roll-off [idrumtune; musicskanner].
- **Attack click:** beater energy ~2–5 kHz; felt/wood beater = **warm/dark** click → click brightness ≈600 Hz, contact 2.75 ms [musicguymixing; homestudioguys].
- **Pitch glide:** strike raises head tension → pitch lifts then relaxes [circularscience]. Two layers: ToneShaper **150→55 Hz over 22.5 ms** + **tensionModAmt 0.18** (energy-driven micro-glide, Membrane-only).
- **Vintage saturation:** wood shells are warm/low-mid focused [musicskanner; janzenbrothers]; wavefolding adds harmonic layers [perfectcircuit; attackmagazine]. **Fold 0.10 (0.314 rad)** + gentle **Drive 0.20** (unity-makeup flavour) + **secondaryMaterial 0.30** dark shell.

---

## Key normalized baseline (physical target)

| Param | Norm | Physical |
|---|---|---|
| Exciter | 0.20 | Mallet |
| Body | 0.00 | Membrane |
| Material | 0.28 | dark/woody brightness |
| Size | 0.78 | f0 83 Hz natural (→55 Hz via env) |
| Decay | 0.27 | short (b1 override governs RT60) |
| Strike Pos | 0.30 | r/a 0.27 off-center |
| Level | 0.82 | linear pre-limiter |
| Drive | 0.20 | internalDrive 2.8, mild warmth |
| **Fold** | **0.10** | **0.314 rad light fold** |
| PitchEnv Start | 0.4376 | 150 Hz |
| PitchEnv End | 0.2197 | 55 Hz |
| PitchEnv Time | 0.045 | 22.5 ms |
| PitchEnv Curve | 0.15 | fast-initial (exp) drop |
| Decay Skew | 0.55 | +0.1 low-favoring |
| Body b1 | 0.34 | 17.1 s⁻¹ (T60≈0.40 s) |
| Body b3 | 0.10 | 1e-4 (wood HF damping) |
| Air Loading | 0.70 | deep air-loaded membrane |
| Coupling Strength | 0.55 | head→shell drive |
| Secondary Enabled | 1.0 | shell bank on |
| Secondary Size | 0.42 | shell f0 ≈0.685·head |
| **Secondary Material** | **0.30** | **dark woody shell** |
| Tension Mod | 0.18 | strike-tension micro-glide |
| Noise Mix | 0.10 | low, dark, short |
| Click Mix / Bright / Contact | 0.62 / 0.40 / 0.25 | warm beater thud |
| Macros (Bright/BodySize/Punch/Complex) | 0.30 / 0.70 / 0.65 / 0.45 | darker, bigger, punchier, simpler |
| Pan | 0.50 | center |

Left at default (physical reason): ToneShaper filter + filter-env (bypassed, body damping shapes tone); pitch-env knee/mid (single segment suffices); ModeInject & NonlinearCoupling (0 — keep the real membrane spectrum, fold supplies harmonics); ModeScatter (0 — defined pitch); Morph (off); FM/Feedback/NoiseBurst/Friction params (no-op for Mallet); cross-pad Coupling Amount (kit-level, off); Choke/Bus (kick → main, no mute group).

> Note: built against the post-audit corrected chain (linear voice + bus limiter, measured-strike body norm, M-2 Drive unity makeup so Drive=flavour-not-level, fold/secondary-shell active). The existing `vintageWoodKit()` pad[0] is a close match; this recipe leads with the spec'd **fold 0.10** and **secondaryMaterial 0.30** as the vintage/woody differentiators and trims Drive to a gentle 0.20 since fold is now the primary saturation source.