# Membrum Recipe — "FM Plate Snare" (west-coast metallic-plate snare)

**Body:** Plate (free-plate Chladni `(m+2n)^1.7` inharmonic bank)
**Exciter:** FMImpulse (non-integer FM ratio → inharmonic metallic clang)
**Concept:** Replace the 808/909 snare's two tuned tonal oscillators with a single *inharmonic plate* body driven by an FM exciter. Brighter, clangier, more "metal panel" than a tuned-membrane snare, sitting under a sharp stick crack and a bright wire-buzz noise layer.

## Acoustic / synthesis basis
- **Fundamental band:** acoustic 14" snare ≈ 170–200 Hz, first edge overtone ≈ 1.5–1.7× (~300–350 Hz); no clear pitch because partials are dense + inharmonic (idrumtune; hyperphysics; Madsen Phys406).
- **Synthetic analogue:** 808 snare = two T-network sine osc ~250 Hz & ~500 Hz + HP avalanche noise gated by "snappy"; 909 = 2 osc + waveshapers + tone/snappy noise (Wikipedia TR-808/909; SOS Practical Snare Synthesis). FM-plate variant swaps the two tones for a whole inharmonic plate.
- **Plate inharmonicity:** Chladni `f ∝ (m+2n)^P`, P≈1.6–1.7 for real cymbals, "1 nodal circle ≈ 2 nodal diameters" (Rossing 1982; UNSW; F&R). "Far from harmonic" (SOS Metallic).
- **FM clang:** non-integer carrier:modulator ratio → inharmonic spectrum; modulation index decaying bright→pure under a slow amp decay = "metallic bell clang" (Chowning; CCRMA; mercity).
- **Energy migration:** metallic energy moves *upward* through the spectrum over several hundred ms (SOS Metallic) → modelled by Material Morph 0.55→0.80 + amplitude-driven NonlinearCoupling.
- **Noise = wires:** dense near-random buzz = white noise HP/LP-shaped, snappy env (808/909; Nord book).
- **Crack:** 2–5 ms broadband contact transient, 2–8 kHz presence (idrumtune).

## Signal path
`FMImpulse (ratio 2.65) + Click crack → Plate bank (f0 226 Hz, stretch 1.325, scatter ~6.75%) → NonlinearCoupling (amp-bright) → ToneShaper (LP ~7 kHz, +env sweep, mild drive) × pitch env 230→160 Hz / morph 0.55→0.80 ‖ parallel white-noise wire layer (mix 0.60, ~3.9 kHz, ~80 ms) → ×level 0.95 → bus`

## Key normalized values (full table in `params`)
| Param | Norm | Physical |
|---|---|---|
| Exciter | 0.80 | FMImpulse |
| Body | 0.20 | Plate |
| Material | 0.45 | brightness 0.725 (morph-seeded) |
| Size | 0.55 | f0 ≈ 226 Hz |
| FM Ratio | 0.55 | mod ratio 2.65 (inharmonic) |
| Mode Stretch | 0.55 | stretch 1.325 (extra clang) |
| Mode Scatter | 0.45 | ~6.75% dither (random partials) |
| NonlinearCoupling | 0.45 | louder = brighter |
| Morph | on, 0.55→0.80 / 607 ms exp | upward energy cascade |
| PitchEnv | 230→160 Hz / 60 ms exp | body thump |
| Drive | 0.28 | harmonic fatten (flavour) |
| LP cutoff / env | 0.85 / +0.4, 114 ms | bright crack, tames hash |
| Noise | 0.60, 3.9 kHz, ~80 ms, white | snare wires |
| Click | 0.85, 2.2 ms, 6.7 kHz | stick crack |
| b1 / b3 | 0.28 / 0.18 | ~14 s⁻¹, metallic highs |
| Level / Pan | 0.95 / 0.5 | loud, center |

## Deliberate defaults (NO-OP or neutral)
Tension Mod (Membrane-only), Secondary shell coupling (plate is its own body), all non-FM exciter params, all 5 macros (voice already fully set), pitch-env knee, Air Loading (Membrane-only). See `defaultedParams`.

## Notes vs. corrected post-audit semantics
- Plate body is the corrected free-plate Chladni model (audit Phase-2) — voiced against `(m+2n)^1.7`, not the old SSSS ladder.
- Mode Stretch uses the corrected 1-indexed `sqrt(1+B·(k+1)²)` with widened B_max.
- NonlinearCoupling is the env-LEVEL waveshaping redesign (amplitude brightening, sustained), not the old transient AM/whole-signal recipSqrt.
- Drive is unity-makeup (timbre, not level). Level is linear pre-rail. Body normalized to −6 dBFS measured strike. Pan 0.5 = center.