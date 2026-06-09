# Membrum Factory Kit Inventory — Current State (pre-Phase-4 re-voicing)

Source: `tools/membrum_preset_generator.cpp` (Pad struct, `writeKitBlob`, 20 `xxxKit()` builders).
Blob version 3 (57-slot sound array incl. M-9 pan). All values cited are NORMALIZED [0,1].

## Categories (5 kits each, matching `membrum_preset_config.h`)
- **Acoustic**: Acoustic Studio Kit, Jazz Brushes, Rock Big Room, Vintage Wood, Orchestral
- **Electronic**: 808 Electronic Kit, 909 Drum Machine, LinnDrum CR-78, Modular West Coast, Trap Modern
- **Percussive**: Hand Drums, Latin Perc, Tabla, World Metal, Cajon and Frames
- **Unnatural**: Experimental FX Kit, Glass Bell Garden, Drone and Sustain, Chaos Engine, Ghost Bones

## Pad-count / crafted gating
`disableUncraftedPads()` forces `enabled=0` on every pad NOT in `k.crafted`. Effective sounding-pad counts:
| Kit | Crafted pads | Notes |
|-----|-------------|-------|
| Acoustic Studio | 13 | pads 15-31 configured in loops then DISABLED |
| Jazz Brushes | 15 | |
| Rock Big Room | 15 | kit globals set (coupling/snareBuzz/tomRes) |
| Vintage Wood | 16 | |
| Orchestral | 16 | maxPoly 16; headroom cuts predate N-1 |
| 808 Electronic | 13 | 19 FM-bell perc slots configured then disabled |
| 909 | 14 | |
| LinnDrum | 14 | |
| Modular West Coast | 14 | most experimental Electronic |
| Trap Modern | 14 | tom row reuses pad7 as 3rd hat |
| Hand Drums | 12+1 latent | **BUG: crafted lists pad 14 which is never configured -> default Membrane sounds** |
| Latin Perc | 14 | |
| Tabla | 10 | smallest standard kit; tension/decaySkew showcase |
| World Metal | 20 | most populated; almost all Bell+Mallet |
| Cajon and Frames | 13 | |
| Experimental FX | 32 | **ONLY kit using all 32 pads + ONLY kit using modeInject** |
| Glass Bell Garden | 16 | ALL Bell, algorithmic |
| Drone and Sustain | 14 | all decay 0.92 sustains |
| Chaos Engine | 14 | max-everything algorithmic |
| Ghost Bones | 14 | high-modeStretch algorithmic |

## Body/Exciter usage summary
- **Membrane**: kicks, snares, toms, congas/bongos/djembe/tabla/dholak/bodhran/frame drums.
- **Plate**: misc perc, wood blocks, rim shots (909/trap), cajon, FM/inharmonic toms (West Coast, Experimental), drone feedback bodies.
- **Shell**: side sticks, rim shots (rock), Feedback snare (Experimental), drone bodies, chaos.
- **String**: friction drones, tubular bell (orchestral), mbira (World Metal), tanpura (Tabla), ghost bones, chaos.
- **Bell**: cowbells, claves, agogos, triangles, crotales, gongs, kalimba, tingsha, temple bells, glass bells, FM hats (West Coast), chaos/ghost.
- **NoiseBody**: hi-hats, cymbals/crashes, claps, shakers/maracas/cabasa/tambourine/guiro/vibraslap/pandeiro.

## How the current kits map to the audit "presets sound the same" findings
The audit (§1, §4) attributes sameness to gain-staging collapse + buried body + dead modulation axes — all now FIXED in code (H-1/2/4, H-3/M-1, M-2/3/4/5, N-1, M-6/8/9, Plate/Shell/Bell physics, Stretch, mode_inject). The PRESETS were voiced against the BROKEN chain and have NOT been re-tuned. Cross-cutting weaknesses observed in the current generator:

1. **Tom rows = one recipe size-swept.** Almost every drum kit builds its 6 toms in a `for` loop that stamps ONE Membrane/Mallet(or Impulse) recipe and ramps only size/material/decay/pitchEnv/b1. This is audit §4 #3 directly — a single timbre with a size sweep, not 6 distinct drums.

2. **Cymbal/hat banks = clone+detune.** Hats (closed/pedal/open) and cymbals (crash/ride/splash) are built by `pads[8]=pads[6]` clone-then-tweak. NoiseBody dominates; the body knobs barely matter on NoiseBody.

3. **Metallic kits stamp ONE Bell skeleton.** World Metal (8 kalimba + bells) and Glass Bell Garden (16 all-Bell) ramp material/size/fmRatio over a fixed bell partial topology — audit §B/§4 #4: the inharmonic fingerprint that separates one bell from another is not in the parameter space. ALL voiced against the OLD (wrong) bell strike shape.

4. **modeInjectAmount is 0 in 19 of 20 kits.** Only Experimental FX uses it (0.2). The 1/k injection fix (audit §B) is essentially unused across the whole library — a free variety axis left on the table, especially for metallic/unnatural kits.

5. **modeStretch sits at/near default (0.333=physical) almost everywhere** except the Unnatural kits (Ghost Bones 0.65-0.89, Chaos 0.40+). The corrected (k+1) index + widened B_max=0.01 mean the few kits that DO set stretch (Ghost Bones especially) are now FAR more inharmonic than intended and must be re-checked.

6. **decaySkew is default (0.5=flat) on most drum pads.** The M-5 per-mode-tilt fix (now on all bodies) is exploited only by Tabla, World Metal, and the Unnatural kits.

7. **pan = 0.5 (center) on literally every pad in all 20 kits.** No kit sets the M-9 per-pad pan — the restored stereo image is completely unused. Perc/bell kits would benefit most.

8. **Heavy reliance on noise+click over body on snares/hats** (snare clickMix 0.85-0.95 + noiseMix 0.62-0.98). This was the H-2 'noisy thwack masks body' pattern; the recent redesigns ADD shell coupling underneath, but the noise/click are still the dominant perceptual layer on most snares.

## Re-voicing priority (highest first), driven by which fixed stages a kit leans on
1. **Chaos Engine** — maxes nonlinearCoupling/tension/drive/fold + all bodies; leans hardest on every redesigned stage; likely now over the rail (max values predate N-1).
2. **Ghost Bones** — high modeStretch set against OLD off-by-one/B_max=0.001; now dramatically more inharmonic.
3. **Modular West Coast / Experimental FX / Drone and Sustain** — heavy nonlinearCoupling + Feedback exciter (M-6 secondary shell + tension restored) + Plate/Shell/Bell shapes corrected.
4. **Glass Bell Garden / World Metal / Latin Perc / Orchestral** — Bell-heavy, voiced against the OLD bell strike shape (Phase 2 fix).
5. **All Acoustic snares/kicks** — voiced against the clipped/buried chain (gain-staging + N-1); levels/airLoading/secondary cuts in Orchestral explicitly compensate for the OLD clip and should be relaxed.

## Notable structural bugs found
- **Hand Drums**: `k.crafted` includes pad index 14, but pad 14 is never configured in the builder, so it ships an audible DEFAULT Membrane voice.
- Several kits configure 17-19 pads in loops (cymbals, FM-bell perc) that are then disabled — wasted builder code, harmless but misleading.
