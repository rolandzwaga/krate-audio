<!-- verdict: pass-with-fixes | coverageOk: false | issues fixed: 6 | IMPLEMENTED: 2026-06-09 (commit re-tune Experimental FX Kit) | NOTE: FX region (chaos cycle + drones + pings + clap) reconstructed from cited archetypes + sibling kits (no full per-pad table in plan). Pads 26/28 filled as extra chaos voices to keep all 32 live. -->

# Membrum Kit Re-Design — "Experimental FX Kit" (Unnatural · `experimentalKit()`)

Voiced entirely against the **post-audit corrected** chain (measured-strike body norm + linear Level [N-1], free-plate Chladni Plate / free-free Shell / 2-D Bell shapes [Phase-2], widened 1-indexed modeStretch B_max, env-level NonlinearCoupling [M-3/M-4], unity-makeup Drive [M-2], per-mode decaySkew on all bodies [M-5], pitch-env on all bodies [H-3], Membrane-only tensionMod [N-1], per-pad pan [M-9], modeInject 1/k).

> **Verification pass applied (pass-with-fixes).** Body/exciter assignments, post-audit semantics, ranges, and layout all verified correct. Two coverage gaps were closed: pad 0 (kick) and pad 13 (crash) had meaningful params silently at default; they are now set per their cited archetypes (see "Fixes folded in" below).

## Concept
The synthetic sound-design flagship of the Unnatural category — no acoustic referent, "controlled instability under the −1 dBTP bus limiter." It keeps the current kit's three identifying strengths (all 32 pads live, widest body/exciter palette, the ONLY kit using **modeInject**) but replaces the homogeneous 20-pad algorithmic FX cycle with individually-crafted archetypes and adds the texture voices an FX kit should have (drones, clap, aux-bus cymbals).

## Layout (GM anchors kept, FX region re-crafted)
| Pad | Drum | Body | Exciter | Archetype |
|----:|------|------|---------|-----------|
| 0 | FM/Chaotic Kick | Membrane | FMImpulse | FM/Chaotic Synthetic Kick |
| 2 | Feedback-Shell Snare | Shell | Feedback | Feedback Shell Snare |
| 4 | FM-Plate Snare (sister) | Plate | FMImpulse | FM Plate Snare |
| 6/8/10 | Metal Hat C/P/O | Bell | NoiseBurst | Metal-Bell Hi-Hat (Decay-only variants, choke 1) |
| 5/7/9/11/12/14 | Inharmonic Plate Toms 1-6 | Plate | Mallet | Inharmonic Plate Tom (size-graded, pitch-env kerthump) |
| 13 | FM-Bell Crash | Bell | FMImpulse | FM-Bell Crash (aux 1) |
| 15-24 | Chaos cycle ×10 | Plate/Shell/String/Bell/Membrane/NoiseBody | Feedback/FMImpulse/Friction | Chaos Voice / Algorithmic FX (modeInject 0.2) |
| 3 | Friction String Drone | String | Friction | Friction String Drone (aux 1) |
| 25 | Feedback Plate Drone | Plate | Feedback | Feedback Plate/Shell Drone (aux 1) |
| 27 | Feedback Shell Drone | Shell | Feedback | Feedback Plate/Shell Drone — Shell variant (aux 2) |
| 29 | Ghost Tone | Bell | Mallet | Ghost Tone high-stretch skeletal (aux 1) |
| 1 / 31 | Glass-Bell pings | Bell | FMImpulse / Mallet | Glass Bell (modeInject 0.2, aux 1) |
| 30 | Clap | NoiseBody | NoiseBurst | Clap |

**Gaps/dupes flagged:** the current kit's 20-pad FX cycle was effectively one voice × i-ramps (duplicate); no clap, no drones, no aux-bus use (missing for an FX kit). Both resolved. Impulse is the one exciter not used (documented — its hard-click role is covered by the Click layer on toms/snares).

