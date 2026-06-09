# Membrum Recipe — "Mbira" (mallet), graded across 4 pitched tines

**Body:** String (WaveguideString) · **Exciter:** Mallet · **Pads:** 4 ascending tines (e.g. pads 8–11)

> Voiced against the CORRECTED post-audit (2026-06-07) signal path: linear voice + measured-strike body norm, env-level NonlinearCoupling, per-pad pan, Drive = flavour. All values are NORMALIZED [0,1] (on-wire / preset representation).

## Why String (and the honest caveat)
The mbira tine is acoustically a **clamped-free cantilever bar** — its physically-exact Membrum analogue is **Bell/Shell**, which is exactly how the companion *kalimba* is voiced in `worldMetalKit`. This recipe deliberately uses the **String/waveguide** body to get a **more sustained, slightly buzzy plucked-tine tone** that contrasts the kalimba's bell. The accepted cost: a waveguide is **quasi-harmonic (1:2:3)** and therefore cannot reproduce the tine's true **inharmonic 1:5:14** spectrum (ideal bar 1 : 6.267 : 17.55). The inharmonic attack colour is approximated by the Click tick + a little NonlinearCoupling, not by real bar modes.

**On String these params are NO-OPs** (the `string_mapper` consumes only size, material, decay, strikePos): `modeStretch`, `decaySkew`, `modeScatter`, `bodyDampingB1/B3`, `airLoading`, `tensionMod`. The brief's `modeStretch 0.42` / `decaySkew` would be stored-but-inert — left at neutral here.

## Acoustic profile (researched)
- **Pitch:** 22–28 forged-metal tines, 3–3.5 octaves; these 4 pads sit in the playable mid register **383 / 420 / 460 / 505 Hz** (size 0.32/0.28/0.24/0.20 via f0 = 800·0.1^size).
- **Partials:** strongly **inharmonic**; prominent overtones measured at **~5× and ~14×** the fundamental (King & Bilbao, JASA 2008), lowered from the ideal cantilever-bar 6.27×/17.55× by tip geometry. Inharmonics are **strongest in the attack and die quickly**, leaving an almost pure, chimelike fundamental.
- **Decay:** clear, sustaining; long body decay (norm **0.78**) → ~4.4–7.0 s nominal waveguide ring (perceptually trimmed by loop-filter brightness).
- **Attack:** short bright thumb-nail tick (Click ≈2.4 ms, ~1.9 kHz).
- **Buzz (machachara):** broadband bright metallic rattle from bottle caps/shells — *essential* texture; modelled by the parallel Noise layer at a modest mix.
- **No pitch glide** (a tine has no tension "kerthump").

## Baseline values (per pad, normalized)
| Param (offset) | Norm | Physical target |
|---|---|---|
| Exciter (0) | 0.20 | Mallet (soft beater) |
| Body (1) | 0.60 | String |
| Material (2) | **0.50 / 0.62 / 0.72 / 0.82** | loop-filter brightness 1−material; decay base 2.25→3.56 s |
| Size (3) | **0.32 / 0.28 / 0.24 / 0.20** | f0 383 / 420 / 460 / 505 Hz |
| Decay (4) | 0.78 | ×1.96 on base → long singing ring |
| Strike Pos (5) | 0.85 | pick near free tip (bright) |
| Level (6) | 0.70 | quiet, intimate |
| NonlinearCoupling (24) | 0.18 | louder pluck = brighter attack, relaxes on decay |
| Noise Mix (42) | 0.12 | quiet machachara buzz |
| Noise Cutoff (43) | 0.72 | ~3.4 kHz LP |
| Noise Reso (44) | 0.15 | Q≈1.0 |
| Noise Decay (45) | 0.35 | ~108 ms |
| Noise Color (46) | 0.78 | White |
| Click Mix (47) | 0.32 | nail tick |
| Click Contact (48) | 0.14 | ~2.4 ms |
| Click Bright (49) | 0.70 | ~1.9 kHz |
| PitchEnv Time (15) | 0.00 | pitch env OFF |
| Tension Mod (58) | 0.00 | off (Membrane-only anyway) |
| Drive (11) | 0.00 | bypassed |
| Filter Cutoff (8) | 1.00 | SVF bypassed |
| Pan (64) | 0.50 | centre |
| Enabled (59) | 1.00 | on |

**Grading rationale:** ascending pitch (shorter tine = smaller size = higher f0); material rises with pitch so higher/shorter tines have slightly darker top-partial damping = shorter top sustain, matching real tine behaviour.

## Left at default (per-pad coverage)
Filter Type/Reso/Env (7,9,10,17–20) — SVF bypassed. Fold (12) — n/a. PitchEnv Start/End/Curve/Knee/Mid (13,14,16,60–63) — pitch env disabled. Morph (25–29) — static tone. Choke (30)=0 free ring. Bus (31)=0 main. FM/Feedback/NoiseBurst/Friction params (32–35) — wrong exciter, inert. Coupling Amount (36)=0.5, all Macros (37–41)=0.5 neutral. Body Damping b1/b3 (50,51), Air Loading (52), Mode Scatter (53) — **inert on String**. Secondary/Coupling Strength (54–57) — off (no coupled shell on a single tine). Mode Stretch (21)/Decay Skew (22) — **inert on String**, left neutral.

## Sources
- Mbira — Wikipedia: https://en.wikipedia.org/wiki/Mbira
- King & Bilbao, *Vibrational frequencies and tuning of the African mbira*, JASA 2008: https://pubmed.ncbi.nlm.nih.gov/18247916/ · https://www.researchgate.net/publication/5604142_Vibrational_frequencies_and_tuning_of_the_African_mbira
- BYU Acoustics, clamped-free bar overtones (1 : 6.267 : 17.55): https://acoustics.byu.edu/animations-bar-trans-clamped-free
- Mbira/machachara buzzing: https://instrumentheritage.com/mbira-kalimba-history/ · https://soundgenetics.com/guide-to-the-mbira/
- Lamellophone (transverse cantilever modes): https://grokipedia.com/page/Lamellophone
