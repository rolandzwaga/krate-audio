# Membrum Recipe — "Chaos Voice (max-everything algorithmic)" (synthetic-fx)

> The most extreme synthetic sound-design voice: aggressive, distorted, inharmonic, evolving chaotic hits with **no acoustic referent**. There is nothing to reproduce — the goal is *maximum controlled instability* under the −1 dBTP bus limiter. This is a **per-pad CYCLED** archetype: body, exciter, and filter rotate across the kit; the chaos *is* the rotation. The repo's existing `chaosEngineKit()` (tools/membrum_preset_generator.cpp:3387) is the canonical implementation of exactly this — these values track and refine it against the post-audit corrected semantics.

## Body / Exciter
- **Body:** CYCLE `i%6` → Plate / Shell / String / Bell / Membrane / NoiseBody. Seed = **Plate** (free-plate Chladni, most inharmonic/metallic, home of the von-Kármán cymbal nonlinearity).
- **Exciter:** CYCLE `i%3` → **Feedback / FMImpulse / Friction** (the three energetic/self-oscillating exciters). Seed = Feedback.
- **Filter type:** CYCLE `i%3` → LP / HP / BP, **engaged** (cutoff ≈ 0.55, not the 1.0 bypass).

## The four synthesis pillars (this archetype's "physics")
1. **Feedback FM / self-oscillation** → `Feedback Amount 0.7` (→ ≤0.85 self-osc drive, SC-008 rail). Drives squeal/sustained instability. *(loopmasters; unison.audio)*
2. **Chowning inharmonic FM** → `FM Ratio 0.45` (c:m ≈ 2.35, non-integer → Bessel-sideband bell/metallic spectrum). *(Wikipedia FM; cs.cmu.edu; ccrma)*
3. **Von-Kármán nonlinear plate coupling** (cymbal/gong energy cascade + amplitude pitch glide) → `Nonlinear Coupling 0.75` (louder→brighter, sustained) + `Tension Mod 0.8` (amplitude pitch glide, Membrane voices). *(sciencedirect S0022460X15000759; researchgate von-Kármán cymbal; Avanzini-Marogna-Bank 2012)*
4. **Buchla west-coast wavefolding + drive** → `Fold 0.35` (≈1.1 rad sine-fold) + `Drive 0.45` (≈5×, odd-harmonic, flavour-not-level post-M-2). *(buchlaarchives; perfectcircuit; DAFx17 Buchla 259)*

## Chaos fingerprint params
- `Mode Stretch 0.6` (phys 1.4) — clearly metallic/inharmonic partial spacing (B up to 0.01, post-audit 1-indexed).
- `Mode Scatter 0.6` (~9% frequency dither) — no two hits identical.
- `Decay Skew 0.55` — per-mode spectral tilt (all modal bodies, M-5).
- `Morph Enabled` (half the pads), Start 0.2 → End 0.85 over ~800 ms — material sweeps *within* each hit.
- `Complexity 0.85` macro, `Coupling Amount 0.78` + kit `globalCoupling ~0.92` — emergent inter-voice sympathetic chaos.
- `Secondary Enabled` + `Coupling Strength 0.55` — coupled shell weight/beating.
- **Choke groups** spread (0/1/2) and **Output buses** spread (0–3) + per-pad **Pan** — voices stutter-cut and occupy different spectral/spatial slots instead of one mush.

## Gain staging
Body trimmed to −6 dBFS measured-strike (N-1); Drive/Coupling are knob-gated flavour stages under the rail; Level 0.72 (hot voices); the −1 dBTP `TruePeakLimiter` owns the N-voice ceiling. Drive/Coupling were the two stages re-voiced after the audit — values here target the **corrected** behaviour.

## Deliberate defaults
Mode Inject **OFF** (clean harmonics fight the chaos), Air Loading **OFF** (anti-whistly = wrong aesthetic; Membrane-only anyway), Brightness/BodySize/Punch macros **neutral** (underlying params set explicitly), Strike Position default, Pitch-Env Knee off, NoiseBurst Duration moot (exciter never NoiseBurst).

> All values normalized [0,1] (on-wire). Cycled params show a representative seed value; per-pad `i`-indexed cycling across the 14 crafted pads is the implementation (see `chaosEngineKit()`).