## Fixes folded in (verification pass)
- **Pad 0 (FM Kick):** `Strike Position 0.25` is now SET (was silently default 0.3). Near-center strike weights the lowest Bessel (0,1) mode → kick sub weight; meaningful on every body. Closes a coverage gap.
- **Pad 13 (FM-Bell Crash):** four archetype-load-bearing params that were silently defaulted are now SET to their cited values — `Strike Position 0.32` (azimuth 0.50 rad, uneven clangy partial balance), `Body Damping b1 0.30` (15.1 s⁻¹ long metallic ring floor), `Click Contact 0.30` (~2.9 ms crisp impact), `Noise Resonance 0.20` (Q≈1.24, no pitched noise peak). Source: fm-bell-crash archetype table.
- **Pad 2 (Feedback-Shell Snare):** `Strike Position` is now explicitly documented as an intentional default (0.3) in `defaultedParams` rather than a silent omission (the archetype table does not treat it as load-bearing; an edge-ish strike already lights the inharmonic free-free bar bank).

## Per-pad exact values
See the structured `pads` array for every meaningful param with EXACT normalized value, rationale, and citation, plus the `defaultedParams` (with one-line reasons) for each pad. Headline anchors:

### Drums (GM anchors)
- **Pad 0 FM Kick** — Membrane Size 0.85 (f0~71 Hz), **Strike Position 0.25 (near-center, sub weight)**, fmRatio 0.60 (c:m 2.8), pitch 300→30 Hz/75 ms exp, modeScatter 0.30, **tensionMod 0.50 (LIVE — Membrane)**, airLoading 0.40 (LIVE — Membrane), coupled metallic shell (sec size 0.60/mat 0.85), Drive 0.30, NLC 0.35, b1 0.18/b3 0.25, brown-noise air 0.25.
- **Pad 2 Feedback-Shell Snare** — Shell Size 0.55 (f0~423 Hz), feedback 0.40, b1 0.28/b3 0.04 (metallic), noise 0.70 White, click 0.85, 230→160 Hz chirp, LP filter env +0.4/114 ms; NLC 0.22 + Drive 0.30 re-voiced to corrected stages. Strike Position default 0.3 (documented).
- **Pad 4 FM-Plate Snare** — Plate Size 0.55 (f0~226 Hz), fmRatio 0.55, modeStretch 0.55, morph 0.55→0.80, noise 0.60, click 0.85, pitchEnv 230→160/60 ms.
- **Pads 6/8/10 Metal Hats** — Bell Size 0.10 (f0~635 Hz), morph 0.95→0.30/200 ms, b3 0, violet noise 0.55, **choke 1**; Closed/Pedal/Open differ ONLY by Decay 0.08/0.04/0.60 (+ open Noise Decay 0.55).
- **Toms 5/7/9/11/12/14** — Plate Size 0.85→0.35, Strike Position 0.62, modeStretch 0.50, decaySkew ~0.42, NLC 0.30, **per-Plate pitch-env kerthump** (Start>End, Time 45 ms) graded up the row, coupled metallic shell ramped, Fold 0.18 + Drive 0.12 flavour. tensionMod removed (no-op on Plate).
- **Pad 13 FM-Bell Crash** — Bell Size 0.42, Material 0.92, **Strike Position 0.32**, fmRatio 0.45, modeStretch 0.45, decaySkew 0.65, **b1 0.30**, b3 0, **Noise Resonance 0.20**, violet shimmer noise 0.50, click 0.30 / **contact 0.30** / bright 0.85, aux 1.

### FX region
- **Chaos cycle 15-24** — body i%6 (Plate/Shell/String/Bell/Membrane/NoiseBody) × exciter i%3 (Feedback/FMImpulse/Friction), filter LP/HP/BP engaged (Q~6), modeStretch 0.6→0.88, modeScatter 0.6, decaySkew 0.55, **modeInject 0.2 (signature)**, NLC 0.75→0.85, Fold 0.35→0.50, Drive 0.45, Level 0.72; choke group 2 + bus/pan spread; the Membrane pad (19) gets tensionMod 0.80 (live) + airLoading 0 (deliberate, anti-whistly).
- **Drones 3/25/27/29** — Friction-String (T60~5.4 s, morph 0.45→0.70, LP up-sweep, fold/drive flavour, aux 1); Feedback Plate (BP in-loop, env +0.9 oct, decaySkew 0.78 shimmer, aux 1); Feedback Shell variant (Size 0.55, mat 0.70, aux 2); Ghost Tone (HP, **modeStretch 0.77 / decaySkew 0.81**, morph, coupling 0.85, aux 1).
- **Glass-Bell pings 1/31** + **Clap 30** — pings: Bell, modeInject 0.2, decaySkew 0.6-0.7, aux 1, panned L/R; clap: NoiseBody+NoiseBurst, longer burst 0.6, noise 0.70 + click 0.40.

