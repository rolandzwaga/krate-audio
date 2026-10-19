# Profundum — Mass Bass Instrument Roadmap

*(Name: **Profundum**, Latin "the deep" — the Krate naming line of Iterum / Disrumpo / Ruinae /
Innexus / Gradus / Membrum / Seraphis / Vorago. Concept source: `specs/Profundum-concept.md`.)*

A phased, DSP-library-first plan for a bass instrument built around **mass rather than layers**: one
physically coherent bass voice that moves from pure sub → resonant bass → harmonic bass → distorted
speaker-grabber while never losing its low-frequency centre. Each phase is sized to become one speckit
spec with its own tests and evaluation criteria. Phases 1–9 build and unit-test KrateDSP components;
phases 10–13 assemble the plugin.

## Positioning in the lineup

| Plugin | Identity |
|---|---|
| Ruinae | instability, chaos, aggression |
| Disrumpo | distortion as the subject |
| Innexus | reconstructing harmonic identity from existing sound |
| Seraphis | ethereal, weightless, angelic evolution |
| Vorago | dark, massive, subterranean, geological time |
| **Profundum** | **extremely controlled, physical, low-frequency synthesis that becomes complex without ever losing its centre** |

Against the external references the concept was written from:

| Instrument | Core idea |
|---|---|
| Beatsurfing Low Bass | fast bass creation / immediate results |
| Arturia Pure SUB | dedicated multi-layer bass-design workstation |
| **Profundum** | **one physically coherent bass voice that morphs between clean weight and harmonic aggression** |

Not sold as "oscillator + sub + filter + distortion". Sold as **"a bass synthesizer where the entire
sound is one controllable mass."** The four proprietary ideas are **Mass**, **Anchor**, **Spectral
Transfer** (the SHIFT control) and **Bass Morph**; envelopes, saturation, stereo, glide exist because a
bass needs them. The four Personality macros replace a bank of oscillator presets.

## Core Philosophy

- **One evolving bass organism, not Sub + Harmonics + Texture layers.** Depth / Body / Edge / Shift
  control different aspects of the *same* harmonic spectrum; they are not four brightness knobs.
- **Harmonics are regenerated, not filtered.** The core is an additive harmonic engine whose spectrum
  is computed analytically from the controls, so character tracks the keyboard (harmonic 4 is always
  harmonic 4) and morphing/modulation is coherent.
- **The fundamental never disappears.** A per-voice Sub Anchor sits outside every nonlinear stage by
  default. Filthy midrange + pristine sub is the mix-ready promise.
- **Mono core, stereo-capable image.** Weight stays centred; harmonics above it may spread and move.
  Width is part of the generation architecture, not an effect bolted on at the end. Mono compatibility
  is measured, never assumed ("mono below 120 Hz" alone is rejected: crossovers shift phase and
  distortion creates harmonics on both sides of the boundary).
- **Concepts over parameters.** Mass, Shift, Shape, Spread, the four Personality macros and the
  Morph slider are the surface; the synthesis architecture is an Advanced page.
- **Alias-free low end is engineered.** Additive synthesis is band-limited by construction; every
  nonlinear stage ships oversampled or ADAA with a measured aliasing bound at the lowest notes.
- **Nothing ships inert.** Every component's levers are proven audible by a render descriptor before
  presets exist (Vorago Phases 13b–13d lesson).

## Architecture Overview

```
                    ┌───────────────────┐
                    │  Note / Gesture   │  MONO (default) · POLY (4 voices)
                    └─────────┬─────────┘
                              ▼
┌─ Per Voice ──────────────────────────────────────────────────────────┐
│                                                                      │
│  ┌─────────────────────────────────────┐   ┌───────────────────────┐ │
│  │ HARMONIC CORE                       │   │ SUB ANCHOR            │ │
│  │ harmonic generator (64 partials)    │   │ sine/tri/rounded/     │ │
│  │ → spectral shape (Depth·Body·Edge)  │   │ asym/octave-down,     │ │
│  │ → spectral SHIFT                    │   │ phase-locked to h1,   │ │
│  │ → edge excitation (nonlinear)       │   │ LOCK/FOLLOW/PUNCH/    │ │
│  │ + low-note guard, per-partial width │   │ GHOST — always mono   │ │
│  └──────────────┬──────────────────────┘   └──────────┬────────────┘ │
│                 ▼                                     │              │
│  ┌─────────────────────────┐                          │              │
│  │ SHAPE  ROUND·HOLLOW·BITE│  one morph control       │              │
│  │ (concept §13 "spectral  │                          │              │
│  │  shaper")               │                          │              │
│  └──────────────┬──────────┘                          │              │
│                 ▼                                     │              │
│  ┌─────────────────────────┐                          │              │
│  │ HARMONIC FOLD           │  soft sat → harmonic     │              │
│  │ (concept §13 "saturator")  compression → fold      │              │
│  └──────────────┬──────────┘                          │              │
│                 ▼                                     │              │
│  ┌─────────────────────────┐                          │              │
│  │ CHARACTER  Tube·Fold·   │  Amount + Tone each,     │              │
│  │ Rectify·Bite·Crush·     │  L/R-capable             │              │
│  │ Fracture                │                          │              │
│  └──────────────┬──────────┘                          │              │
│                 ▼                                     ▼              │
│          DROP (pitch exciter) · amp envelope · voice sum (+ Anchor)  │
└──────────────────────────────┬───────────────────────────────────────┘
                               ▼
┌─ Global ─────────────────────────────────────────────────────────────┐
│  MASS           transient · compression (sub-safe sidechain) · law   │
│  STEREO FIELD   SOLID · SPREAD · WIDE  +  GRAVITY  +  MOTION         │
│  OUTPUT         DC safety · true-peak limit                          │
│                                                                      │
│  CONTROL LAYER  Personality macros (Weight · Dark↔Bright ·           │
│                 Tight↔Loose · Clean↔Violent) · Motion (ENV · CYCLE · │
│                 DRIFT, global depth, Motion→Mass) · BASS MORPH (A/B  │
│                 complete states, global, Anchor morphs on its own)   │
└──────────────────────────────────────────────────────────────────────┘
```

Mapping to the concept's §13 chain (Oscillator ∥ Anchor → Harmonic Engine → Spectral Shaper →
Saturator → Character → Compressor → Stereo Field → Limit): the Harmonic Engine is the Harmonic Core,
the Spectral Shaper is the SHAPE stage (§6), the Saturator is the Harmonic Fold (§7), and the Compressor
is the Mass dynamics block. Two deliberate deviations: the Anchor joins *after* Character rather than at a
mix directly after the oscillators (§7's "the sub anchor is excluded" and the 2026-10-09 ruling to keep
the Anchor outside the character stages by default), and the Spectral Transfer stage is inside the
oscillator rather than after it (harmonics are regenerated, not filtered). The concept's three output
regions (CLEAN LOW / BODY-MID / EDGE-TEXTURE) survive as the Sub / Body / Presence descriptors used by
every audibility test and by the UI's energy view — not as hard bands in the signal path.

### Key Design Decisions (ruled 2026-10-09)

1. **Core = additive harmonic engine with a continuously variable spectral shape.** Up to 64
   harmonics (raise later if presence needs it), Nyquist-capped. Depth / Body / Edge are overlapping
   Gaussian-like envelopes on the log-harmonic-number axis; Shift moves the Body/Edge envelope centres
   along that axis; the result is combined and loudness-normalised so Depth is not a volume knob. No
   hard harmonic boundaries (no "2–8 vs 9+" switch). Deterministic phases. Build the linear core first;
   nonlinear excitation (oversampled soft clip, blend tied to Edge) is a separate phase, because
   "more upper harmonics" and "new harmonics from nonlinearity" are different capabilities.
2. **Mono core, stereo-capable output — a design principle, not a mode.** The Anchor is always
   centred (no stereo modulation, unison or phase tricks). The harmonic voice may generate width
   (per-partial pan/phase offsets, L/R-different character processing). **SPREAD** is the
   frequency-dependent width macro (zero = centred; rising = upper harmonics widen, lows stay put;
   crossover and curve exposed to advanced users). Three image modes: **SOLID** (mono voice), **SPREAD**
   (default — centred Anchor, stereo harmonic voice), **WIDE** (expansive stereo processing, Anchor
   still centred). Mono-sum phase cancellation is checked by test at every stereo-touching stage.
3. **MONO default + POLY mode.** MONO: one active voice, a new note replaces the previous (legato,
   retrigger, glide, note priority). POLY: each note its own voice, **4 voices** to start (raisable),
   steal the oldest *released* voice first, then the oldest active. Every voice owns its oscillator
   state, Anchor, amplitude and pitch envelopes and voice-level processing — the Anchor is per voice,
   never one shared oscillator. Mono/stereo image and mono/poly are independent axes.
4. **Bass Morph interpolates two complete sound states, globally.** A and B each hold the full
   sound-shaping state (core, fold, shape, character, drop, amp envelope, stereo, motion config, mass,
   anchor). Interpolation is per-parameter with the right law (linear / logarithmic for frequencies and
   times / equal-power for mixes / crossfade or algorithm-specific transition for discrete choices);
   the core's spectrum is derived from the *interpolated controls*, never an audio crossfade. The
   Anchor morphs on its own curve: **Lock** (constant), **Blend** (interpolated), **Punch** (designed
   curve — weight retained early, yielding late). Morph is one continuous, automatable performance
   control affecting all voices together; it is not a preset switcher.
5. **No conventional filter section, no FX rack.** Three character processors (ROUND / HOLLOW / BITE)
   behind one SHAPE control; six Character algorithms with Amount + Tone each. Pure SUB's 12-slot FX
   rack is deliberately not competed with.
6. **Share the Seraphis/Vorago substrate, diverge at the identity layer.** Harmonic bank, nonlinear
   primitives, oversampling, dynamics, crossover/M-S, envelopes, LFO, drift, voice allocation, mono
   handling, macro-matrix pattern and the Vorago plugin scaffold are reused. Profundum-specific
   components are the spectral-shape recipe, Harmonic Fold, Anchor, Drop, Shape morph, character
   stage, transient shaper, Mass law, bass stereo field and the Bass Morph engine.

## Reuse Inventory (existing KrateDSP → Profundum)

Legend: ✅ = exists and is largely sufficient · 🔶 = exists, needs extension · 🆕 = new component.
Paths relative to `dsp/include/krate/dsp/`.

