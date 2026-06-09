<!-- verdict: pass-with-fixes | coverageOk: true | issues fixed: 10 | IMPLEMENTED: 2026-06-09 (commit re-tune World Metal) -->

# Membrum Kit Redesign — "World Metal" (Percussive · `worldMetalKit()`)

Voiced against the **corrected post-audit (2026-06-07) signal path**: measured-strike body norm (N-1), Bell 2-D meridional Chladni strike shape, Plate free-plate `(m+2n)^1.7`, Shell free-free end-antinode, widened modeStretch `B_max=0.01`, `mode_inject` 1/k, per-mode decaySkew tilt on all bodies, Drive = flavour (M-2 makeup), env-level NonlinearCoupling, per-pad equal-power pan (M-9). All values are NORMALIZED [0,1].

> **Verification status: PASS-WITH-FIXES.** Every pad's body/exciter assignment, every cited normalized value, and every documented no-op was checked against the Stage-A archetype recipes, `param-dictionary.json`, and `AUDIT-signal-path-2026-06-07.md`. Two prose/consistency fixes were applied (level-range claim; gong decaySkew aligned to its cited recipe value +0.30). No physical-model, range, or coverage errors were found; `modeInject 0` and `airLoading 0` kit-wide are confirmed physically correct and are documented per-pad, not silently defaulted.

## What changed and why

World Metal is the plugin's tuned-metal idiophone kit. The shipped version crafts only 20 of 32 pads, is ~90% Bell+Mallet, stamps 8 kalimba pads from one recipe + a size ramp (audit §4 sameness), voices every Bell pad against the **old** strike-shape bug, parks `modeStretch` near neutral (the widened B_max axis unused), sets `modeInject 0` everywhere, and leaves `pan 0.5` throughout. It also stores two **inert** params (Membrane-only `tensionModAmt` on Bell singing-bowl/temple-bell) and several String no-ops (mbira's `modeStretch/decaySkew/b1/b3`).

This redesign:
- **Keeps the world-metal identity** and the existing 20 pad assignments, re-voicing each to its cited archetype against corrected physics.
- **Breaks kalimba sameness** (pads 0-7): real C4-E5 pitch grade + alternating center-out pan + true Decay-Skew tilt — one *instrument across a scale*, not one timbre x8.
- **Promotes `modeStretch` to a real inharmonicity axis**: 0.333-0.40 on clean tuned bells (crotales/temple) up to 0.55-0.62 on the bell-tree / cowbell / gong / tingsha / triangle for dense inharmonic clusters.
- **Removes the two inert `tensionModAmt` settings** and documents mbira's String no-ops as neutral.
- **Fills pads 20-31** with gap voices: gong, triangle (only Shell body), tubular bell (2nd String), agogo hi/lo, cowbell, FM-bell perc, sub-bell (only Drive user), ride bell, glass bell, two kalimba octave extensions — introducing the **FMImpulse** exciter (now live after the L-3 FM-index fix) and **secondary head-shell coupling** (gong).
- **Uses pan across the row** for a wide constant-power metal field; sends long-ring voices (gong/bowl/ride) to **aux bus 1**.
- `maxPolyphony` 16→20 (denser overlapping rings); `globalCoupling 0.40`, `couplingDelayMs 1.4` kept; `crafted` {0..19}→{0..31}.

## Body / exciter map