## Kit-level globals
- `globalCoupling ≈ 0.30` (was 0) so per-pad Coupling Amount / the ghost-tone's 0.85 sympathetic value engages.
- Aux buses 1-3 used for cymbals/drones/bells; main bus for drums.
- maxPoly left generous (FX tails overlap).

## Param-surface coverage (collective)
All 6 bodies · 5 of 6 exciters (Impulse omitted by design) · modeInject (sole user) · full modeStretch range incl. 0.88 · per-mode decaySkew on every modal body · env-level NonlinearCoupling · Fold + Drive flavour · pitch-env on Membrane+Plate+Shell+Bell · Material Morph · BP-filter-env feedback-drone mechanism · choke groups 1 & 2 · aux-bus spread · full L-R pan image · Membrane-only tensionMod + airLoading used only where live · Strike Position now explicitly voiced on kick/toms/crash (was a silent default on kick/crash).

---

## Verification log (6 issues found & fixed)

1. COVERAGE (pad 0, FM Kick): Strike Position was silently defaulted (0.3). It is meaningful for every body (mode-shape weighting; on a kick a near-center strike emphasizes the low Bessel fundamental). FIX: set Strike Position 0.25 (rationale: near-center, weights the (0,1) sub for kick weight) and document it.

2. COVERAGE (pad 13, FM-Bell Crash): the cited fm-bell-crash archetype explicitly lists Strike Position 0.32, Body Damping b1 0.30, Click Contact 0.30, and Noise Resonance 0.20 as load-bearing, but the proposal left all four at default. FIX: added Strike Position 0.32 (uneven clangy partial balance), Body Damping b1 0.30 (15.1 s^-1 long metallic ring floor), Click Contact 0.30 (~2.9 ms crisp impact), Noise Resonance 0.20 (Q~1.24, no pitched noise peak), per the archetype.

3. COVERAGE (pad 2, Feedback-Shell Snare): Strike Position left silent. The archetype table does not list it as load-bearing, but for completeness/consistency it is now documented in defaultedParams as an intentional default (0.3, edge-ish strike already excites the inharmonic bar bank) rather than a silent gap.

4. PHYSICS/SEMANTICS audit (no change needed, verified correct): tensionMod correctly REMOVED from all Plate toms + the Shell snare (Membrane-only no-op) and correctly RETAINED on the Membrane kick (0.50) and the one Membrane chaos pad (0.80). airLoading correctly set ONLY on Membrane pads (kick 0.40 live; chaos Membrane 0 deliberate). Plate bodies voiced as free-plate Chladni; Bell for tuned bells/hats/crash/ghost/glass; Shell for the bar snare/drone; String waveguide for the friction drone with all modal-bank params correctly flagged INHERENT no-ops. modeInject (0.2) confined to the chaos/algorithmic cycle + glass-bell pings (kit signature) and bypassed (0) on drums/drones. Drive treated as flavour, NonlinearCoupling as amplitude brightening. All correct.

5. RANGES audit (no change needed, verified): every valueNorm in [0,1]. Discrete decodes legal: chokeGroup 0.125->grp1, 0.25->grp2; outputBus 0.067->aux1, 0.133->aux2, 0.2->aux3; filterType 0->LP/0.5->HP/1->BP; Noise Color bands correct (kick 0.15->Brown, snare/drone 0.7/0.55->White, hat/crash 0.85->Violet). modeStretch norms are norm-of-[0.5,2.0] and read correctly (neutral 0.333 on kick).

6. LAYOUT audit (no change needed, verified): GM anchors kept on-map (kick 0, snare 2, hats 6/8/10, toms 5/7/9/11/12/14, crash 13); FX region re-crafted. Duplicate (20-pad i-ramp) and missing roles (clap, drones, aux-bus use) correctly flagged and resolved. Impulse exciter omission correctly documented. All 32 pads live.
