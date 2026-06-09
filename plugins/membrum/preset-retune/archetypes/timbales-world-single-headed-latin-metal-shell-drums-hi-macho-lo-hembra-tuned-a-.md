# Membrum Recipe — Timbales (world)

Single-headed Latin **metal-shell** drums played with sticks: bright, cutting tone with a metallic **cáscara shell ring**. The **hi (macho)** and **lo (hembra)** are the *same instrument tuned apart* (typ. a perfect 4th–5th). Implementation target: `latinPercKit()` pads **12 (hi)** and **14 (lo)** in `tools/membrum_preset_generator.cpp`.

## Archetype mapping
- **Exciter:** NoiseBurst (violet-noise bandpass burst → short bright stick-on-head contact)
- **Body:** Membrane (Bessel drumhead) for the struck head
- **Secondary (shell) bank ENABLED:** the metallic shell ring — `secondarySize 0.40–0.50`, `secondaryMaterial 0.65` (per brief)
- **Pitch-env glide:** hi 380→280 Hz, lo 280→200 Hz (20 ms)
- **Click + light noise** for the bright attack tick

## Acoustic basis
- **Pitch:** single-headed metal-shell drums; hi/lo tuned ~a 4th–5th apart (CongaChops). Measured energy peaks ≈ 250 Hz and just below 400 Hz with many overtones (HomeRecording). Voiced 380→280 (hi) / 280→200 (lo).
- **Modes:** struck circular head → Bessel series 1 : 1.59 : 2.14 … (Fletcher & Rossing). The **steel/aluminium shell** adds a bright free-free metallic ring → Membrum **Secondary (shell) bank** (free-free beam ratios) at ~0.70·head-f0.
- **Decay:** steel = bright/fast/cutting, aluminium = drier (DrumCenterNH). Short tight ring; players damp the shell with the stick.
- **Attack:** hard stick on plastic head + metal rim/shell → sharp bright tick (Click layer + NoiseBurst contact).
- **Noise:** modest bright broadband stick-contact hiss (white), low level — not a snare sizzle.
- **Air loading:** only PARTIAL (0.40) — one open head on a shallow shell is lightly air-loaded vs a deep closed timpani kettle.

## Baseline values (HI drum, macho) — all NORMALIZED [0,1]

| Param | Norm | Physical | Why |
|---|---|---|---|
| Exciter Type | 0.40 | NoiseBurst | stick-on-head filtered-noise burst |
| Body Model | 0.0 | Membrane | tensioned circular head |
| Material | 0.55 | bright head | bright with metallic edge |
| Size | 0.40 | f0 199 Hz nat (pitch-env overrides) | small hi drum |
| Decay | 0.30 | ~0.55× base, short | bright fast metal decay |
| Strike Position | 0.30 | r/a 0.27 | near-edge cutting strike |
| Level | 0.82 | linear | loud projecting drum |
| PitchEnv Start | 0.639 | 380 Hz | hi start pitch |
| PitchEnv End | 0.573 | 280 Hz | settle pitch |
| PitchEnv Time | 0.04 | 20 ms (enables) | fast tension drop |
| PitchEnv Curve | 0.15 | −0.7 (fast drop) | exp tension relax |
| Air Loading | 0.40 | partial low-mode depress | single open head |
| Mode Scatter | 0.18 | ~3% dither | natural variation |
| Tension Mod | 0.22 | ~+0.5 st energy glide | velocity micro-glide (membrane-only) |
| Secondary Enabled | 1.0 | shell bank on | metallic cáscara ring |
| Coupling Strength | 0.50 | shell drive 0.5 | audible shell, balanced |
| Secondary Size | 0.40 | shell f0 = 0.70·head | metal ring below head |
| Secondary Material | 0.65 | bright/long metal | steel/aluminium shell |
| Body Damping b1 | 0.32 | ~16 s⁻¹ tight | articulate decay floor |
| Body Damping b3 | 0.10 | mild f² damping | keep metal highs |
| Click Mix | 0.65 | bright tick | sharp attack |
| Click Contact | 0.12 | 2.4 ms | hard short stick |
| Click Brightness | 0.78 | ~3.9 kHz BP | crisp metal tick |
| Noise Mix | 0.30 | low support | contact hiss |
| Noise Color | 0.65 | White | bright broadband |
| NoiseBurst Duration | 0.25 | ~5.25 ms | snappy burst |
| Mode Stretch | 0.333 | 1.0 unstretched | head is physical (shell carries inharmonicity) |
| Decay Skew | 0.50 | 0.0 | no tilt |
| Pan | 0.45 | slightly L | two-drum image |
| Filter Cutoff | 1.0 | bypass | no post-filter |

## LO drum (hembra) — deltas from hi
Copy the hi pad, then:
- **Size 0.55** (f0 141 Hz nat — lower/bigger drum)
- **Decay 0.40** (slightly longer)
- **Material 0.50** (a touch darker)
- **PitchEnv Start 0.573 → 280 Hz**, **PitchEnv End 0.5 → 200 Hz**
- **Secondary Size 0.50** (shell f0 = 0.625·head — bigger shell)
- **Pan 0.55** (slightly right, opposite the hi)

Hi/lo together span a ~5-semitone (perfect-4th) interval (380→280), matching real macho/hembra tuning.

## Left at default (with reason)
- **ToneShaper filter + filter-env:** bypassed (cutoff 20 kHz, env 0) — head+shell define the timbre.
- **Drive / Fold:** 0 (exact dry) — clean acoustic tone.
- **PitchEnv Knee/Mid/Curve2:** off — single small glide segment is enough.
- **Mode Inject:** 0 — shell bank already supplies metallic inharmonics.
- **Nonlinear Coupling:** 0 — keep the clean tick; no amplitude brightening needed.
- **Morph:** off — static material over the short note.
- **Choke Group:** 0 — hi/lo ring independently.
- **Output Bus:** 0 (main).
- **FM Ratio / Feedback / Friction Pressure:** no-ops for NoiseBurst.
- **Coupling Amount + 5 macros:** neutral 0.5 — explicit params already voice it.
- **Noise Cutoff/Reso/Decay:** neutral — only noise color is load-bearing at this low mix.
- **Pad Enabled:** 1.0 (on).

## Sources
- CongaChops — timbale tuning intervals/pitches: https://www.congachops.com/blog-articles/how-to-tune-the-timbales
- HomeRecording — timbale energy peaks (~250 Hz, <400 Hz): https://homerecording.com/bbs/threads/eq-info-for-latin-percussion-like-timbales.399387/
- Wikipedia — Timbales (construction, cáscara): https://en.wikipedia.org/wiki/Timbales
- RareInstrument — bright attack / cutting tone: https://rareinstrument.com/timbales/
- DrumCenterNH — steel vs aluminium shell tone/decay: https://drumcenternh.com/collections/timbales
- Wikipedia — Rimshot (near-edge bright strike): https://en.wikipedia.org/wiki/Rimshot
- CCRMA percussion synthesis; Nathan Ho modal synthesis; SOS synthesizing percussion (modal/noise method).