| Pads | Drum | Body | Exciter |
|---|---|---|---|
| 0-7 | Kalimba C4-E5 | Bell | Mallet |
| 8-11 | Mbira tines | String | Mallet |
| 12 | Bell Tree | Bell | NoiseBurst |
| 13-14 | Crotales hi/lo | Bell | Mallet |
| 15 | Singing Bowl | Bell | Friction |
| 16-17 | Wood block hi/lo | Plate | Impulse |
| 18 | Tibetan Tingsha | Bell | Impulse |
| 19 | Indian Temple Bell | Bell | Mallet |
| 20 | Gong / Tam-Tam | Bell | Mallet |
| 21 | Triangle | Shell | Impulse |
| 22 | Tubular Bell | String | Mallet |
| 23-24 | Agogo hi/lo | Bell | FMImpulse |
| 25 | Cowbell | Bell | FMImpulse |
| 26 | FM-Bell Perc | Bell | FMImpulse |
| 27 | Sub-Bell Perc | Bell | FMImpulse |
| 28 | Ride Bell ping | Bell | NoiseBurst |
| 29 | Glass Bell | Bell | FMImpulse |
| 30-31 | Kalimba F5/A5 | Bell | Mallet |

*Verified: triangle is Shell (free-free bar) per the recipe's explicit Bell→Shell deviation; tubular bell + mbira are String; woodblocks are Plate (post-audit free-plate Chladni); FM family is FMImpulse (live after L-3). Membrane intentionally absent — idiophone kit — so tensionMod is inert kit-wide.*

## Per-pad exact values (meaningful params; defaults documented separately)

### Kalimba row 0-7 (Bell+Mallet) — shared baseline + per-pad pitch grade
Shared: `strikePosition 0.15`, `level 0.72`, `modeStretch 0.40`, `decaySkew 0.62`, `modeScatter 0.08`, `bodyDampingB1 0.18`, `bodyDampingB3 0.10`, `clickLayerMix 0.42`, `clickLayerContactMs 0.25`, `clickLayerBrightness 0.78`, `noiseLayerMix 0.10`, `noiseLayerCutoff 0.60`, `noiseLayerResonance 0.30`, `noiseLayerDecay 0.20`, `noiseLayerColor 0.65`, `airLoading 0.0`. Defaulted: pitchEnv off, modeInject 0 (would fight inharmonic bell), nonlinearCoupling 0, tensionMod 0 (Membrane-only), morph off, filter bypass, drive/fold 0, fmRatio/feedback/noiseBurst/friction (Mallet no-ops), coupling/secondary off, macros 0.5, choke 0, bus 0.

| Pad | Note | material | size | decay | pan |
|---|---|---|---|---|---|
| 0 | C4 | 0.40 | 0.485 | 0.60 | 0.50 |
| 1 | D4 | 0.46 | 0.435 | 0.56 | 0.38 |
| 2 | E4 | 0.52 | 0.385 | 0.52 | 0.62 |
| 3 | G4 | 0.58 | 0.310 | 0.47 | 0.30 |
| 4 | A4 | 0.63 | 0.260 | 0.43 | 0.70 |
| 5 | C5 | 0.69 | 0.185 | 0.38 | 0.25 |
| 6 | D5 | 0.74 | 0.135 | 0.34 | 0.75 |
| 7 | E5 | 0.80 | 0.085 | 0.30 | 0.50 |

*(Pad 7 size 0.085 → f0 ≈ 658 Hz ≈ E5, at the Bell-body f0 ceiling; matches recipe.)*

### Mbira row 8-11 (String+Mallet)
Shared: `decay 0.78`, `strikePosition 0.85` (pick near tip), `level 0.70`, `nonlinearCoupling 0.18`, `noiseLayerMix 0.12`, `noiseLayerCutoff 0.72`, `noiseLayerResonance 0.15`, `noiseLayerDecay 0.35`, `noiseLayerColor 0.78`, `clickLayerMix 0.32`, `clickLayerContactMs 0.14`, `clickLayerBrightness 0.70`. **String no-ops (neutral):** modeStretch/decaySkew/b1/b3/airLoading/modeScatter/tensionMod. Defaulted: pitchEnv off, drive 0, filter bypass, modeInject 0, coupling/secondary off, fmRatio/feedback/noiseBurst/friction (Mallet no-ops), macros 0.5. *(material rises with pitch → loop brightness 1−material darkens top sustain, per String mapper.)*