| Plan layer | Existing components | Verdict / new work |
|---|---|---|
| Harmonic generator | `processors/harmonic_oscillator_bank.h` (+`_simd`: 96 coupled-form sines, zero latency, per-partial amp/ratio/phase, `setStereoSpread` per-partial pan), `core/phase_utils.h` (`PhaseAccumulator`), `core/pitch_utils.h`, `core/midi_utils.h`. `processors/additive_oscillator.h` (IFFT overlap-add, 128 partials) has FFT latency — **rejected** for a bass that must be sample-accurate on attack. | ✅ bank. 🆕 `SpectralShapeRecipe` (L2, pure math D/B/E/S → aₙ) + 🆕 `ProfundumCore` (L3). |
| Nonlinear primitives | `primitives/tanh_adaa.h`, `hard_clip_adaa.h`, `wavefolder.h`, `chebyshev_shaper.h`, `waveshaper.h`, `core/sigmoid.h`, `core/wavefold_math.h`; `processors/wavefolder_processor.h`, `saturation_processor.h`, `tube_stage.h`, `diode_clipper.h`, `bitcrusher_processor.h`, `feedback_distortion.h`; `primitives/oversampler.h` (2×/4×, IIR economy or FIR). | ✅ pieces. 🆕 `HarmonicFold` (L2), 🆕 `Rectifier` (L2 — only inline rectification exists, inside `FuzzProcessor`), 🆕 `BassCharacterStage` (L3). **ODR: `CharacterProcessor` already exists in `systems/` — do not reuse that name.** No generic `OversampledProcessor` wrapper exists; Disrumpo's `oversampling_utils.h` is plugin-local. |
| Shape processors | `primitives/svf.h`, `ladder_filter.h`, `processors/multimode_filter.h`, `spectral_tilt.h`, `primitives/dc_blocker.h` | ✅ filters. 🆕 `ShapeMorph` (L3) composition with equal-loudness crossfade. |
| Sub Anchor | `processors/sub_oscillator.h` (÷2/÷4 flip-flop; Vorago's `systems/subharmonic_engine.h` composes three of them), `PhaseAccumulator`, `processors/yin_pitch_detector.h` + `subharmonic_validator.h` (FOLLOW candidate), `primitives/pitch_tracker.h` | 🔶 phase-lock idea from `SubOscillator`. 🆕 `AnchorOscillator` (L3): waveform set, LOCK/FOLLOW/PUNCH/GHOST, strictly mono. |
| Pitch & note behaviour | `processors/mono_handler.h` (`MonoMode` Last/Low/High, legato, `PortaMode` Always/LegatoOnly, portamento time — used by Ruinae), `processors/note_processor.h`, `primitives/smoother.h` (`OnePoleSmoother`, `LinearRamp`, `SlewLimiter`), `systems/voice_allocator.h`; Membrum's `PitchSegmentEnvelope` is plugin-local and stays there. | 🔶 extend `MonoHandler` (constant-rate and quantized glide) — append-only, Ruinae green. 🆕 `DropEnvelope` (L2). ✅ `VoiceAllocator` for POLY. |
| Dynamics | `processors/dynamics_processor.h`, `true_peak_limiter.h`, `envelope_follower.h`, `sidechain_filter.h`, `core/modulation_source.h` `TransientDetector` (a mod source, not a shaper) | ✅ compressor/limiter. 🆕 `TransientShaper` (L2) — none exists. |
| Stereo | `processors/midside_processor.h`, `crossover_filter.h` (`CrossoverLR4`, flat-sum), `core/stereo_utils.h`, bank per-partial pan; `systems/stereo_field.h` is delay-flavoured (Mono/PingPong/DualMono) | 🆕 `BassStereoField` (L3): generation-domain width law + post safety stage. No Haas, rotation or frequency-dependent width exists. |
| Modulation / macros | `primitives/adsr_envelope.h`, `processors/multi_stage_envelope.h`, `primitives/lfo.h` (tempo sync via `core/note_value.h`), `processors/brownian_drift.h`, `systems/voice_mod_router.h`, `systems/seraphis_macro_matrix.h` / `vorago_macro_matrix.h` (pattern) | ✅ sources and router. 🆕 `ProfundumMacroMatrix` (L3, pattern copy). |
| Morph | `plugins/disrumpo/src/dsp/morph_engine.h` (plugin-local, 2–4 distortion nodes, family crossfade — the *discrete-choice transition* idea), `systems/spectral_morph_engine.h` (spectral states), `processors/harmonic_frame_utils.h` | 🆕 `MorphState` (plain data) + `BassMorphEngine` (L3). A generic A/B parameter-state interpolator does not exist anywhere. |
| Voice / engine | `systems/seraphis_voice.h`, `seraphis_engine.h`, `vorago_voice.h`, `vorago_engine.h` (composition + determinism-harness template), `systems/synth_voice.h` / `poly_synth_engine.h` (basic subtractive) | 🔶 pattern only. 🆕 `ProfundumVoice`, `ProfundumEngine` (L3). |
| Output safety | `true_peak_limiter.h`, `dc_blocker.h` | ✅ direct reuse. |
| Plugin infrastructure | `plugins/vorago/` scaffold (Ruinae shape + Membrum bus config), `plugins/shared/` preset manager, `ArcKnob`, `XYMorphPad`, `BipolarSlider`, `ADSRDisplay`, `PitchEnvelopeDisplay`, DataExchange piggyback (Membrum MetersBlock pattern); Disrumpo's `SpectrumDisplay` / `SpectrumAnalyzer` (controller views, not yet shared) | ✅ scaffold and controls. 🔶 spectrum view: promote Disrumpo's or build a harmonic-bar view — Phase 12 decides. |

ODR note: before creating any class below, run `grep -r "class Name" dsp/ plugins/`. Near-name hazards:
`CharacterProcessor`, `SubOscillator`, `StereoField`, `MorphEngine`, `MorphNode`, `TransientDetector`,
`HarmonicCloud`, `SpectralState`, `MacroMapper`.

## Relationship to Seraphis and Vorago

Both shipped their DSP phases (Seraphis 1.0 on 2026-08-31; Vorago Phases 1–13c complete, 13d in
progress). **No Profundum phase is blocked by either.** Profundum consumes the shared bank, nonlinear
primitives, dynamics, crossover/M-S, envelopes, LFO, `BrownianDrift`, `VoiceAllocator`, `MonoHandler` and
the macro-matrix / voice-engine / plugin-scaffold *patterns* as they are today. The one shared-header
change this roadmap plans (`MonoHandler` glide modes, Phase 4) is append-only and gated on Ruinae's
suites.

**Recommendation:** Phase 1 first (it unblocks 2, 3, 7 and 8); Phases 4, 5 and 6 are chain-stage
components with no dependency on the core and can run in parallel with it; Phase 9 composes everything.

---

## Part A — DSP Foundations (KrateDSP, unit-tested, no plugin yet)

### Phase 1: Harmonic Core

**Status: ✅ COMPLETE (2026-10-10)** — see specs/profundum-phase1-harmonic-core/compliance.md (75 of 75 after
two main-loop closures). Shipped: `SpectralShapeRecipe` (L2) and `ProfundumCore` (L3) over the existing
`HarmonicOscillatorBank` with one append-only bank method (`restoreCenterPan`). Rulings during the build: Depth is
the base-law tilt (1/n → 1/n² at depth 0.5 → sine); Body emphasis is a whole-vector odd-harmonic bias; per-sample
f0 input; unweighted power normalisation; Low Note Guard onset C1; dB-linear cap taper; SC-010 zipper measured
on the residual against a per-sample ideal that includes the mandated smoothing, which exposed real stair zipper
at a 64-sample control interval — **`kControlInterval` = 32** shipped (D-4). Measured: partial accuracy
≤ 0.00036 cent; Depth loudness flat ±0.5 dB; Body/Edge/Shift spans 8.65 dB / 8.89 dB / 1.43 oct; anchors and
Heavy/Hollow/Growl within tolerance (margins 7.5 / 25.7 / 12.8 dB); aliasing ≤ −122 dB; sidebands worst −67.3 dB
(bar −60); CPU 0.602 % of one core at 64 partials (bar 1 %). **64 partials ship** (96 measured 0.943 %, listening
verdict "more presence in some states, no difference in others"); 96 remains reachable at `prepare`.
**Amended 2026-10-13 (found by Phase 4, FR-070 / SC-028):** the bank re-rolled each partial's amplitude by up to ±11 dB after a
large pitch motion (the magic-circle state kept across a coefficient change; a Phase 1 latent defect, invisible on static
notes and ±12 st bends). Ruled as an opt-in, append-only bank fix (`setContinuousFrequencyUpdates`, default off, other
consumers bit-identical) that `ProfundumCore::prepare` enables: the spectrum is preserved across a 48 st Drop within
0.1 dB, Phase 1's bend residual improved from −60 to −67.6 dB against its reference re-expressed in the same law
(Phase 1 Clarifications 2026-10-13); no Phase 1 bar changed.

**Spec:** `profundum-phase1-harmonic-core`
**Goal:** The sound source. A continuously variable harmonic spectrum under four controls that stay
musically useful across the whole bass range. Linear only — get this sounding excellent before any
nonlinearity.

New components:

- `SpectralShapeRecipe` (L2, `processors/spectral_shape_recipe.h`, pure math, no audio) — computes the
  per-harmonic gain vector aₙ = Aₙ(D, B, E, S) for n = 1…N:
  - **Depth** — fundamental dominance: relative gain of h1 against the rest, loudness normalised
    separately so Depth is never a volume knob. Max = near-pure sine.
  - **Body** — a smooth envelope over the low-order harmonics (≈ 2–8 by the 2026-10-09 ruling; the
    concept said ~8–16, and the Edge envelope overlaps from there) with adjustable curvature and
    emphasis: round ↔ hollow ↔ woody ↔ nasal ↔ thick.
  - **Edge** — a smooth envelope raising the high-order partials: smooth ↔ bright ↔ buzzy; Phase 2's
    excitation extends it to aggressive.
  - **Spectral Shift** (the concept's *Spectral Transfer*) — moves the Body and Edge envelope centres
    along the *logarithmic harmonic-number axis* (heavier/rounder ↔ upper-partial emphasis), so energy
    moves between the Sub / Body / Presence regions. Not a pitch shift, not an EQ sweep. The concept's
    three named distributions — **Heavy** (energy piled on the sub), **Hollow** (body-dominant, thin sub
    and presence), **Growl** (presence-dominant) — must each be reachable from the D/B/E/S space and are
    the named test targets.
  - **Waveform progression** — the D/B/E space must cover the concept's continuous path *sine →
    rounded triangle → saw-like*: the harmonic laws of a pure sine, an odd-harmonic 1/n² triangle and a
    1/n saw are reachable anchor points (within a ruled tolerance). The *asymmetric → clipped →
    increasingly complex* tail of that path is Phase 2's (even harmonics and new harmonics come from
    nonlinearity, not from the recipe).
  - Envelopes are Gaussian-like in log-n space, overlap, and are combined and normalised. No hard
    boundaries between regions. Perceptually distinct, not mathematically independent.
  - **Low Note Guard** — a documented taper of harmonic density as f₀ falls (an extremely low C must
    not become mud) plus a hard cap on partials above Nyquist.
- `ProfundumCore` (L3, `systems/profundum_core.h`) — recipe → `HarmonicOscillatorBank` (64 partials
  default, count a prepare-time constant decided after CPU measurement), per-block recipe evaluation
  with per-sample amplitude smoothing in the bank, deterministic harmonic phases, a retrigger phase
  policy input (reset vs free-running, consumed by Phase 4), and a per-partial pan/offset vector hook
  (consumed by Phase 7; identity in this phase).

**Success criteria:** partial frequency accuracy (< 0.1 cent static); Depth sweep monotonic in h1/rest
ratio with output loudness flat within ±0.5 dB; Body, Edge and Shift each move a distinct spectral
descriptor (centroid, low-order energy ratio, high-order energy ratio) monotonically and
distinguishably — the first **audibility gate**; harmonic-domain spectrum invariant across C1 → C3 within
tolerance (character tracks the keyboard); sine / triangle / saw anchor points and the Heavy / Hollow /
Growl distributions each reached within tolerance; no zipper under full-speed control sweeps and pitch
bends (per-block amplitude delta bounded); energy above Nyquist ≤ −96 dBFS at every note; Low Note Guard
measurably reduces upper-harmonic energy below the guard pitch; CPU ≤ 1 % of one core per voice @
48 kHz for 64 partials. Evaluation: static notes across four octaves, pitch bends and automated sweeps
rendered and inspected.

---

### Phase 2: Edge Excitation & Harmonic Fold

**Status: ✅ COMPLETE (2026-10-11)** — see specs/profundum-phase2-edge-fold/compliance.md (78 of 79; the one red row,
`VoragoMacro_SweepAxes`, is an INHERITED Vorago `[long]` failure: identical on `main` at 89b512d1 before Phase 1 and in
Long Tests Nightly 38046177066; not counted as a Phase 2 pass, remediation tracked on the Vorago branch).
Shipped: `PolyphaseHalfbandOversampler` (L1, polyphase allpass halfband 2×/4×, inline allpass, heap-free),
`HarmonicFold` (L2: soft saturation → envelope-normalised harmonic compression with per-channel windowed true-peak
detectors → sine fold, `kFoldIndexMax` 3.0 with the ease-out index law p = 1.5) and the edge excitation inside
`ProfundumCore` (`Sigmoid::recipSqrt` with closed-form first-order AA, drive −12…+12 dB dB-linear in Edge, static makeup
±1.5 dB, 500 Hz split bypass so h1 stays on a zero-delay path, 4× oversampled residual with 2fs notches and a 2 Hz DC
blocker). Rulings during the build: D-1 ease-out fold-index law; D-2/D-3 SC-026 measured on h1 (0.07°) and the 125 Hz
band, high-band delay recorded (≈ 5 samples); D-4 true-peak detector with 0.5 % release hysteresis (the windowed peak of a
non-integer-period signal is not constant; ratified by the user 2026-10-11 after a 180-cell stability grid, worst ripple
1.25e-5, and an amplitude-programme tracking test within 0.56 % of the detector's law). Measured: aliasing ≤ −83 dBFS at E0–E2 across 44.1/48/96 kHz (bar −80);
DC ≤ −109 dBFS; excitation THD span +31 dB; Fold THD strictly monotonic, harmonic count 1 → 17; regime continuity ratio
1.010 (bar 1.1); mix 0 bit-identical to Phase 1; CPU alone: core 0.689 %, stage cost 1.019 % (bar 1.5), chain 1.708 %
(bar 2.5). Noted for Phase 9 listening (characterised, decision deferred): the split's recombination LP·(1 − Dos) + Dos puts a
+1.2…+1.6 dB hump at 500–550 Hz (44.1/48 kHz; +0.7…+0.8 dB at 96 kHz) on the excitation's linear path, independent of
pitch and present at any mix above the engage ramp; `evidence/splithump.js` and SC-022(j) reproduce it.

**Spec:** `profundum-phase2-edge-fold`
**Goal:** The two nonlinear generators the core needs: new harmonics from nonlinearity (inside the
oscillator) and the signature "filthy midrange, pristine sub" saturator.

New components:

- **Edge excitation** inside `ProfundumCore` — an optional, continuously blendable oversampled soft-clip
  stage (`TanhADAA` / `Oversampler`) whose drive is influenced by Edge. Gives Edge its second capability:
  generating harmonics rather than only raising existing ones, completing the concept's progression
  (*asymmetric → clipped → increasingly complex*: an asymmetry control adds even harmonics, drive adds
  clipping). Band-limited; worst case is the lowest notes at maximum drive.
- `HarmonicFold` (L2, `processors/harmonic_fold.h`) — one 0–100 control crossing three regimes with
  C⁰-continuous output: soft saturation (low) → harmonic compression (middle; defined in spec — the
  candidate is dynamic limiting of upper-harmonic growth so density rises without level) → alias-controlled
  folding (high; `Wavefolder`/`WavefolderProcessor` math). Oversampled or ADAA, DC-safe. The Anchor never
  passes through it. **It must not behave like conventional waveshaping** (concept §7): the middle regime
  is what makes it different — harmonic density rises before level or crest factor change — and the
  descriptor trajectory over 0–100 must differ measurably from a plain tanh drive sweep.

**Success criteria:** harmonic count and THD monotonic with amount for both stages; even-harmonic
energy monotonic with asymmetry; Fold's descriptor trajectory distinct from a tanh drive sweep; aliasing
≤ −80 dBFS at E0–E2 with maximum drive/fold (`testing-dsp-analysis` method); regime transitions produce no level
or spectral jump (descriptor continuity over the full sweep); DC ≤ −80 dBFS; transparent at zero (null
within tolerance); CPU ≤ 1.5 % per voice both stages on.

---

### Phase 3: Sub Anchor

**Status: ✅ COMPLETE (2026-10-12)** — see specs/profundum-phase3-anchor/compliance.md (87 pass, 1 partial, 0 fail; the partial is
SC-003's ISA clause: every bar is green on every ISA measurable here — AVX2/SSE2 under MSVC, AVX2/SSE4/SSSE3/SSE2 under WSL GCC —
and macOS / ARM NEON wait for the nightly `[long]` lane after push). Shipped: `AnchorOscillator` (L3, `systems/anchor_oscillator.h`,
header-only, includes L0–L2 only): sine / triangle (16 odd terms n ≤ 31, cap-tapered, Clenshaw, THD −18.335 dB) / rounded sine (h3
−12 dB) / asymmetric sine (h2 −12 dB, one-period onset ramp) / octave-down; modes LOCK / FOLLOW / PUNCH / GHOST with raised-cosine
and Hermite-bridge C¹ envelopes (124 renders, 0 clicks); a bit-faithful mirror of the core's float h1 coefficient chain (drift
≤ 0.123° over 60 s on MSVC, 0.005° on GCC, bar 0.5°; FR-032's observer fallback not invoked); FOLLOW = engine-derived ratio r with a
16-sample glide (±0.0001 cent, latency ≤ 30 samples); `level` 0–1 relative to the core's delivered h1 (k = 1, default 0.5, RMS rise
≤ 3.52 dB); re-arm as its own event; strictly mono (200 renders, 0 L ≠ R); aliasing −148 dBFS, DC −112 dBFS; CPU alone and pinned
0.141 % worst cell (bar 0.3 %). The pre-Fold feed passed its audibility gate (THD −4.5 … −8.5 dB, centroid −0.38 … −0.86 oct) and ships
as Phase 9's `anchorFoldFeed` (default 0); the FR-066 handoff clauses are written into Phases 4, 5, 6, 8, 9 and 11 below.
Rulings during the build (all user-ratified, spec Clarifications 2026-10-11/12): the 14 plan S1 items (C-1 … C-14); SC-009's 1 dB
coherent-sum bar re-derived from Phase 2's ratified −12 dB Fold-h1 clause (coherence within 0.1 dB of the prediction plus a 4.1 dB
bound; measured −2.3 … +1.3 dB on the sum against −5.3 … +2.5 dB core-only, the Anchor's own sub energy identical in both chains);
SC-022(2) one-sided (floor −1 dB, ceiling +6.27 dB; feed 1 raises the summed fundamental +3.95 / +4.41 dB because the Anchor's h1 passes
the Fold and adds to its direct copy); SC-003(e)(ii) recorded, not gated (0.59° at C3 over 60 s is Phase 1's inter-partial float
phase walk seen through the Fold — `evidence/drift-probe.cpp`: the Fold on a periodic input drifts 0.0001°, the core's partials up to
8.35° / 60 s — the user notes the slow "breathing" may be musically welcome; Phase 9's soak records it, listening decides).

**Spec:** `profundum-phase3-anchor`
**Goal:** The fundamental that cannot be destroyed. A per-voice, always-mono anchor oscillator with
behaviours Pure SUB's sub-regen does not have.

New component (L3, `systems/anchor_oscillator.h`):

- Waveforms: sine, triangle, rounded sine, asymmetric sine, octave-down sine. Phase-locked to its
  voice's core h1 (shared `PhaseAccumulator` or locked at note-on — the `SubOscillator` divider idea),
  so it stays mathematically stable however the main voice is processed.
- Modes: **LOCK** (fundamental level and character constant), **FOLLOW** (tracks the processed voice's
  *perceived* fundamental — mechanism is Open Question 1: `YinPitchDetector` + `SubharmonicValidator` on
  the processed voice vs an engine-derived estimate), **PUNCH** (temporarily louder during the attack),
  **GHOST** (absent during the attack, emerges underneath over a settable time — milliseconds to tens of
  milliseconds, so an aggressive transient-heavy bass gets its *actual* sub a few ms later).
- Strictly mono: L == R bit-identical, no modulation, unison or phase tricks. Excluded from Fold, Shape
  and Character by default; the default join point is after Character (an advanced pre-Fold feed is
  Open Question 4).
- One instance per voice (POLY ruling): overlapping notes never share an anchor.

**Success criteria:** THD per waveform; zero phase drift against the core h1 over a 60 s render;
PUNCH boost amount/duration and GHOST emergence time measured against their parameters with C¹
envelopes; L/R bit-identity asserted; FOLLOW tracking accuracy and latency on a heavily folded voice;
anchor audible through maximum Fold + Character (sub-band energy preserved within 1 dB); CPU ≤ 0.3 % per
voice.

---

### Phase 4: Drop & Note Behaviour

**Status: ✅ COMPLETE (2026-10-14)** — see specs/profundum-phase4-drop-note-behaviour/compliance.md (86 pass, 0 partial, 0 fail after the
2026-10-14/15 closures: SC-020's inherited `VoragoMacro_SweepAxes` red — Vorago's own engine-limited Movement rho, ruled 2026-10-08 —
is now recorded by Vorago's test instead of failing, so the long lane is green; SC-021's listening verdict is 4 of 4 after the
kick-bass hybrid preset was re-authored from A 48 to A 24 st, the user having heard the four-octave snap as a squeak). Shipped: `DropCurve` (L0), `RetriggerDeclick` (L1, 1.5 ms raised-cosine dip, deferred Reset), `DropEnvelope` (L2,
Amount 0–48 st, Time 1–4000 ms, Shape exp / lin / log / elastic damped oscillation, Rebound), `DropPhaseLock` (L2 corrector inside
the Drop, γ ≤ 48 st, neighbour-integer target, GL8 quadrature with Illinois continuation when a solve is unconverged, staircase-aware
predictor, per-hold mirror), `NoteMemory` (L2, four modes × Retrigger, Continue collapse accepted), `MonoHandler` GlideMode
{ConstantTime, ConstantRate ms/octave, Quantized} append-only (Ruinae / Seraphis / Vorago / Innexus bit-identical). Rulings during
the build: SC-015 re-ruled to the rendered aliased-power measurement (O-1), no correction ceiling below 48 st (O-4); FR-070 / SC-028
— the bank re-rolled partial amplitudes by up to ±11 dB after large pitch motion (Phase 1 latent defect), fixed opt-in and enabled by
the core; SC-026 scoped to the Anchor's pitch / phase / mode gain with the waveform gated after dividing out the level feed (the raw
level swing on hard snaps, worst −8.6 dB at E0 A 48 T 10 ms, is a Phase 9 listening item); SC-010(a) recorded, the analytic
sample-delta bound (b) is the click gate; SC-014's dip reference is the Continue one-period RMS; SC-019 levers within the existing
bars, verdict the median of three isolated lanes. Measured: SC-013 landing |θ| ≤ 0.534° (bar 1°), drift ≤ 0.047° (bar 0.5°); FOLLOW
0.010°; SC-015 worst −123.65 dBFS (bar −96); SC-010(b) worst ratio 0.85 (bar 1), positive controls 1.99 / 1.99 / 2.11; SC-028 spectrum
across a 48 st Drop within 0.1 dB; SC-019 **0.0286 %** alone at close (median of three lanes 0.0357 %, bar 0.05 %), trigger-cost
ratios 1.06 / 1.17 (bars 2 / 1.5); use-case renders pairwise distinct (start pitch ≥ 12 st or t_s ratio ≥ 3.2 against bars 6 / 2).
Workflow lessons encoded: resume keys are chain-hashed (prompt edits mid-run re-run the tail), gate targets derive from the test TUs,
remediation triages wording / evidence / behaviour.

**Spec:** `profundum-phase4-drop-note-behaviour`
**Goal:** The pitch exciter and the recurring bass-instrument problems made explicit. DROP exists for
808 attacks, cinematic bass impacts, techno bass plucks and synthetic kick/bass hybrids — without turning
the instrument into an 808 clone (it is one stage of the voice, not the identity).

New / extended components:

- `DropEnvelope` (L2, `processors/drop_envelope.h`) — **Amount** (how far the pitch falls), **Time**,
  **Shape** (exponential ↔ linear ↔ logarithmic ↔ elastic, continuous), **Rebound** (brief overshoot
  past the target before settling). C¹ output, retriggerable with continuation, no allocation. Reusable
  (Membrum's local `PitchSegmentEnvelope` stays where it is — not generalised speculatively).
- `MonoHandler` extension (append-only, `processors/mono_handler.h`) — glide as constant-time,
  constant-rate, or **quantized** (semitone-stepped); existing Ruinae behaviour unchanged.
- **Note Memory** policy — always retrigger / legato / envelope-retrigger-only / oscillator-continuous,
  driving the core's retrigger phase policy (Phase 1 input) and the amp/drop envelopes. Note Memory
  decides when the Anchor's re-arm event is issued (legato, retrigger, overlapping notes, voice reuse);
  the core and the Anchor share a common phase reference at the resolved note pitch before Drop
  (Phase 3 FR-036, FR-048).
- **Retrigger** — oscillator and anchor phase behaviour on new notes (reset / continue), click-free by
  construction.

**Success criteria:** curve-shape tests per shape family (fitted exponent / linearity metrics); rebound
overshoot amount and settle time measured; quantized-glide step timing; a legato × retrigger × note
memory matrix with expected envelope/phase behaviour asserted; no clicks on retrigger at any policy
(max sample delta bound); `ruinae_tests` green after the shared-header change. Evaluation: the four
use-case renders (808 attack, cinematic impact, techno pluck, kick/bass hybrid) inspected.

---

### Phase 5: Shape & Character

**Status: ✅ COMPLETE (2026-10-17)** — see specs/profundum-phase5-shape-character/compliance.md (89 pass, 0 partial, 0 fail after the
2026-10-16/17 close: eight user rulings recorded in their FR/SC rows first — Crush m_min 3, one-period pre-roll with DC prime at every
branch start and bypass exit, SC-012(f) and SC-003(a) scope under the C-3 / Crush precedents, the Rectify descriptor rule at −20 dB h2,
Fracture observer edges from the Fold-100 sine ∩ triangle bands, a restartable 10 ms raised-cosine SHAPE ramp, and FR-064's
append-only fallback used once for `DCBlocker::setState`; two root fixes in ShapeMorph's regime handover; the harvested loudness and
flip-edge tables pasted; SC-015 pinned at 1.85 % of one core against 2.0 %). Shipped: `ClosedFormAdaa2` (L0), `KeytrackedTilt` and
`OversampledResidualPath` (L2; 4× halfband, three 2fs notches and a 2 Hz blocker on the residual only), `Rectifier` (L2, half/full-wave
blend in double ADAA2, sine makeup), `ShapeMorph` (L3: ROUND / HOLLOW / BITE behind one SHAPE, linear weights after the filters, HOLLOW's
linear chain warm from prepare, equal-loudness table harvested, BITE's emphasis inverse on HOLLOW's tap), `BassCharacterStage` (L3: Tube,
Fold, Rectify, Bite, Crush, Fracture with Amount + Tone each; keytracked tilts; pitch-synchronous noiseless Crush hold with a per-slot
hysteresis quantiser; Fracture = polarity alternation + bias-flip clip on the f₀/2 lattice; `perceivedPitchRatio()` with hysteresis for
Rectify and Fracture; `syncPhase(coreCycles)`; 10 ms algorithm crossfade preceded by a one-period pre-roll; L/R-capable offsets).

**Phase 9 handoff (plan S13; compliance "Remaining gates"):** (1) a note that has been through a Drop keeps partial phases unlike a
fresh note's, so every waveform-dependent stage sounds different after a Drop — a listening item and a possible Phase 4 amendment;
(2) the voice calls `syncPhase` at note-on, retrigger and bypass exit, and an algorithm switch takes 1/f₀ + 10 ms, a bypass exit
1/f₀ + 5 ms; (3) kGrowl reads 2·f₀ to the estimator and every nonlinear stage (and most SHAPE positions) restores f₀ — FOLLOW takes r
from the engine, never from processed audio; (4) Rectify's descriptor: `setInputHalfWaveSymmetric(true)` when the Rectify input's h2 is
below −20 dB re h1 (−24 … −16 dB is the documented uncertainty band); (5) Fracture's observer is stated for Fold-100 sine / triangle
inputs and lags the audio by up to 0.17 Amount on saw-like voices at Tone ≥ 0.75; (6) the FR-040 h1 transfer table, the C-15 group
delays, the ShapeMorph growl level swing (C-6) and the crossfade CPU cell (2.65 %). **Phase 7 handoff:** the SC-013(b) offset ceilings
(Rectify and Fracture at 0.10; evidence/sc013-offset-ceilings.txt) and the ±0.15 readings. **Phase 8:** SHAPE, Amount, Tone and the
algorithm are the morphable fields; transitions only through `setAlgorithm`.

**Spec:** `profundum-phase5-shape-character`
**Goal:** The replacement for a filter section and for an FX rack.

New components:

- `ShapeMorph` (L3, `systems/shape_morph.h`) — three character processors behind one continuous
  **SHAPE** control: **ROUND** (low-pass-like: `LadderFilter`/`SVF` LP with gentle drive), **HOLLOW**
  (band/reject/resonant behaviour: `SVF` notch/band + resonance), **BITE** (high-frequency emphasis +
  nonlinear excitation: tilt/shelf + `DiodeClipper`-style asymmetry). Equal-loudness crossfade between
  regimes; internally rich, externally one knob.
- `BassCharacterStage` (L3, `systems/bass_character_stage.h`) — six algorithms, each with only
  **Amount + Tone**: Tube (`TubeStage`), Fold (`WavefolderProcessor`), Rectify (🆕 `Rectifier`, L2:
  half/full-wave blend, DC-blocked, anti-aliased), Bite (tilt + asymmetric clip), Crush
  (`BitcrusherProcessor`), Fracture (defined in spec — Open Question 5; candidates: `FeedbackDistortion`,
  rate-reduced fold). Algorithm switches are click-free (crossfade); the stage is L/R-capable so Phase 7's
  WIDE mode can offset drive/tone per channel. The Anchor bypasses it by default. Rectify, Crush and
  Fracture each ship an r-law deliverable: the perceived-pitch ratio r = f_perceived / f0 as a function
  of the stage's controls with a measured reference, or r ≡ 1 recorded with evidence (Phase 3 FR-047).

**Success criteria:** each algorithm's harmonic signature measured and pairwise distinct (descriptor
separation ≥ the bar ruled in spec — audibility gate); Tone monotonic in spectral centroid for every
algorithm; SHAPE sweep continuous (no level/descriptor jump between regimes); algorithm-switch sample
delta bounded; aliasing ≤ −80 dBFS at E0–E2 for every algorithm at maximum Amount; DC ≤ −80 dBFS;
CPU ≤ 2 % per voice with Shape + Character on.

---

### Phase 6: Mass & Dynamics

**Status: ✅ COMPLETE (2026-10-18)** — see specs/profundum-phase6-mass-dynamics/compliance.md (77 pass, 0 partial, 0 fail after the
2026-10-17/18 close: twelve user rulings recorded in their FR/SC rows first — the wide-arm detector bar, resampled period descriptors, the
SC-019 chain recovery point, Depth and Anchor travel as harvest levers, w₁ measured at the fold + anchor sum tap, the Anchor keyed to the
recipe's **unbiased** h1, SC-001(c) as a bounded fall (≥ Mass 0 − 6 dB), the **output trim as the law's seventh calibrated target**, the
sine fixture recorded for SC-003 / SC-015, the Fold-100 fixture recorded for SC-010, the 7.5 ms E1-sine duration cell recorded, the
TruePeakLimiter clamp ratified, and the SC-020 gate scoped to the change set; main-loop refinements: harvest |ΔP| at every Mass unit
(Fold row bent to SCurve), positive control on Fold-100, run-time `std::log2` / `std::exp2` dB forms (SC-017 1.27 % → 0.991 % alone and
pinned), the SC-005(c) smoke lattice). Shipped: `PeriodPeakDetector` (L2; HarmonicFold's held true-peak as a public component),
`TransientShaper` (L2; held true-peak → fast / slow Amplitude followers, c = clamp(20·log10(f/s) / 12), gain = A·c + S·(1 − c), duration
through a harvested slow-attack table 5…250 ms, slow follower ticked /8), `BassCompressor` (L3, in `systems/mass_dynamics.h`; keytracked
2·f₀ high-pass → true-peak → max(L, R) → one-period hold → one `DynamicsProcessor` gain computer, five-row harvested amount table
(threshold −11.0 dB, makeup +5.84 dB at amount 1), D-5 program-dependent release 50 + 450·f ms from a 300 ms rule ring, no look-ahead),
`MassDynamics` (L3; shaper → compressor → output trim, 32-sample control grid), `MassLaw` (L3; seven monotone rows Depth, Body,
TransientDuration, FoldAmount (SCurve), CompressionAmount, AnchorLevel, OutputTrim; 40 ms traversal slew; pure `evaluate`), and the
validation chain fixture (core → fold → + anchor keyed to the unbiased h1 → MassDynamics → DC → limiter) with the BS.1770 K-weighting helper.

**Phase 9 handoff (compliance "Remaining gates"; evidence/phase9-handoff.txt):** (1) the voice feeds the Anchor the recipe's h1 for its
own controls **before** the Mass biases (`unbiasedH1Amplitude`); (2) the output trim is a dB gain after the compressor inside
`MassDynamics` (`setOutputTrimDb`, 0 dB at Mass 50, −2.05 dB at Mass 0 harvested), one of the seven law targets; (3) a sub-only voice is
invisible to the 2·f₀ sidechain: it receives the full +5.84 dB makeup unopposed and sits on the limiter at Mass 100 (steady GR −0.18 dB
at E1), while the reference and saw voices hit the limiter 5.8–7.9 dB for about 0.5 s at the note attack — listening items; (4) Fold-100
input through the dynamics folds gain-modulation sidebands at −63 … −78 dBFS and the 4× true-peak estimate wobbles with sub-sample phase
at E2 (steady −46 / −55 dBFS at 44.1 / 48 kHz) — recorded; Phase 9's chain budget decides 2× oversampled gain application plus 8×
detectors (≈ +0.5 % of a core); (5) `setDetectorKeyHz(lowestSoundingF0)` once per 32-sample interval, 0 = no voice → 80 Hz; (6) the
Mass dynamics cost 0.991 % of a core alone (margin 0.009 %), `DCBlocker` ×2 + `TruePeakLimiter` 1.23 % recorded for the engine budget —
a run-time fast path in `db_utils.h` (its constexpr series cost ≈ 24 ns per call under MSVC) is the lever if Phase 9 needs it;
(7) attack / sustain gains are user-facing (Phase 11, 700–799; defaults +6 / 0 dB), duration is Mass-driven; (8) 4 detuned voices at
Mass 100 read −10.1 dB sub-band GR on the shared compressor (recorded). **Phase 7:** nothing in the dynamics is stereo-aware beyond
max(L, R) detection and one shared gain; SPREAD's side content passes the compressor unchanged. **Close-lane rule (G9-4):** the long
lane lists the phase's own TUs plus the suites of consumers of a modified shared header, never every `[long]` case of the executables.

**Spec:** `profundum-phase6-mass-dynamics`
**Goal:** The macro that lets a user think "20 % more mass" instead of "which of 47 parameters".

New components:

- `TransientShaper` (L2, `processors/transient_shaper.h`) — attack/sustain gain shaping from a
  fast/slow `EnvelopeFollower` pair; this is what "transient duration" in the Mass law needs. None exists.
- **Bass compressor configuration** of `DynamicsProcessor` — program-dependent release, sidechain
  high-pass (`SidechainFilter`) so the sub never pumps the gain computer.
- **The Mass law** (L3 data + mapping, `systems/mass_law.h`) — a documented, monotonic, calibrated
  multi-target map from one 0–100 control to: fundamental strength (recipe Depth bias), low-mid harmonic
  density (Body bias), transient duration (shaper), saturation (Fold amount), compression amount, and
  the sub/body relationship (anchor level). Mass 0 = pure, sine-like; 50 = thick analogue; 100 =
  enormous distorted. Continuous and predictable by construction: every target monotonic, bounded slew.
  The voice supplies the core's h1 amplitude per control block (`deliveredGains()[0] · kCenterPanGain`,
  Phase 3 FR-023), and the summed core + Anchor signal is validated through Mass dynamics and the output
  safety stages, not by compensation in the Anchor (FR-025).

**Success criteria:** every Mass target monotonic over the sweep; perceived level slope bounded (Mass is
not a volume knob — ruled dB range in spec); sub-band gain modulation depth under compression ≤ a ruled
bound (no pumping); transient attack/sustain gains measured against parameters; no discontinuity in any
descriptor across 0–100 at automation speed; CPU ≤ 1 % global.

---

### Phase 7: Bass Stereo Field

**Status: ✅ COMPLETE (2026-10-19)** — see specs/profundum-phase7-stereo-field/compliance.md (80 pass, 0 partial, 0 fail after the
Group 10 close rulings). Built on branch `synth/profundum`. `BassStereoField` (L3, `systems/bass_stereo_field.h`): pan-only width
law (raised cosine in log f, `curve` warps the ramp, Gravity sets the lower knee), parity panning with h1 centred, FR-015 slew
(0.003 per interval, exact landing within 334 intervals), MOTION Drift + Cycle (inward depth, octave-folded sync), WIDE offsets
{+K, −K} engaging over the slew length, and the post-sum safety stage (M/S, side-only min-phase LR4 at 2·f_c, zero latency,
side gain g_floor·(1 − Gravity) → 0). Harvested: g_floor = 0.035 (−29 dB), K_wide = 1.0. Rulings: C-1/C-2/C-17 (float
bit-identity read as bounds), C-9 (sync fold), C-10, C-15, C-18, G7-1 (long-ramp click form), G7-2 (offset engagement ramp),
G8-1 (deep floor; octave Q recorded on nonlinear arms; anti-phase cells recorded), G10-1 (SC-003 dirty-arm octaves recorded)
and the G8/G10 measurement readings, all in the spec's Clarifications.

**Phase 8 / 9 / 11 / 12 handoff (evidence/phase-handoffs.txt; FR-080):** (1) `Controls` is the morphable set; mode is stepped
through the 10 ms side crossfade plus the vector slew (measured 9.98 ms); (2) engine call order per interval: advanceControl, then
per voice note-on → computePanVector → setPartialPanOffsets, wideOffsets → setChannelOffsets; the stage sits after the Mass
dynamics; setTempoBpm from the process context; (3) per-voice CPU line "law + pan forwarding" 0.198 % (four voices 0.692 %),
stage 0.07–0.10 %; (4) the chain converges to L == R after a SOLID switch (DC and limiter states; re-sync is Phase 9's call);
(5) the sub band of a nonlinear arm is protected by the floor, not by coherence: Fold / Rectify at offset ceilings invert the
fundamental between channels (9 cells per arm recorded) and the Anchor restores the sub — a listening item; (6) synced Cycle
folds note values above 1 Hz onto one rate (Phase 11 / 12 state the aliasing); (7) Gravity acts only where harmonics lie below
2·f_c (inert at C3 with the default crossover, by design); (8) the Phase 6 limiter leaves a −45 … −55 dBFS anti-phase residue
above the partial range (recorded, Phase 9 output chain item).

**Spec:** `profundum-phase7-stereo-field`
**Goal:** Reese-like width and motion without the sub ever becoming unstable — in the generation
architecture, not as an end effect.

New component (L3, `systems/bass_stereo_field.h`):

- **Image modes**: SOLID (mono voice, maximum predictability), **SPREAD** (default: centred Anchor,
  stereo harmonic voice), WIDE (more expansive processing, Anchor still centred).
- **SPREAD** — frequency-dependent width implemented primarily *in generation*: a per-partial
  pan/decorrelation vector for the core bank (Phase 1 hook) where partial width = f(partial frequency;
  crossover frequency, width curve). Zero = everything centred. Advanced: crossover and curve.
- **GRAVITY** — how aggressively the field collapses to mono as frequency falls: the steepness of the
  generation-domain law *and* a post-sum safety stage (`CrossoverLR4` + `MidSideProcessor` side
  attenuation below the crossover) that catches side content created by the nonlinear stages. The
  post stage exists because distortion makes new harmonics on both sides of any boundary; its phase
  behaviour is measured, not assumed (Open Question 6).
- **MOTION** — slow rotation / oscillation of the field (`BrownianDrift` or synced `LFO` on the pan
  vector / M-S rotation), bounded and slow.
- WIDE adds small per-channel drive/tone offsets in the character stage. The Anchor never enters any of
  this.

**Success criteria:** mono-sum test per band — summed level within a ruled tolerance of the L/R energy
(no cancellation) below the crossover, sub-band inter-channel correlation ≥ 0.99 in every mode; width
(inter-channel correlation / side energy) monotonic in both frequency and SPREAD; SOLID output is L == R
bit-identical; distortion-born side content below the crossover attenuated ≥ a ruled dB figure by the
safety stage; MOTION rate and depth bounded; CPU ≤ 0.5 % global.

---

### Phase 8: Bass Morph

**Status: ✅ COMPLETE (2026-10-19)** — see specs/profundum-phase8-bass-morph/compliance.md (80 pass, 0 partial, 0 fail after the
build rulings). Built on branch `synth/profundum`. `MorphState` (L3, `systems/morph_state.h`): 19 members, 62 leaf fields
(the four earlier `Controls` structs by value plus Fold, SHAPE, Character, Drop, `AmpEnvelopeFields`, `MotionConfig`, Mass and
transient gains, Anchor and `anchorFoldFeed`), a 62-row field-descriptor table (law, axis, group, range, byte offset) that the
serializer and the resolver both read, 201-byte versioned little-endian serialisation, `isValidMorphState`.
`BassMorphEngine` (L3, `systems/bass_morph_engine.h`): A / B by value through whole-endpoint `setEndpoint` (Q8), a
rate-limited position (user ramp 5–2000 ms, default 40 ms, Q3; 0.5·ulp(p) term in the step bar, B3-1), laws evaluated in
double and rounded once (C-4): linear / logarithmic (frequencies and times, incl. the stereo crossover) / equal-power /
stepped; stepped fields select per governing-amount group (Q1 amount-aware selection, C-2: Character, ImageMode, FieldMotion,
Motion at p, Anchor at p_a) with 0.52 / 0.48 hysteresis and identity by selection at 0 / 1; Anchor morph modes Lock (whole
Anchor block held at A, Q5) / Blend / Punch (p³ fixed, Q6) on p_a with `anchorMass` (FR-054); a 20-byte runtime blob
(target, ramped p and p_a, mode, ramp time, group-mask and Anchor selections) restored without reset; no audio path.
Showcase pair B harvested (B7-2, 96 candidates × 3 runs): kHeavy edge 1, Fold 80, SHAPE 0.5, Tone 0.8, Character Amount
0.5, Anchor level 0.5. Rulings beyond the eight at specify: C-2, C-4, C-16, deviation 10, B3-1, B7-1 (floor at the resolved
Anchor level), B7-2, B9-1 (Mass morph gated on Phase 6's SC-011 bar), B10-1 (clicks ≤ the direct-setter reference's own),
B10-2, B10-3 (SC-006(b) pairs constrain A's Fold to the soft regime), B10-4 (D_st on the core tap, D_t on the sum tap); reader
refinements (E_sub on f₀, D_nl against a linear twin core, −60 dB relative descriptor floors, Phase 7 band validity on the
core-tap D_st, 8-period SC-017(b) window), all in the spec's Clarifications. CPU: `advanceInterval` 239 ns per interval
(0.036 % of a core, bar 0.05 %).

**Phase 9 / 11 / 12 handoff (evidence/phase-handoffs.txt; FR-083):** (1) `advanceInterval` once per 32-sample grid point,
before `BassStereoField::advanceControl` and every voice's setters; (2) the FR-054 Mass split: `MassLaw`'s AnchorLevel row at
`anchorMass`, every other row at `mass`; (3) change-only setters for the amp-envelope curves (`ADSREnvelope::setAttackCurve`
rebuilds a table); (4) `AmpEnvelopeFields` / `MotionConfig` are final for version 1 — Phase 9 appends under a version bump
and records any field it cannot realise; (5) the worst transition interval with all four groups switching at 4 voices measured
81.6 % of a core (fixture overhead included, 30.8× Phase 5's 2.65 % crossfade cell) — Phase 9 rules against its 8 % / 25 %
ceilings; (6) MotionConfig selectors' click-free gate is Phase 9's once the Motion component exists; (7) Drop morphs act on
the next note; (8) join-point finding: the nonlinear chain regenerates a fundamental whose phase against the Anchor rotates
with settings and note (θ −149° … +179°, 0.27–8.3× the Anchor), Phase 3's "add coherently" holds at the core tap only; (9)
component switch clicks the morph does not add (Fracture → Crush / Rectify at C3; OctaveDownSine → X at E1, 5 ms ramp); (10)
HarmonicFold's detector hysteresis latches a path-dependent envelope above kSoftEnd (0.13 dB on upper harmonics, non-decaying);
(11) the true-peak limiter engages from p ≈ 0.8 on the showcase at Mass 50 and the WIDE output's side already equals its mid —
engine-level monotone claims read the sum / core taps; (12) Phase 11: 422 bytes of state per instance, restore order
`setEndpoint` × 2 then `restoreRuntime`, Edit A / Edit B routing, IDs 100–199 and 1000–1099; (13) Phase 12: the law table and
the Anchor curves are "what the user sees while morphing"; while Lock is active B's Anchor edits are stored but inaudible.

**Spec:** `profundum-phase8-bass-morph`
**Goal:** The killer feature — travel between two complete bass identities while the fundamental
never disappears.

New components (L3):

- `MorphState` (`systems/morph_state.h`, plain data) — every sound-shaping control: core Depth / Body /
  Edge / Shift, edge excitation, Fold, Shape, Character algorithm + Amount + Tone, Drop, amp envelope,
  image mode / Spread / Gravity / Motion, Motion-section configuration, Mass, Anchor waveform / mode /
  level / `anchorFoldFeed`. Excluded: note behaviour, output level, polyphony. Serialisable, validated (`isValidMorphState`).
- `BassMorphEngine` (`systems/bass_morph_engine.h`) — holds A and B, resolves one state per control
  block from a smoothed, automatable morph position:
  - per-parameter interpolation laws: linear; logarithmic (frequencies, times); equal-power (mixes);
    stepped-with-crossfade or algorithm-specific transition (character algorithm, image mode, anchor
    waveform — the Disrumpo `MorphEngine` family-crossfade idea);
  - the core's spectrum is derived from the *interpolated* D/B/E/S (recipe domain) — never an audio
    crossfade, which would cause phase cancellation and loudness dips;
  - the engine understands what is being morphed (concept §10): each of the concept's five perceptual
    axes has a named law — **sine → complex oscillator** (recipe domain), **clean → nonlinear** (Fold,
    excitation and Character amounts with algorithm crossfade), **mono → stereo** (image mode stepped,
    Spread / Gravity / Motion continuous), **short → long** (envelope and Drop times logarithmic),
    **dark → bright** (Shift / Edge / Shape / Tone in their own domains);
  - **Anchor morph modes**: Lock / Blend / Punch (designed curve: weight retained early, yielding late).
    The concept's example — main voice 0 → 100 % while the anchor travels only 100 → 70 % — is the Blend
    mode with per-endpoint anchor levels; Punch is the asymmetric-curve generalisation;
  - global: one morph position, all voices follow.

**Success criteria:** morph 0 reproduces A and 1 reproduces B exactly (parameter identity); bounded
per-block change of every resolved parameter at any morph speed including 0 → 1 jumps (the position is
ramped); discrete transitions click-free (sample delta bound at the crossfade); Anchor mode curves
measured; A → B → A idempotent; both states and the position serialise round-trip; descriptor trajectory
along the morph is monotonic on each of the five perceptual axes for a pure-weight → harmonic-monster
pair (no loudness dip ≥ a ruled dB, sub-band energy never below the Lock floor).

---

### Phase 9: Profundum Voice & Engine

**Spec:** `profundum-phase9-voice-engine`
**Depends on:** all above; pattern template from `seraphis_voice.h` / `vorago_engine.h` /
`vorago_macro_matrix.h`.
**Goal:** Compose everything into the playable instrument core and prove every capability audible
before a plugin exists.

- `ProfundumVoice` (L3) — core (+ edge excitation) → Shape → Harmonic Fold → Character (the concept's
  §13 order: spectral shaper → saturator → character, confirmed 2026-10-09); Anchor in
  parallel joining after Character; Drop on pitch; amp envelope (`ADSREnvelope` or `MultiStageEnvelope`,
  ruled in spec). Per-voice vs post-sum placement of Fold / Shape / Character in POLY confirmed by CPU
  measurement (Open Question 7; default per-voice so chords do not intermodulate). Voice-level send
  `anchorFoldFeed` ∈ [0, 1], default 0, feeding the Anchor into Fold with the direct path always
  present (Phase 3 SC-022).
- `ProfundumEngine` (L3) — **MONO** (default) via `MonoHandler` with the Phase 4 note behaviour; **POLY**
  via `VoiceAllocator`, 4 voices (`kMaxVoices` raisable), steal oldest-released then oldest-active;
  voice sum → Mass dynamics → stereo field safety stage → DC safety → `TruePeakLimiter`.
- **Motion section** — ENV (one flexible envelope), CYCLE (`LFO`, free-running or tempo-synced
  rhythmic), DRIFT (slow random, `BrownianDrift`) → `VoiceModRouter` as the modulation bus with a small
  fixed destination set and one global **Motion Depth** (the concept's global *Modulation Depth*);
  Motion can modulate Mass (basses that swell and contract).
- **Personality macros** via `ProfundumMacroMatrix` (pattern copy): **Weight** (sub / fundamental /
  body), **Dark ↔ Bright** (spectral distribution), **Tight ↔ Loose** (envelope, transient, compression),
  **Clean ↔ Violent** (nonlinear processing). Presets store these coordinates.
- Global Bass Morph feeding every voice; Mass law wired; determinism harness (seeded, measured
  tolerances).
- **Capability audibility table** — every section's on/off and every macro/lever moves a render
  descriptor ≥ the ruled bar on a documented showcase setting. This is the Vorago 13b–13d lesson moved
  to where it is cheap.

**Success criteria:** whole engine, everything on: ≤ 8 % of one core @ 48 kHz in MONO and ≤ 25 % at
4 voices (derived from per-voice budgets ≈ 4.8 % + globals ≈ 1.5 %; the ceiling is never relaxed to pass);
voice steal and MONO note replacement clickless; macro sweeps monotone along their documented axes;
Motion → Mass swell measured; the audibility table complete with no cell below the bar; determinism
harness; 30 min soak bounded with no DC build-up; mono-sum check on the full engine in every image mode.
Anchor sub energy (w1) within 1 dB through maximum Fold + every Character algorithm at maximum Amount, at
E0–E2 (Phase 3 D-7): the Anchor's own sub energy is the quantity within 1 dB; the voice's summed
fundamental is gated by the coherent-sum bound derived from Phase 2's −12 dB h1 clause (≤ 4.1 dB at
Anchor level 1, coherence within 0.1 dB of the prediction from the recorded core-only h1 change — Phase 3
SC-009 re-ruling 2026-10-12). FOLLOW tracks the perceived fundamental of a voice through a real
pitch-moving stage (accuracy ±5 cents, latency measured), and the capability audibility table carries a
FOLLOW row driven by that stage, or FOLLOW is flagged inert (Phase 3 Q1). The Anchor joins after
Character by default, the direct Anchor path is always present, and Drop is routed to the core only: the
Anchor receives the resolved note pitch before Drop in LOCK / PUNCH / GHOST and the core's post-Drop f0
in FOLLOW (Phase 3 Q3, Q4). The Anchor–h1 lock is measured in the 30 min soak with Motion modulating the
core spectral controls (Phase 3 C-5 residual). Inside the voice, the core's output is bit-identical with
the Anchor attached and detached, and the core + Anchor sum is a plain add with no compensation or
normalisation stage (Phase 3 FR-025, SC-024(d)).
**Phase 4 handoff (C-9, C-10, O-2; O-1 ruled 2026-10-12):** no slope bound is needed under the rendered gate; the
tail upswing figure 0.418 st/sample is recorded for Phase 9's own aliasing measurement.

---

## Part B — Plugin (plugins/profundum/)

Follows the Vorago Part B template nearly verbatim — those phases were specified against the same repo
infrastructure and their checklists apply directly.

### Phase 10: Plugin Scaffold

**Spec:** `profundum-phase10-plugin-scaffold`

- **Template: Vorago shape** (Ruinae-style instrument with `parameters/` packs, engine-config layer,
  preset browser) with **Membrum/Gradus bus config** (event-in + stereo-out, no `addAudioInput()`,
  `kSupportedNumChannels 02`, matching single plist config).
- AU identity: type `aumu`; subtype **`Prfd`** (taken: `Itrm Dsrm Ruin Innx Grad Mbrm Srph Vrgo`);
  manufacturer `KrAt`; bundle base `com.krateaudio.profundum`; two fresh FUIDs.
- Parameter ID base 0 with 100-ID section gaps: 0–99 global (gain, mono/poly, polyphony, limiter),
  100–199 Personality macros + Mass + Morph position, 200–299 core, 300–399 anchor, 400–499 drop / note
  behaviour, 500–599 fold / shape, 600–699 character, 700–799 mass dynamics, 800–899 stereo field,
  900–999 motion, 1000–1099 morph control (anchor morph mode, edit target).
- Full out-of-tree registration checklist from Seraphis Phase 8.5 (root CMake, ci.yml ~17 sites,
  release.yml, valgrind-nightly, both clang-tidy scripts, check-changelog-coverage, gen-specs-index,
  CLAUDE.md rosters + new leaf) — every item, day one.
- Day-one tests: bus setup, denorm round-trip, state round-trip, non-silent render, editor-lifecycle
  harness enrollment. Test target `profundum_tests`.

**Success criteria:** builds on all three OS legs, `profundum_tests` green, pluginval strictness 5
clean, `auval -v aumu Prfd KrAt` passes, check-portability clean, clang-tidy `all` picks it up in both
scripts.

### Phase 11: Full Parameter Surface & State

**Spec:** `profundum-phase11-parameters`

All engine parameters registered / denormalized / persisted with `kCurrentStateVersion`; the **morph
editing model** settled (Open Question 2: the registered sound parameters address the selected
endpoint via an Edit A / Edit B target, the engine runs the resolved state, both snapshots live in state);
per-section parameter packs; `IMidiMapping` (mod wheel and channel pressure → Mass by default,
pitch-bend range, sustain); pluginval + full round-trip tests including both morph states.
**Amended 2026-10-11 (UI design conversation):** Copy A → B / B → A and A / A·B / B compare are controller-side
operations on the two snapshots, not parameters (the compare must not write state); the Morph position ramp time
is registered if Phase 8 exposes it; the Motion target set registered in 900–999 covers Mass and the core spectral
controls first (no general matrix). **Amended 2026-10-12 (Phase 3 SC-022 PASS):** `anchorFoldFeed` is
registered in the 300–399 anchor block. **Amended 2026-10-13 (Phase 4 FR-027, SC-027):** "The single portamento knob is labelled per Glide
Mode: milliseconds for ConstantTime (the whole glide), milliseconds per octave for ConstantRate, milliseconds for Quantized
(the whole underlying ramp). `MonoHandler` has no second glide setter (Phase 4 FR-026)."

### Phase 12: UI

**Spec:** `profundum-phase12-ui` — one spec in two stages: **12a UI design** (deliverables ratified by the
user) and **12b implementation**; 12b does not start until 12a is ratified. *(Amended 2026-10-11 from the
user's design conversation; the concept §14 screen remains the reference the amendment is measured against.)*

**Design principle:** the UI follows the engine — a small number of powerful controls on the surface, the
mechanisms on an Advanced page. It must not read as a conventional subtractive synth (a row of oscillators,
filters and envelopes). What it shows is the relationship between a stable, centred low end and a
transformable harmonic voice. Two levels: one **Main** performance page a user can operate without
understanding the DSP, one **Advanced** sound-design page (whether MODULATION and SETTINGS are tabs of
Advanced or separate pages is decided in 12a).

**12a — design deliverables:**
1. Main-page wireframe. Working basis: the conversation's Concept A (functional, macro-first, Bass Morph as
   the visual centrepiece) with Concept B's terminology and visual split — **THE ANCHOR** (weight) and
   **THE MASS** (character) as two distinct regions. The page has three jobs: *shape the mass* (Depth, Body,
   Edge, Fold), *control the weight* (Anchor level, Anchor character, Anchor behaviour), *transform the
   sound* (Bass Morph, voice mode + glide, Motion mode).
2. Advanced-page wireframe, by section: **Core spectrum** (Depth, Body, Edge, Spectral Shift, Fold, the
   harmonic spectrum display); **Sub Anchor** (level, character/waveform, mode, morph behaviour);
   **Excitation & Fold** (edge drive, excitation mix, asymmetry, fold amount); **Pitch & Envelopes** (Drop
   amount / time / shape / rebound, amp envelope, note behaviour: retrigger, glide mode, note memory);
   **Stereo & Output** (image mode, Spread, Gravity / crossover, field Motion, output); **Morph behaviour**;
   **Motion** detail.
3. Macro-to-parameter mapping table — Phase 9's `ProfundumMacroMatrix` table reproduced and ratified as the
   UI contract. The main page must make a good sound with its visible controls alone.
4. Bass Morph interpolation specification — Phase 8's per-parameter laws restated as what the user sees
   while morphing.
5. Voice-mode and note-handling — a cross-reference to Phases 4 and 9 (deterministic, documented stealing
   and retrigger); 12a adds no behaviour.
6. UI interaction and automation specification: which controls are host parameters and which are UI-only
   state (A / B compare, copy state), automation and MIDI-learn behaviour of the macros and Morph.
7. Accessibility, scaling and host-resizing requirements.

**Main-page questions carried into 12a** (concept §14 vs the conversation; decided in spec, recommendation
first):
- **Knob set.** Concept: Weight · Body · Edge · Mass / Anchor · Shift · Fold · Drop. Conversation: Depth ·
  Body · Edge · Fold, the macros Weight · Dark ↔ Bright · Clean ↔ Violent, Anchor level / character /
  behaviour, voice mode + glide, Motion mode. Recommendation: the conversation's set **plus Mass and Drop**
  (both one-knob, both proprietary-feeling); Shift and Tight ↔ Loose move to Advanced.
- **Spectrum view.** Concept: main-page centrepiece with the draggable energy point. Conversation: optional,
  on Advanced. Recommendation: keep it on the main page — it is the instrument's signature visual and the
  Anchor / Mass split can be drawn in it (Anchor separately, Sub / Body / Presence energy as bars). Wherever
  it lives it shows the **actual harmonic structure** (DataExchange piggyback), never decoration.
- **Image-mode selector and the SPREAD mini-spectrum** (20 Hz – 10 kHz, MONO ↔ WIDE across frequency,
  concept §8): main page (concept) or Stereo & Output (conversation).

**Components the amendment adds:**
- **Morph component.** Endpoints carry a name and a three-word descriptor (e.g. *A — DEEP PRESSURE · Round
  · Clean · Heavy*, *B — IRON FOLD · Dense · Bright · Aggressive*), a position readout in %, **A / A·B / B
  compare** (UI state, not a parameter) and **Copy A → B / B → A** (variation workflow). Advanced "Morph
  behaviour": Anchor morph mode (Lock / Blend / Punch) and **Morph smoothing** (Phase 8's position ramp
  time, if Phase 11 registers it). *Not adopted:* a user-selectable global "parameter curve" — the
  per-parameter laws are fixed by Phase 8's ruling.
- **Motion.** Main page: source mode only (OFF / ENV / CYCLE / DRIFT). Advanced: source, target (Mass and
  the core spectral controls first), amount, rate, shape, sync + division; phase, retrigger, fade-in and
  polarity as advanced detail. No general modulation matrix in 1.0.
- **Macro discoverability.** Hovering or moving a macro highlights the parameters it drives; no permanent
  matrix on screen.
- **Anchor naming.** The Anchor's own modes (LOCK / FOLLOW / PUNCH / GHOST, Phase 3) and its morph behaviour
  (Lock / Blend / Punch, Phase 8) are two controls and never share one widget or one label set; 12a names
  them apart (the conversation's main-page "ANCHOR MODE LOCK / BLEND / PUNCH" is the morph behaviour).

**Interaction principles (binding on 12b):** main-page controls give musical results without DSP knowledge;
macros drive several parameters through intentional, documented mappings; Morph uses parameter-appropriate
curves; the Anchor's morph behaviour is explicit; per-voice envelopes and note state stay independent in
POLY; voice stealing and retrigger are deterministic and documented; **no Advanced control may silently
invalidate the stable, centred low end** — a control that affects mono compatibility says so and its
guarantee is the one Phase 7 measures; there is no "low-end protection" switch implying safety at every
setting; the spectrum display represents the actual signal.

**Not adopted from the conversation (conflicts with ratified design):** user-facing **Oversampling /
Quality** settings — the excitation and the Fold ship with fixed zero-latency anti-aliasing (Phase 2
SC-026 / SC-005), and a quality switch would ship either an inert path or one that adds latency; a
**Low-End Protection [ENABLED]** toggle — Low Note Guard is a documented continuous taper and Gravity is the
measured stereo-field law, neither is a safety switch.

VSTGUI only. Spectrum view implementation: promote Disrumpo's `SpectrumDisplay` to `plugins/shared` or
build a harmonic-bar view — decided in 12a. No param-type swaps on registered IDs, ever.

**Reference wireframes from the design conversation (2026-10-11).** Inputs to 12a, not decisions: the main-page
questions above are still open, and the concept §14 screen (spectrum centrepiece, eight knobs, CLEAN ─ MORPH ─
VIOLENT) is the other reference. The "Oversampling" and "Quality" cells and the "Low-End Protection" toggle drawn
on the Advanced page are the items **not adopted** above.

Concept A — macro-first, Bass Morph as the visual centrepiece (the recommended functional layout):

```text
┌──────────────────────────────────────────────────────────────────────────┐
│ PROFUNDUM                                      PRESET: Deep Pressure  ▾  │
│                                                                          │
│  VOICE     MONO  ●  POLY     4 VOICES       GLIDE  ─────●────            │
├──────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│   CORE VOICE                          BASS MORPH                         │
│                                                                          │
│       DEPTH                               A ◉━━━━━━━━●━━━━━━━━○ B        │
│        ◉                                  DEEP         MORPH         RAW  │
│                                                                          │
│   BODY              EDGE               FOLD                              │
│    ◉                 ◉                  ◉                                │
│                                                                          │
│   [ WEIGHT ]        [ DARK ↔ BRIGHT ]    [ CLEAN ↔ VIOLENT ]              │
│       ◉                   ◉                   ◉                          │
│                                                                          │
├──────────────────────────────────────────────────────────────────────────┤
│  SUB ANCHOR       LEVEL ─────●────      CHARACTER ────●────               │
│  ANCHOR MODE      LOCK  /  BLEND  /  PUNCH                              │
├──────────────────────────────────────────────────────────────────────────┤
│  MOTION           OFF  /  ENV  /  CYCLE  /  DRIFT        [ ADVANCED  › ]  │
└──────────────────────────────────────────────────────────────────────────┘
```

Concept B — "the Anchor and the Mass" (the terminology and visual split 12a borrows):

```text
┌──────────────────────────────────────────────────────────────────────────┐
│ PROFUNDUM                                           PRESET ▾    ⚙ SETUP │
├──────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  THE ANCHOR                          THE MASS                            │
│                                                                          │
│  SUB LEVEL  ──────●─────             DEPTH     BODY      EDGE     FOLD    │
│                                          ◉       ◉         ◉       ◉     │
│  SUB CHARACTER ────●────                                                │
│                                                                          │
│  ANCHOR BEHAVIOUR                    HARMONIC CHARACTER                  │
│  [ LOCK ] [ BLEND ] [ PUNCH ]        [ WEIGHT ] [ DARKNESS ] [ VIOLENCE ]│
│                                                                          │
├──────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  SOUND A  ◉━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━○  SOUND B     │
│                            BASS MORPH                                    │
│                                                                          │
│  MOTION:  OFF  /  ENV  /  CYCLE  /  DRIFT       ADVANCED  ›              │
└──────────────────────────────────────────────────────────────────────────┘
```

Advanced page:

```text
┌──────────────────────────────────────────────────────────────────────────┐
│ PROFUNDUM    MAIN    ADVANCED    MODULATION    SETTINGS                  │
├──────────────────────────────────────────────────────────────────────────┤
│ CORE SPECTRUM                                                            │
│                                                                          │
│ Depth       Body        Edge       Spectral Shift       Fold              │
│ ──●─────    ────●───    ──●─────    ─────●──────       ────●────          │
│                                                                          │
│ [Optional spectrum display: harmonic amplitudes across partials]          │
├──────────────────────────────────────────────────────────────────────────┤
│ SUB ANCHOR                                                               │
│                                                                          │
│ Level       Character       Morph Behaviour       Low-End Protection      │
│ ──●─────    ───●─────       [ LOCK ▾ ]            [ ENABLED ]             │
├──────────────────────────────────────────────────────────────────────────┤
│ EXCITATION & FOLD                                                        │
│                                                                          │
│ Edge Drive  ────●────    Excitation Mix ─────●────    Oversampling [4×]   │
│ Fold Amount ──────●──    Fold Character ─────●────    Quality [HIGH ▾]   │
├──────────────────────────────────────────────────────────────────────────┤
│ PITCH & ENVELOPES                                                        │
│                                                                          │
│ Pitch Drop  ───●─────    Time ─────●─────    Shape ─────●─────            │
│ Amp Attack  ──●──────    Decay ─────●─────    Release ──●──────           │
├──────────────────────────────────────────────────────────────────────────┤
│ STEREO & OUTPUT                                                          │
│                                                                          │
│ Mode [SPREAD ▾]   Width ─────●────   Crossover ────●────   Output ──●──   │
└──────────────────────────────────────────────────────────────────────────┘
```

Morph behaviour and Motion blocks (Advanced):

```text
MORPH BEHAVIOUR                               MOTION
────────────────────────────────────────      ────────────────────────────────────────
Sub Anchor      [ LOCK ▾ ]                    Source       [ CYCLE ▾ ]
Parameter Curve [ PERCEPTUAL ▾ ]  (not adopted) Target       [ MASS ▾ ]
Morph Smoothing ─────────●──────              Amount       ─────────●──────
A/B Compare     [ A ] [ B ] [ MORPH ]         Rate         ────●───────────
Copy State      [ A → B ] [ B → A ]           Shape        [ SINE ▾ ]
                                              Sync         [ ON ]   Division [ 1/8 ▾ ]
                                              Advanced: phase, retrigger, fade-in,
                                                        bipolar/unipolar, target routing
```

Morph control concept: *A — DEEP PRESSURE (Round · Clean · Heavy)* ◉━━━━●━━━━○ *B — IRON FOLD (Dense · Bright ·
Aggressive)*, buttons A / A·B / B, position readout "42 %".


### Phase 13: Factory Presets & Release Readiness

**Spec:** `profundum-phase13-presets-release`

Fixed preset category set (filesystem dirs + XML metadata must match — Membrum lesson), installed to
`C:\ProgramData\Krate Audio\Profundum\`. **Variety is the governing requirement** (Vorago Phase 14
rule): the spec carries a preset × capability coverage matrix as an FR, the preset list is derived from
it, and the harness measures pairwise distinctness alongside boundedness. **The audibility probe runs
before the preset list is written** and re-uses Phase 9's table — a preset built on an inaudible feature
is a defect (Vorago 13b–13d). Validation harness: round-trip tests + all-presets NoteOn render sweep
(seconds-scale — this is a bass, not a drone) + per-preset mono-sum check + true-peak check at maximum
Mass. Release gate via `release-readiness` flow.

---

## Dependency Graph

```
                 ┌─→ Phase 2 (edge excitation + fold) ─┐
                 ├─→ Phase 3 (anchor) ─────────────────┤
Phase 1 ─────────┼─→ Phase 7 (stereo field) ───────────┼─→ Phase 9 (voice/engine) ─→ Phase 10 (scaffold)
(harmonic core)  └─→ Phase 8 (bass morph) ─────────────┤              │
                                                       │              ▼
Phase 4 (drop + note behaviour) ───────────────────────┤   Phase 11 → 12 → 13
Phase 5 (shape + character) ───────────────────────────┤
Phase 6 (mass + dynamics) ─────────────────────────────┘
```

Phases 4, 5 and 6 have no dependency on the core and can be built in parallel with Phase 1. Phases 2, 3,
7 and 8 need Phase 1's recipe/bank contract. Phase 9 needs all of them. Part B is strictly sequential.

## Cross-Cutting Constraints (apply to every spec)

- **RT safety:** no allocations / locks / exceptions / IO on the audio thread; everything sized at
  prepare.
- **Audibility is a theme-level FR:** every component ships with a render-descriptor gate showing its
  levers move the sound by at least the ruled bar; nothing ships inert, and Phase 9 proves the whole
  table before any plugin code exists.
- **Mono compatibility is a theme-level FR:** every stage that touches stereo ships a mono-sum test;
  the Anchor path is bit-identical L/R in every mode.
- **Anti-aliasing is a theme-level FR:** every nonlinear stage is measured at E0–E2 worst case with
  the ruled dB bound; oversampling or ADAA is the mechanism, never "it sounds fine".
- **Layer discipline** + **ODR sweep** before every new class name (hazard list above).
- **CPU budgets are FRs**, measured in tests (per-voice budgets Phases 1–5, global 6–7, whole engine 9
  at both MONO and 4-voice operating points). Relaxing a ceiling is never the lever.
- **Close lanes are scoped to the change set** (ruled 2026-10-18, Phase 6 G9-4): per-push lanes of the phase's executables, the
  `[long]` cases of the phase's own TUs, and the full suites of every consumer of a shared header the phase modified; `[long]` cases
  of untouched code are not re-run, and a timing-budget assertion of such a case failing under a concurrent lane is recorded as load.
- **No bit-exact float goldens** — `render_fingerprint.h` / measured tolerances only; goldens harvested
  inside the consuming test binary.
- **Portability:** `node tools/check-portability.js` before commits; WSL probe for Linux doubts;
  `tools/lint-apple-globals.js` and the aligned-load lint on any new SIMD.
- **Naming:** `k{Section}{Parameter}Id`; canonical parameter names from the project table.
- **Shared-component changes** (`MonoHandler` glide modes, any `HarmonicOscillatorBank` hook) are
  append-only and must keep Ruinae, Seraphis, Vorago and Innexus suites green.

## Open Questions (resolve in the relevant spec, not before)

1. **Anchor FOLLOW mechanism** — analysis on the processed voice (`YinPitchDetector` +
   `SubharmonicValidator`, costs latency and CPU) vs an engine-derived perceived-pitch estimate from the
   stages applied (deterministic, free) — Phase 3.
2. **Morph editing model** on the host surface — registered sound parameters address the selected
   endpoint (Edit A / Edit B) while the engine runs the resolved state, vs alternatives — Phase 8 for the
   engine contract, Phase 11 for the VST surface.
3. **Partial count** — 64 vs 96/128 — Phase 1, after CPU measurement and listening for presence at E1.
4. **Anchor join point** default (post-Character) and an advanced pre-Fold feed — Phase 3. The Shape /
   Fold order is **decided 2026-10-09: Shape → Fold** (the concept's §13 order); it is not a spec question.
5. **"Fracture" algorithm** definition — Phase 5.
6. **Gravity law** — crossover frequency range, width curve family, and whether the post safety stage
   is minimum-phase (`CrossoverLR4`) or linear-phase — Phase 7, after mono-sum measurement.
7. **POLY placement** — which nonlinear stages run per voice vs post-sum, and whether Anchor FOLLOW is
   affordable per voice — Phase 9, after CPU measurement.
8. **Polyphony ceiling** beyond 4 — Phase 9 / 11 (`kMaxVoices` vs the registered polyphony range; a
   range change at a shipped ID is forbidden later, so the registered maximum is chosen once).
9. **Main-page knob set and spectrum placement** — concept §14 vs the 2026-10-11 design conversation — Phase
   12a, before any UI code.
10. **Control-name collisions** inherited from the concept — Shape **BITE** vs Character **Bite**,
   Character **Fold** vs **Harmonic Fold**, Anchor modes **LOCK / PUNCH** vs anchor-morph modes
   **Lock / Punch** — resolved once in the Phase 11 parameter names (and the Phase 12 labels), never
   by renaming a registered ID later.
