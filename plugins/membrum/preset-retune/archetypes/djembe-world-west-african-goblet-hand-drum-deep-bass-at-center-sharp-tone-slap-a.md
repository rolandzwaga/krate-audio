# Membrum Recipe — Djembe (World)

**Archetype:** West-African goblet hand drum. Deep bass at the *center*, sharp tone/slap at the *rim*. **Strike Position is the master articulation axis.** Provided as a **Bass** voice and a **Slap** variant.

**Body:** Membrane (Bessel circular drumhead, 48 modes) · **Exciter:** Impulse (raised-cosine hand-contact click)

---

## Acoustic basis (cited)

| Property | Real djembe | Source |
|---|---|---|
| Bass | 65–80 Hz, strike **center** w/ full palm → (0,1) mode + goblet Helmholtz ~75 Hz; tension-independent | [Wikipedia](https://en.wikipedia.org/wiki/Djembe) |
| Tone | 300–420 Hz, fingers near edge → (1,1) + low modes | [Wikipedia](https://en.wikipedia.org/wiki/Djembe) |
| Slap | 700–1000 Hz, overtones >4 kHz, rim → (2,1),(0,2),(3,1),(1,2),(0,3)+ ; suppresses bass/(0,1)/(1,1) | [Wikipedia](https://en.wikipedia.org/wiki/Djembe) |
| Head/shell | goatskin membrane on a carved hardwood (lenke) goblet shell; shell adds bell/wineglass modes coupled to the head | [JASA](https://pubs.aip.org/asa/jasa/article/108/5_Supplement/2591/552859/Acoustic-and-modal-analysis-of-an-African-djembe), [ResearchGate modal analysis](https://www.researchgate.net/publication/260571485_Acoustic_and_modal_analysis_of_an_African_djembe_drum) |
| Air loading | depresses low (m,1) modes → near-harmonic 1:1.5:2:2.44:2.9 | Rossing 1982, [pauken.org](https://wtt.pauken.org/chapter-3/air-loading-2) |

**Membrum model note:** Membrum has no literal Helmholtz cavity. The bass is synthesized as a **low membrane f0** (size 0.78 → ~83 Hz) plus a **pitch glide 150→85 Hz** (the attack 'thump' settling onto the cavity band) and a **coupled secondary shell** (the wood goblet). All voiced against the **post-audit corrected** model: measured-strike body norm (N-1), airLoading on Membrane, membrane-gated tension glide, per-pad pan.

---

## BASS voice — normalized baseline

| Param | Norm | Physical target |
|---|---|---|
| Exciter Type | 0.0 | Impulse |
| Body Model | 0.0 | Membrane |
| Material | 0.30 | warm/woody, baseDecay≈0.345 s, brightness 0.30 |
| Size | 0.78 | f0 ≈ 83 Hz (bass band) |
| Decay | 0.45 | ~0.78× base ring (medium-short) |
| Strike Position | 0.50 | r/a 0.45 → center-ish, low modes (bass) |
| Level | 0.85 | hot foundational hit |
| PitchEnv Start / End / Time | 0.4356 / 0.3148 / 0.05 | 150 → 85 Hz over 25 ms |
| PitchEnv Curve | 0.15 | exp (fast initial drop) |
| Air Loading | 0.65 | Rossing low-mode deepening |
| Mode Scatter | 0.18 | organic goatskin detune |
| Body Damping b1 / b3 | 0.30 / 0.10 | ~15 s⁻¹ floor / 1e-4 light HF loss |
| Tension Mod | 0.22 | brief loud-hit pitch lift (≤+3 st), membrane-only |
| Coupling Strength | 0.40 | head→shell feedforward drive |
| Secondary Enabled | 1.0 | shell bank on |
| Secondary Size / Material | 0.50 / 0.30 | shell f0 ≈ 0.625·f0 (~52 Hz), woody |
| Click Mix / Contact / Brightness | 0.40 / 0.22 / 0.40 | dull palm thud ~870 Hz, ~2.7 ms |
| Noise Mix / Cutoff / Color / Decay / Reso | 0.18 / 0.5 / 0.45 / 0.30 / 0.2 | low pink air ~850 Hz LP, ~80 ms |
| Pan | 0.5 | center |
| Macro Body Size | 0.85 | one-knob 'big drum' nudge |

---

## SLAP variant — deltas from Bass

| Param | Norm | Physical target / why |
|---|---|---|
| Strike Position | **0.10** | r/a 0.09 → **rim strike**: excites the high (2,1)/(0,2)/(3,1)… slap mode cluster |
| Decay | **0.20** | ~0.39× base — short sharp crack |
| Click Mix / Brightness / Contact | **0.85 / 0.78 / 0.12** | sharp fingertip tick (~5.6 kHz, ~2 ms) |
| PitchEnv Start / End | **toLogNorm(280) / toLogNorm(220)** | higher, smaller glide (slap reads brighter, no deep boom) |
| Macro Punch | **0.85** | deeper+faster pitch-env drop, shorter contact |

(Everything else inherited from the Bass voice; Material may be nudged up ~0.55 for extra edge brightness.)

---

## Deliberately defaulted

ToneShaper filter/drive/fold (body modes carry the timbre, not a post filter) · PitchEnv knee/mid (single segment suffices) · Mode Stretch / Decay Skew (natural Bessel balance) · Mode Inject / Nonlinear Coupling (acoustic, exact-bypass at 0) · Material Morph (static within a hit) · Choke (rings freely) · FM/Feedback/NoiseBurst/Friction params (no-op for Impulse) · cross-pad Coupling Amount, neutral macros, Output Bus, Pad Enabled.

---

## Maps to `tools/membrum_preset_generator.cpp`

This recipe matches the existing **Djembe bass (pad 10)** and **Djembe slap (pad 12)** in `handDrumsKit()` and confirms them against the corrected post-audit model. Use `toLogNorm(hz)` for the pitch-env Start/End fields (e.g. `toLogNorm(150)`, `toLogNorm(85)`), `Membrane` body, `Impulse` exciter, and the explicit `bodyDampingB1/B3`, `airLoading`, `couplingStrength`/`secondaryEnabled`, and `tensionModAmt` fields as tabled above.