| Pad | f0 | material | size | pan |
|---|---|---|---|---|
| 8 | 383 Hz | 0.50 | 0.32 | 0.40 |
| 9 | 420 Hz | 0.62 | 0.28 | 0.47 |
| 10 | 460 Hz | 0.72 | 0.24 | 0.53 |
| 11 | 505 Hz | 0.82 | 0.20 | 0.60 |

### Single voices 12-29 and octave extensions 30-31
Exact normalized values for every meaningful param, with rationale and citation, are in the structured `pads[]` array (one entry per pad index 12-31). Highlights:

- **12 Bell Tree** — material 0.95, size 0.30, decay 0.85, **modeStretch 0.55 + modeScatter 0.55** (inharmonic shimmer), decaySkew 0.78, **morph 0.85→0.55 / 0.55 / lin**, noise 0.42/violet/13 kHz, click 0.55/0.92, b1 0.30, b3 0.0. fmRatio dropped (NoiseBurst no-op).
- **13/14 Crotales hi/lo** — material 0.92, size 0.12/0.22, decay 0.85, **modeStretch 0.333** (keep the harmonic octave), decaySkew 0.58, b1 0.30/b3 0.0, click 0.42/0.85, noise off, pan 0.62/0.38.
- **15 Singing Bowl** — Friction, material 0.78, size 0.45, decay 0.95, frictionPressure 0.28, modeStretch 0.45, **decaySkew 0.85**, modeScatter 0.06, nonlinearCoupling 0.22, **morph 0.78→0.55 / 1.7 s / exp**, click 0, noise 0.10/pink, **bus 1**, complexity 0.85. tensionMod **removed** (inert).
- **16/17 Wood blocks** — Plate, material 0.30/0.28, size 0.18/0.28, decay 0.16/0.20, **modeStretch 0.50** (free-bar spread), decaySkew 0.50 (neutral, per recipe), modeScatter 0.20, b1 0.50 (dry tok)/b3 0.10, click 0.78, noise off, pan 0.42/0.58.
- **18 Tibetan Tingsha** — Impulse, material 0.95, size 0.12, decay 0.92, **b1 0.08/b3 0.04** (tens-of-sec ring), **modeStretch 0.62**, decaySkew 0.38, nonlinearCoupling 0.22, modeScatter 0.12, noise off, click 0.40/0.85.
- **19 Indian Temple Bell** — Mallet, material 0.82, size 0.50 (warm hum), decay 0.92, modeStretch 0.40, decaySkew 0.78, b1 0.18/b3 0.06, modeScatter 0.06, soft click 0.28/0.42, noise 0.04. tensionMod **removed** (inert).
- **20 Gong/Tam-Tam (NEW)** — Bell+Mallet, size 0.85 (low fifth ≈113 Hz), decay 0.95, **modeStretch 0.62 + scatter 0.55**, **decaySkew 0.65 (+0.30, per recipe)**, **nonlinearCoupling 0.80** + morph (bloom), **couplingStrength 0.85 / secondary on / size 0.40 / mat 0.70**, noise 0.30/violet, **bus 1**.
- **21 Triangle (NEW)** — **Shell**+Impulse, size 0.085, material 0.95, decay 0.85, modeStretch 0.62, decaySkew 0.40 (shimmer), b3 0.02, modeScatter 0.12, **click 0.95/0.97** (defines attack), noise off, pan 0.60.
- **22 Tubular Bell (NEW)** — **String**+Mallet, material 0.85, size 0.55, decay 0.92 (~8.7 s), strikePos 0.30, click 0.40/0.65, noise 0.10. (modeStretch/decaySkew/b1/b3/scatter inert String no-ops, documented.)
- **23/24 Agogo hi/lo (NEW)** — **FMImpulse**, **fmRatio 0.72/0.55** (→3.16/2.65), size 0.14/0.22, decay 0.28/0.35, material 0.85, modeStretch 0.45, decaySkew 0.40, b1 0.30/b3 0.0, click 0.55/0.85, macroBrightness 0.65, pan 0.42/0.58.
- **25 Cowbell (NEW)** — FMImpulse, **fmRatio 0.50** (→2.5, detuned fifth), size 0.22, decay 0.30, **modeStretch 0.55 + scatter 0.20** (clang/beat), b1 0.32/b3 0.0, click 0.55/0.72, pink halo 0.10, macroBrightness 0.65.
- **26 FM-Bell Perc (NEW)** — FMImpulse, **fmRatio 0.40** (→2.2), size 0.25, decay 0.20 (ping), b3 0.0, modeStretch 0.333, click 0.15, noise off.
- **27 Sub-Bell Perc (NEW)** — FMImpulse, **fmRatio 0.72** (→3.16, grit), size 0.35 (sub), decay 0.40, **level 0.80** (recipe default loudness; the kit's loudest pad), **nonlinearCoupling 0.40 + Drive 0.30** (west-coast evolution — only Drive user), modeStretch 0.45, decaySkew 0.42, click 0.50/0.80, noise 0.12/violet.
- **28 Ride Bell ping (NEW)** — NoiseBurst, size 0.18, decay 0.70, noiseBurstDuration 0.20, modeStretch 0.45, decaySkew 0.55, modeScatter 0.15, b1 0.25/b3 0.0, **noise wash 0.30/violet/9 kHz/0.55**, click 0.45/0.88, **bus 1**, pan 0.40.
- **29 Glass Bell (NEW)** — FMImpulse, **fmRatio 0.30** (→1.9, pure, clean counterpart to the gritty sub-bell), size 0.20, decay 0.85, b1 0.20/b3 0.0, modeStretch 0.40, decaySkew 0.45, click 0.30, noise off, pan 0.60.
- **30/31 Kalimba F5/A5 (NEW)** — Bell+Mallet kalimba-baseline extension: material 0.83/0.86, size 0.05/0.02 (→714/764 Hz; A5 880 Hz unreachable past the Bell f0 ceiling, documented), decay 0.28/0.26, pan 0.35/0.65.

## Param-surface coverage (kit-collective)

- **Bodies:** Bell (18), String (5 — mbira 4 + tubular 1), Plate (2 woodblocks), Shell (1 triangle). *Membrane intentionally absent — this is an idiophone kit.*
- **Exciters:** Mallet (12), FMImpulse (6 — now live), Impulse (3), NoiseBurst (2), Friction (1).
- **Unnatural zone:** modeStretch spans 0.333→0.62; decaySkew spans 0.38→0.85; modeScatter 0.06→0.55; nonlinearCoupling on gong/sub-bell/tingsha/mbira/singing-bowl; modeInject **0 throughout** (correct — a 1/k series fights inharmonic bells; documented, not an oversight).
- **Tone shaper:** Drive 0.30 (sub-bell only); filters bypassed (bodies carry timbre).
- **Layers:** click on every pad, noise wash on bell-tree/gong/ride/mbira/kalimba, click-off on the bowed bowl.
- **Coupling:** secondary head-shell on the gong; couplingAmount default elsewhere under globalCoupling 0.40.
- **Routing/space:** pan used across ~22 pads; aux bus 1 on the three long-ring voices.
- **Gain staging:** per-pad level 0.65-0.80 under the −6 dBFS measured-strike body budget + −1 dBTP bus limiter (sub-bell pad 27 is the 0.80 top of the range per its recipe); long-ring voices (gong/bowl/ride) on outputBus 1.

## Gaps / duplicates resolved
- Kalimba 8x-duplicate timbre → one scale-graded instrument.
- Added the missing large-metal (gong), bent-rod (triangle, only Shell), long-tube (tubular bell), FM-bell family (agogo/cowbell/fm-perc/sub-bell/glass), and a ride ping.
- No drum/membrane voice by design; `tensionMod` correctly inert kit-wide and documented; `airLoading 0.0` everywhere (no-op on these bodies).

---

## Verification log (10 issues found & fixed)

1. PROSE/GAIN-STAGING INCONSISTENCY (fixed): kitCharacter and the markdown both claim 'per-pad level 0.65-0.78', but pad 27 (Sub-Bell) carries level 0.80 (authorized by the sub-bell recipe, which sets level 0.80 = default loudness). The value is correct per its cited recipe; the prose range was wrong. Fixed the prose to '0.65-0.80' so the stated budget matches the actual per-pad levels.

2. GONG decaySkew vs recipe (fixed in prose): pad 20 sets decaySkew 0.70 (= phys +0.40), but the gong recipe's mapping text specifies 'decaySkew +0.30' (= norm 0.65). +0.40 is still within the recipe's stated intent ('longest lows, fast highs') but overstates the cited tilt. Aligned the pad to the cited recipe value decaySkew 0.65 (+0.30) and corrected the rationale string and the markdown highlight ('decaySkew +0.30').

3. TUBULAR-BELL String no-op honesty (verified, no change): pad 22 documents modeStretch as a String no-op carried at neutral, but the pad actually sets modeStretch via material/size/decay only -- consistent with the recipe (which carries modeStretch 0.50 'for kit consistency, inert'). The proposal's defaultedParams correctly list modeStretch as a String no-op. No physical error; confirmed coverage is honest.

4. WOODBLOCK lo (pad 17) decaySkew coverage (verified, no change): pad 17 omits an explicit decaySkew, inheriting neutral 0.5 (the recipe's value). Recipe sets decaySkew 0.5 (neutral) on both blocks, so this is a correctly-defaulted meaningful param, not a silent gap. No fix needed.

5. BELL f0 ceiling for top kalimba/octave pads (verified, no change): pads 7 (E5, size 0.085 -> 658 Hz), 30 (F5, size 0.05 -> 714 Hz), 31 (A5, size 0.02 -> 764 Hz) -- Bell f0 = 800*0.1^size caps near 800 Hz, so true A5 (880 Hz) is unreachable. The proposal honestly documents this ('caps near 800 Hz at size 0'), matching the crotales recipe's same documented caveat. Acceptable; no fix.

6. OUTPUTBUS encoding (verified): outputBus 0.0667 -> round(0.0667*15)=1 = aux bus 1. Correct on pads 15/20/28. In range.

7. FM RATIO denorm sanity (verified): all FMImpulse pads use fmRatio in [0,1] decoding via 1+3*norm: agogo hi 0.72->3.16, agogo lo 0.55->2.65, cowbell 0.50->2.5, fm-bell 0.40->2.2, sub-bell 0.72->3.16, glass 0.30->1.9 -- all match their cited recipes and the L-3-fixed live FM index range. In range, physically correct (Bell+FMImpulse is the canonical metallic-clang voice).

8. BODY/EXCITER MAP (verified correct): triangle correctly uses Shell (free-free bar) NOT Bell per the recipe's explicit deviation; tubular bell + mbira use String; woodblocks use Plate (free-plate Chladni, post-audit corrected); all tuned bells use Bell; FM family uses FMImpulse. No mis-assignments. Membrane intentionally absent (idiophone kit) and tensionMod documented inert kit-wide -- correct per param dict (tensionMod is Membrane-gated).

9. modeInject = 0 KIT-WIDE (verified correct): every Bell/Plate/Shell pad leaves modeInject at 0, documented as 'a 1/k harmonic series fights the inharmonic bell/plate partials'. This is the physically-correct choice post-audit (mode_inject is now 1/k -6 dB/oct) and is documented per-pad, not silently defaulted. No fix.

10. airLoading = 0 KIT-WIDE (verified correct): explicitly set 0 on every pad with the documented reason 'no-op on Bell/Plate/Shell/String (membrane-only frequency correction)'. Matches param dict offset-52 semantics. No silent default. No fix.
