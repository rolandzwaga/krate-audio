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
  driving the core's retrigger phase policy (Phase 1 input) and the amp/drop envelopes.
- **Retrigger** — oscillator and anchor phase behaviour on new notes (reset / continue), click-free by
  construction.

**Success criteria:** curve-shape tests per shape family (fitted exponent / linearity metrics); rebound
overshoot amount and settle time measured; quantized-glide step timing; a legato × retrigger × note
memory matrix with expected envelope/phase behaviour asserted; no clicks on retrigger at any policy
(max sample delta bound); `ruinae_tests` green after the shared-header change. Evaluation: the four
use-case renders (808 attack, cinematic impact, techno pluck, kick/bass hybrid) inspected.

---

### Phase 5: Shape & Character

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
  WIDE mode can offset drive/tone per channel. The Anchor bypasses it by default.

**Success criteria:** each algorithm's harmonic signature measured and pairwise distinct (descriptor
separation ≥ the bar ruled in spec — audibility gate); Tone monotonic in spectral centroid for every
algorithm; SHAPE sweep continuous (no level/descriptor jump between regimes); algorithm-switch sample
delta bounded; aliasing ≤ −80 dBFS at E0–E2 for every algorithm at maximum Amount; DC ≤ −80 dBFS;
CPU ≤ 2 % per voice with Shape + Character on.

---

### Phase 6: Mass & Dynamics

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

**Success criteria:** every Mass target monotonic over the sweep; perceived level slope bounded (Mass is
not a volume knob — ruled dB range in spec); sub-band gain modulation depth under compression ≤ a ruled
bound (no pumping); transient attack/sustain gains measured against parameters; no discontinuity in any
descriptor across 0–100 at automation speed; CPU ≤ 1 % global.

---

### Phase 7: Bass Stereo Field

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

**Spec:** `profundum-phase8-bass-morph`
**Goal:** The killer feature — travel between two complete bass identities while the fundamental
never disappears.

New components (L3):

- `MorphState` (`systems/morph_state.h`, plain data) — every sound-shaping control: core Depth / Body /
  Edge / Shift, edge excitation, Fold, Shape, Character algorithm + Amount + Tone, Drop, amp envelope,
  image mode / Spread / Gravity / Motion, Motion-section configuration, Mass, Anchor waveform / mode /
  level. Excluded: note behaviour, output level, polyphony. Serialisable, validated (`isValidMorphState`).
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
  measurement (Open Question 7; default per-voice so chords do not intermodulate).
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

### Phase 12: UI

**Spec:** `profundum-phase12-ui`

VSTGUI only. **Concept §14 main screen:** one signature visualisation — the live **spectrum / energy
view** (the concept's SPECTRUM panel: Sub / Body / Presence energy as bars with the Anchor drawn
separately, and the concept §3 interaction — *one draggable point* whose position sets where the energy
lives; whether that point is 1-D along SHIFT or 2-D with Depth is decided in spec) via DataExchange
piggyback (Membrum MetersBlock pattern — no new queues); two rows of large knobs exactly as the concept
draws them — **Weight · Body · Edge · Mass** and **Anchor · Shift · Fold · Drop** — where Weight is the
Personality macro (Depth lives on the Advanced page), Anchor is anchor level, Fold is Harmonic Fold
amount and Drop is Drop amount; the **MORPH** slider spanning A and B, its ends labelled with the
endpoints' names (the concept's default reads CLEAN ─ MORPH ─ VIOLENT) plus the edit target; the four
Personality macros as large knobs (the concept's cross: Weight above, Dark ↔ Bright across, Clean ↔
Violent below, Tight ↔ Loose beside — exact placement decided in spec); image-mode selector; the
**SPREAD** mini-spectrum (20 Hz – 10 kHz, MONO ↔ WIDE across frequency, concept §8) on the stereo panel.
An **Advanced** page exposes the synthesis architecture by section. Spectrum view: promote Disrumpo's
`SpectrumDisplay` to `plugins/shared` or build a harmonic-bar view — decided in spec. No param-type
swaps on registered IDs, ever.

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
9. **Control-name collisions** inherited from the concept — Shape **BITE** vs Character **Bite**,
   Character **Fold** vs **Harmonic Fold**, Anchor modes **LOCK / PUNCH** vs anchor-morph modes
   **Lock / Punch** — resolved once in the Phase 11 parameter names (and the Phase 12 labels), never
   by renaming a registered ID later.
