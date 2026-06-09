# Membrum Recipe — "Feedback Plate/Shell Drone" (drone)

A self-oscillating, feedback-sustained **inharmonic metallic plate/cymbal/gong drone**: a ringing, evolving boxy/metallic sustain driven by the regenerative Feedback exciter through the body's inharmonic modes. All values below are **normalized [0,1]** (preset/on-wire); the physical target each denormalizes to is given.

---

## 1. Acoustics & synthesis research (cited)

**Body = free-edged metal plate / cymbal / gong.** A struck cymbal has FREE edges, so its partials follow **Chladni's power law** `f(m,n)=C·(m+2n)^P` (m nodal diameters, n nodal circles). Classical Kirchhoff `P=2`; real measured cymbals sit **lower, P≈1.6–1.73** (Rossing & Peterson 1982), which packs the upper partials and produces *shimmer*. A 16" cymbal has **~300 measured modes**; spectrum spans ~100 Hz to >5 kHz, strongly **inharmonic** (bronze inharmonicity coeff ≈0.3–0.8). [auditory.org 5pMU8; ResearchGate "normal modes of cymbals"; oemcymbal]

**Pitch / size.** Small gongs f0 ~200–800 Hz; large gongs 65–250 Hz; 36" tam-tam ~65–70 Hz. A drone wants weight, so the fundamental sits low/mid. [mosicocymbal]

**Decay / T60.** Metallic plates ring long, low partials longest (Chaigne `R=b1+b3·f²`). Small gong T60 0.3–0.8 s; large gong 2–15 s; tam-tam >40 s. In a *feedback* drone the exciter re-injects energy faster than the body leaks, so sustain is effectively unbounded. [mosicocymbal; CCRMA T60]

**Energy cascade / shimmer.** A few ms after a hard strike, a tam-tam/gong builds energy into the **high-frequency range** — a wave-turbulence energy cascade whose front propagates to high wavenumbers, leaving a steady broadband spectrum (the perceptual shimmer). [Fletcher 1999 "Nonlinear physics of musical instruments"; Fletcher 2012 acoustics.asn.au; ENSTA/Edinburgh wave-turbulence; arXiv:1509.02737]

**Regenerative drone mechanism.** A band-limited positive-feedback loop around a high-Q resonator self-oscillates once loop gain·Q meets the **Barkhausen criterion**; a **bandpass in the loop** selects which mode sustains and suppresses the rest (E-bow / self-oscillating filter / self-excited microcantilever). [modwiggler resonant filter; ncbi PMC10006876; arXiv:2309.11581; ambientguitar.net]

---

## 2. Body & exciter mapping

- **Body = Plate** (`Body Model` 0.2 → idx 1): Membrum's corrected free-plate Chladni `(m+2n)^1.7·(1+0.11n)` lattice (post-audit §3-B) — exactly the inharmonic cymbal/gong fingerprint. *Sibling:* set `Body Model` 0.4 → **Shell** (free-free Euler-Bernoulli bar 1:2.757:5.404:8.933…) for a tighter "boxy bar" drone.
- **Exciter = Feedback** (`Exciter Type` 1.0 → idx 5): energy-limited body↔exciter loop (`drive=vel+feedbackDrive·(1−vel)`, `feedbackAmount_≤0.85`, RMS limiter at 0.35). Selecting it forces the per-sample body path and keeps the in-loop soft-clip.
- **ToneShaper = BANDPASS + ADSR**: the in-loop band selector. High-Q, swept upward by the filter env → an *evolving* drone, not a static squeal.
- **NonlinearCoupling 0.55**: env-level waveshaping (louder loop ⇒ more odd harmonics, sustained) — the cheap, literature-grounded stand-in for the cymbal nonlinear energy cascade.
- **decaySkew 0.78** (per-mode tilt toward high partials, all bodies post-audit M-5) → static shimmer signature.

---

## 3. Baseline parameters (normalized → physical)

