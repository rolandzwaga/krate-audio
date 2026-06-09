# Membrum Recipe — "Singing Bowl" (Tibetan standing bell, bronze)

**Archetype:** a bronze bowl rubbed/bowed with a leather-wrapped puja — the antithesis of a struck bell. The Friction exciter supplies a soft stick-slip onset; the Bell body's very long, pure-metal ring carries a slowly-evolving near-pure tone.

## Body & Exciter
- **Body:** `Bell` (norm 0.8). Membrum's only tuned long-ring metal modal body. A standing bowl is physically a standing bell.
- **Exciter:** `Friction` (norm 0.6). Stick-slip rim contact (mode "lock-in"), not a strike.
- Caveat: Membrum's Friction is a ~46 ms transient bow, so the **sustain is owned by the Bell body** (long decay + low damping), not the exciter.

## Acoustic targets (cited)
| Property | Real bowl | Source |
|---|---|---|
| Fundamental | ~100–300 Hz ((2,0) mode); 6"→289 Hz, 12"→104 Hz | theohmstore spectral set |
| Partial ratios | ~1 : 2.8 : 5.2 : 8.0 (≈n², strongly inharmonic) | theohmstore / heavenofsound |
| Beating | 0.5–16 Hz from split degenerate modes (asymmetry) | theohmstore; Inácio/Antunes |
| Sustain (T60) | 40–75 s, metallic (highs ring ≈ as long as f0) | theohmstore |
| Attack | none — continuous stick-slip, no click | ScienceDaily / Inácio |
| Noise | faint dark friction scrape only | ScienceDaily |
| Pitch glide | none (steady pitch) | — |
| Material | high-tin bell bronze, hand-hammered | theohmstore |

## Key normalized values (NORM [0,1] → physical)
- Material **0.78** → Bell brightness ~0.93, base ring ~1.21 s (bronze, long highs)
- Size **0.45** → f0 ≈ 284 Hz (small bowl; raise to 0.6–0.7 for deep ~100–170 Hz bowls)
- Decay **0.95** → ~2.74× ring multiplier
- Body Damping **b1 0.30** (~15 s⁻¹ flat floor) + **b3 0.0** (pure flat = metallic long upper partials)
- Mode Stretch **0.45** → physical 1.175 (mild inharmonic widening toward bowl spacing)
- Decay Skew **0.85** → +0.7 (boosts low (2,0)/prime, tames highest partials → deep near-pure fundamental)
- Mode Scatter **0.06** (static detune ≈ the degenerate-mode beating Membrum can't otherwise make)
- Material Morph: **on**, **0.78 → 0.55** over **~1.7 s**, exponential (slow timbral settling)
- Friction Pressure **0.28** (firm scrape); Nonlinear Coupling **0.22** (louder rub = brighter)
- Click Mix **0.0** (no onset click); Noise Mix **0.10**, **Pink** (faint scrape)
- Output Bus **1** (its own send); Complexity macro **0.85**; Pan center

## Deliberate defaults / no-ops
ToneShaper filter & Drive/Fold transparent (keep the tone pure); PitchEnv off (steady pitch); Mode Inject 0 (would impose a harmonic series on an inharmonic body); secondary shell off (single resonator); Tension Mod inert (Membrane-only — left 0); FM/Feedback/NoiseBurst params are no-ops under the Friction exciter.

## Implementation note vs. the existing preset (generator pad 15)
The shipped "Singing bowl" pad (membrum_preset_generator.cpp:3000) already matches the brief and is sound, with two refinements suggested here: (1) `tensionModAmt` is set to 0.40 there but is **Membrane-gated → inert on Bell** (harmless but misleading; can be 0); (2) `modeScatter` defaults to 0 there — adding a small ~0.05–0.06 dither is the closest available analog to the bowl's signature degenerate-mode beating.