| Param | Norm | Physical target | Why |
|---|---|---|---|
| Exciter Type | **1.0** | Feedback (idx 5) | regenerative self-oscillating loop = the archetype |
| Body Model | **0.2** | Plate (idx 1) — *0.4 → Shell* | free-plate Chladni inharmonic metal |
| Material | **0.62** | brightness 0.81; base decay ≈1.39 s | bright sustaining bronze |
| Size | **0.45** | f0 ≈ 283 Hz (Shell ≈ 531 Hz) | felt metallic hum, not a screech |
| Decay | **0.92** | ≈2.7× → ~3.9 s body ring | long-ringing plate the loop tops up |
| Strike Position | **0.45** | ρ=0.625, balanced (m,n) | many partials for the loop to sustain |
| Level | **0.62** | linear −4.2 dB pre-rail | drone runs hot; leave bus headroom |
| Filter Type | **1.0** | Bandpass | in-loop band selector |
| Filter Cutoff | **0.55** | ≈743 Hz BP center | sit on low/mid partials (boxy core) |
| Filter Resonance | **0.45** | Q≈4.9 | focused singing resonant band |
| Filter Env Amount | **0.65** | +0.30 → up to +0.9 oct sweep | drone EVOLVES / brightens (HF cascade) |
| Filter Env Atk | **0.45** | ≈46 ms | blooms open, no strike |
| Filter Env Dec | **0.6** | ≈432 ms | band eases back from peak |
| Filter Env Sus | **0.55** | 0.55 | holds the band open while held |
| Filter Env Rel | **0.65** | ≈549 ms | graceful fade on note-off |
| Mode Stretch | **0.45** | phys 1.175 | extra hand-hammered inharmonicity |
| Decay Skew | **0.78** | +0.56 → boost HIGH modes | bright shimmer / energy-cascade trace |
| Mode Inject | **0.0** | bypass | no synthetic harmonic series (keep inharmonic) |
| Nonlinear Coupling | **0.55** | drive=1+6·vel·0.55·env | amplitude→brightness (nonlinear cascade) |
| Feedback Amount | **0.45** | drive floor 0.45 (→≤0.85) | above self-sustain floor, under stability cap |
| Noise Mix | **0.3** | broadband wash bed | glues partials into a metallic wash |
| Noise Cutoff | **0.55** | ≈1.6 kHz LP | mid/bright air under the partials |
| Noise Color | **0.55** | White | flat metallic sizzle |
| Noise Decay | **0.85** | ≈1170 ms | sustained wash |
| Click Mix | **0.0** | silent | drone blooms, no beater click |
| Mode Scatter | **0.4** | ~6% dither | hand-hammered detune, no sterile lock |
| Body Damping b1 | **0.3** | ≈15 s⁻¹ floor | long RT60; loop tops up |
| Body Damping b3 | **0.2** | 2e-4 s (weak f²) | keep high partials alive (metallic) |
| Output Bus | **0.067** | aux bus 1 (pre-master) | independent drone fader |
| Pan | **0.5** | center (equal-power) | wide centered bed |
| Drive Amount | **0.0** | bypass | coupling already supplies harmonics |
| Fold Amount | **0.0** | bypass | wavefold too buzzy for metal drone |

### Shell variant deltas
Set `Body Model` **0.4** (Shell, f0=1500·0.1^size → ≈531 Hz at Size 0.45). Optionally drop `Size` to ~0.55 (≈422 Hz) so the boxier bar series doesn't sit too high, and nudge `Material` up (Shell brightness=0.85+0.15·material) for a tighter glassy bar ring. Everything else identical.

---

## 4. Deliberate defaults (per-pad coverage)

- **PitchEnv (all 8 params)** — Time=0 disables the glide; a drone has no 808 boom, pitch is the static inharmonic lattice.
- **Tension Mod 0** — gated to Membrane only; literal NO-OP on Plate/Shell.
- **Material Morph (5 params) off** — evolution is carried by the feedback loop + filter sweep, not a per-hit material morph (would muddy the steady spectrum).
- **Air Loading** — Membrane-only frequency table; NO-OP here.
- **Coupling Strength / Secondary Enabled / Size / Material 0** — secondary shell adds struck-drum body weight, irrelevant to a single self-oscillating plate; the drone *is* the body.
- **Coupling Amount 0.5 / Choke Group 0** — no cross-pad sympathetic network or choking wanted for an isolated sustain.
- **Tightness / Brightness / Body Size / Punch / Complexity macros = 0.5** — neutral zero-delta so the directly-set material/decay/cutoff/skew/coupling values are preserved exactly.
- **Click Contact / Brightness** — no-ops (Click Mix 0). **Noise Resonance ~0.2** — broad wash, not a pitched peak on the forced-lowpass noise path.
- **FM Ratio / NoiseBurst Duration / Friction Pressure** — other-exciter params, NO-OPs with Feedback selected.
- **Pad Enabled 1.0** — pad on.

---

## 5. Sources
auditory.org Chladni's law for cymbals; oemcymbal vibration modes; ResearchGate "normal modes of cymbals" / "nonlinear vibrations and chaos in gongs and cymbals"; Fletcher 1999 *Nonlinear physics of musical instruments* (UNSW) & Fletcher 2012 (acoustics.asn.au); ENSTA/Edinburgh wave-turbulence + arXiv:1509.02737; mosicocymbal gong size/decay; CCRMA T60; ncbi PMC10006876 & arXiv:2309.11581 self-excited bandpass oscillators; modwiggler resonant filter; ambientguitar.net E-bow/resonant drone; Valhalla plates-vs-chambers; ADSR feedback FX. Implementation verified against `plate_modes.h`, `shell_modes.h`, `plate_mapper.h`, `shell_mapper.h`, `feedback_exciter.h`, `nonlinear_coupling.h`, `voice_pool.cpp`, and `AUDIT-signal-path-2026-06-07.md` (post-audit corrected semantics).