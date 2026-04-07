# Spec 135 — Membrum: Synthesized Drum Machine

**Status:** Brainstorm / Early Design  
**Plugin:** Membrum  
**Type:** Instrument (`aumu`)  
**Location:** `plugins/membrum/`

## Overview

Membrum is a synthesis-only drum machine plugin. No samples — every sound is generated in real time using modal synthesis, physical modeling, and spectral techniques. The core engine, **Corpus**, uses SIMD-accelerated modal synthesis to model struck and excited resonant bodies, producing drum sounds that range from physically accurate to deliberately unnatural.

The name is Latin for "limb/member," pairing with the engine name Corpus ("body").

## Design Philosophy

Most synth drum machines offer oscillator + noise + filter per voice. Membrum goes deeper: each voice models the physics of an **exciter striking a resonant body**. A mallet hits a membrane. A stick strikes a plate. A brush scrapes a shell. The two-stage model (exciter → body) is intuitive, expressive, and produces sounds with the complexity and variation of real physical interactions.

Then it goes further: the "Unnatural Zone" parameters let you push beyond physics into territory where drums become tonal instruments, evolving textures, or crystalline artifacts.

## Architecture

### Pad Layout

- **32 pads**, mapped to GM drum map (C1/MIDI 36 through G#3/MIDI 67)
- No built-in sequencer — relies on external MIDI (pairs well with Gradus)
- **Separate outputs** per pad (output routing scheme TBD)
- **Kit presets** (all 32 pads) and **per-pad presets**

### Voice Management

- **Configurable max polyphony** (default: 8 voices, range: 4–16)
- Voice stealing policy: configurable (oldest / quietest / priority-based)
- 32 pads provide choice within a bank; not all 32 are expected to sound simultaneously
- **Choke groups**: configurable, essential for open/closed hat pairs and similar

### Per-Voice Signal Path

```
[Exciter] → [Corpus Body] → [Tone Shaper] → [Amp Envelope] → [Output]
                  ↑                                              │
             [Feedback]←─────────────────────────────────────────┘
                  ↑
         [Cross-Pad Coupling]
```

## Exciter Types

The exciter is what triggers the body. Each type responds to velocity — harder hits produce brighter, more complex excitation, not just louder.

| Exciter      | Description                                | Key Parameters         |
|--------------|--------------------------------------------|------------------------|
| **Impulse**  | Pure click, classic analog drum trigger     | Width, brightness      |
| **Noise Burst** | Shaped noise burst for snares/hats     | Color (LP/HP/BP), decay |
| **Mallet**   | Modeled soft-to-hard striker               | Hardness, mass         |
| **Friction** | Bowed/rubbed excitation (brushes, scraped metal) | Pressure, speed  |
| **FM Impulse** | Short FM burst for metallic transients   | Ratio, depth, decay    |
| **Feedback** | Self-excitation from the body's own output | Drive, character       |

### Velocity Behavior

Velocity does NOT simply scale amplitude. This is grounded in physics: the nonlinear power-law contact force `F = K * delta^alpha` means harder hits compress the mallet felt into stiffer layers, producing shorter contact pulses with wider bandwidth. Measured data shows a **10x bandwidth increase** across the dynamic range (turnover frequency: ~100 Hz at pp, ~1000 Hz at ff).

Map MIDI velocity to **two** parameters simultaneously:
1. **Amplitude** (obvious)
2. **Excitation spectral content** (the critical timbral dimension)

Per exciter type:
- **Impulse**: harder = shorter pulse width → wider bandwidth (f_turnover ~ 1/(pi * tau))
- **Mallet**: harder = higher alpha exponent → snappier contact, brighter spectrum. Contact duration: ~8ms (soft) to ~1ms (hard stick)
- **Noise Burst**: harder = higher lowpass cutoff on noise (200 Hz soft → 5000+ Hz hard)
- **Friction**: harder = more bow pressure → richer overtone content, easier self-oscillation

## Corpus Engine — Modal Synthesis Core

The heart of Membrum. Each body model defines a set of **modal frequencies** (partials) with individual amplitudes and decay rates derived from the physics of the modeled object. Each mode is implemented as a two-pole resonator (Gordon-Smith coupled-form) with frequency-dependent damping following the Chaigne-Lambourg model.

### Body Models

| Body          | Physical Analog            | Mode Pattern        | Character                        |
|---------------|----------------------------|---------------------|----------------------------------|
| **Membrane**  | Circular drum head         | Bessel function zeros | Kicks, toms, congas, tablas     |
| **Plate**     | Rectangular metal/wood plate | f ~ m^2+n^2 (stiffness) | Cowbell, woodblock, claves   |
| **Shell**     | Cylindrical resonator      | Mixed axial/radial  | Deep toms, tubular bells         |
| **String**    | Struck/plucked string      | Karplus-Strong variant | Metallic pings, plucks         |
| **Bell**      | Inharmonic rigid body      | Chladni's law f ~ (m+b)^p | Bells, chimes, gamelan      |
| **Noise Body**| Hybrid: sparse modes + filtered noise | ~21 modes/kHz for plates | Hats, cymbals, shakers |

### Body Parameters

| Parameter          | Description                                           | Range        |
|--------------------|-------------------------------------------------------|--------------|
| **Material**       | Controls damping coefficients b1/b3 (see Physics Reference). Soft/damped (wood, rubber) → hard/resonant (metal, glass) | Continuous |
| **Size**           | Scales fundamental frequency and all mode spacings    | Continuous   |
| **Tension**        | Inharmonicity — stretches/compresses mode ratios relative to the body model's physical ratios | Continuous |
| **Damping**        | Overall decay rate (velocity-sensitive). Maps to b1 in Chaigne model | Continuous   |
| **Strike Position**| Scales per-mode amplitude: A_mn ~ J_m(j_mn * r/a) for membranes, sin(m*pi*x/L) for bars | Continuous |
| **Air Coupling**   | For Membrane body: blends between ideal membrane ratios and timpani-like near-harmonic ratios. 0 = pure Bessel, 1 = air-coupled | Continuous |
| **Nonlinear Pitch**| Velocity-dependent pitch rise from tension modulation. Real membranes go up to 65 cents on hard hits | 0–100 cents |

### Partial Configuration

- **Default: 16 partials** per voice — validated as the perceptual sweet spot for membranes (SMC 2019 study: 20-30 modes indistinguishable from recordings)
- 8 modes: minimum viable, acceptable for lo-fi/electronic sounds
- 16 modes: good quality, recognizable drum character
- 20-32 modes: high quality, diminishing returns beyond 30 for membranes
- Cymbals/metallic: need hybrid approach (20-40 modes + filtered noise) due to ~400 modes below 20 kHz
- Max budget at default: 8 voices × 16 partials = **128 simultaneous partials**

## The Unnatural Zone

Parameters that push beyond physical reality. These are core to Membrum's identity, not extras.

| Parameter         | Description                                                        |
|-------------------|--------------------------------------------------------------------|
| **Mode Stretch**  | Compress or spread modal frequencies. 1.0 = physical. Below = clustered/gong-like (gamelan ombak territory). Above = spread/crystalline. Already exists as `stretch` in ModalResonatorBank. |
| **Mode Inject**   | Add synthetic partial series into the body's mode set. Options: harmonic (integer ratios), FM-derived (Chowning ratios like 1:1.4, 1:sqrt(2), 1:phi), or randomized. Uses HarmonicOscillatorBank. |
| **Decay Skew**    | Inverts the Chaigne b3 coefficient sign: normally high modes decay faster (positive b3). Negative b3 = fundamental dies while shimmer sustains. Continuous control from -1 (inverted) through 0 (flat) to +1 (natural). |
| **Material Morph**| Per-hit automation envelope driving the material parameter (b1/b3 coefficients) over the duration of a single hit — evolving timbre from strike to tail. E.g., metal attack → wood decay. |
| **Nonlinear Coupling** | Cubic mode coupling inspired by cymbal physics (von Karman plate theory). Modes exchange energy through nonlinear terms, creating shimmering evolving textures at high amplitudes. Strength parameter controls coupling coefficients. |

## Tone Shaper

Post-body processing per voice:

| Stage              | Description                                    |
|--------------------|------------------------------------------------|
| **Filter**         | LP / HP / BP with dedicated envelope (SVF)     |
| **Drive**          | Soft saturation to hard clipping (ADAA for alias-free processing) |
| **Wavefolder**     | Metallic edge and harmonic complexity           |
| **Pitch Envelope** | Fast exponential pitch sweep. For kicks: f_start=160-500Hz → f_end=40-80Hz over 5-200ms. This is the 808 bridged-T effect: amplitude-dependent frequency shift from diode nonlinearity in the original circuit. |

### Snare Wire Modeling

A special tone shaper stage available on Membrane bodies:

- **Snare amount**: controls mix of snare wire buzz
- Implementation: amplitude-modulated bandpass-filtered noise (1-8 kHz), triggered when membrane displacement exceeds threshold
- **Snare delay**: 0.5-2ms delay between membrane excitation and snare onset (models air cavity propagation)
- **Snare damping**: controls how quickly the wires settle
- Comb filter on noise (delay = snare wire length) for metallic resonance character
- Based on Bilbao's penalty method collision model, simplified for real-time use

## Cross-Pad Coupling (Sympathetic Resonance)

A signature feature: pads can excite each other based on proximity and tuning relationships. Physically modeled after the air-coupled acoustic interaction between instruments in a drum kit.

- **Coupling matrix**: per-pair gain coefficients (0.0 to ~0.05 typical). Each drum's output is bandpass-filtered around the receiving drum's modal frequencies before being fed in as excitation
- **Frequency selectivity**: coupling is strongest when the exciting frequency matches a natural mode of the receiving instrument — this is why tuning toms to different intervals from the snare reduces buzz in real kits
- **Build-up time**: sympathetic response has slower attack than direct excitation (physically accurate)
- Real-world analog: hitting the kick makes snare wires buzz, toms resonate sympathetically
- Configurable intensity (global and per-pair)
- CPU consideration: coupling only active between pads that are currently sounding
- Uses existing SympatheticResonanceSIMD engine from KrateDSP
- Note: IK Multimedia's MODO Drum (the main commercial competitor in this space) exposes this as "Tom Buzz" and "Snare Buzz" knobs — we go further with a full matrix

## Pad Templates

Starting-point configurations for new pads, with physically informed defaults:

| Template   | Exciter Default | Body Default | Key Settings | Character |
|------------|----------------|--------------|-------------|-----------|
| **Kick**   | Impulse        | Membrane     | Pitch env: 160→50Hz/20ms, air coupling 0.3, b1=5, b3=1e-7 | Deep, punchy |
| **Snare**  | Noise Burst    | Membrane     | Snare wires ON, 16 modes, noise cutoff 6kHz | Bright noise + body |
| **Tom**    | Mallet         | Membrane     | Air coupling 0.5 (near-harmonic), pitch env subtle | Pitched, resonant |
| **Hat**    | Noise Burst    | Noise Body   | Hybrid: 20 modes + HP noise at 6kHz, short decay | Metallic sizzle |
| **Cymbal** | Noise Burst    | Noise Body   | Hybrid: 30 modes + broadband noise, long decay, nonlinear coupling | Shimmering wash |
| **Perc**   | Mallet         | Plate        | Square plate ratios, b1=15, medium decay | Cowbell, block, clave |
| **Tonal**  | Mallet         | Bell         | Church bell ratios, b1=0.5, b3=1e-11, long decay | Bells, chimes, gamelan |
| **808**    | Impulse        | Membrane     | Pitch env: 160→50Hz/200ms, high nonlinearity, sine-only (mode 0,1) | Classic electronic |
| **FX**     | FM Impulse     | Shell        | Unnatural zone active, mode stretch=1.5, decay skew=-0.5 | Experimental |

## DSP Implementation Notes

### KrateDSP Integration

The Corpus modal bank will live in the shared DSP library:

- **Layer 2 (processors)** or **Layer 3 (systems)** — TBD based on complexity
- Uses **Gordon-Smith magic circle oscillator** per partial (proven ~30% speedup over sin() in particle oscillator work)
- **Google Highway** SIMD for parallel partial computation
  - SSE: 4 partials per instruction
  - AVX2: 8 partials per instruction (full 16-partial voice in 2 iterations)
  - NEON: 4 partials per instruction (ARM/Apple Silicon)

### Gordon-Smith Per Partial

```
epsilon = 2 * sin(pi * freq / sampleRate)  // precomputed on note-on
s_new = s + epsilon * c
c_new = c - epsilon * s_new                // amplitude-stable (det = 1)
output += amplitude * s_new
```

### Performance Targets

- 8 voices × 16 partials = 128 partials at 44.1 kHz
- Target: < 2% CPU on modern hardware (single core)
- Profiling required to validate; adjust max voices/partials if needed

### Real-Time Safety

Standard Krate rules apply:
- No allocations, locks, exceptions, or I/O on audio thread
- All pad/kit configuration changes via lock-free messaging
- Voice pool pre-allocated at max polyphony

## MIDI

- Standard GM drum map: pad 1 = C1 (MIDI 36) through pad 32 = G#3 (MIDI 67)
- Velocity-sensitive (drives exciter character + amplitude)
- No built-in sequencer

## Output Routing

- Separate outputs per pad (stereo or mono TBD)
- Assignment scheme TBD — to be designed in a separate session

## UI

- To be designed in a separate brainstorm session
- Working concept: 4×8 pad grid (left) + selected pad editor (center) + kit-level controls (right)

## Existing Building Blocks

The KrateDSP library and shared plugin infrastructure already contain the majority of components needed for Membrum. This section maps existing code to Membrum's architecture.

### Corpus Engine — Ready to Use

The modal synthesis core already exists and is SIMD-accelerated:

| Component | Location | What It Does | Membrum Role |
|-----------|----------|-------------|--------------|
| **ModalResonatorBank** | `dsp/include/krate/dsp/processors/modal_resonator_bank.h` | Up to 96 parallel Gordon-Smith coupled-form resonators. SoA layout, 32-byte aligned. Chaigne-Lambourg frequency-dependent damping. Material presets (Wood, Metal, Glass, Ceramic, Nylon). | **THE Corpus body engine.** Direct use for Membrane, Plate, Shell, Bell body models. Already has size, damping, material, inharmonicity (stretch/scatter) params. |
| **ModalResonatorBankSIMD** | `dsp/include/krate/dsp/processors/modal_resonator_bank_simd.h/.cpp` | Highway-accelerated SIMD kernel for modal bank. Processes 4 (SSE) or 8 (AVX2) modes per iteration. | **Core performance engine.** Already proven, runtime ISA dispatch. |
| **ModalResonator** | `dsp/include/krate/dsp/processors/modal_resonator.h` | Simpler single-body modal resonator, up to 32 modes. Impulse-invariant two-pole topology. | Fallback / lightweight body for CPU-constrained voices. |
| **IResonator** | `dsp/include/krate/dsp/processors/iresonator.h` | Abstract resonator interface (frequency, decay, brightness, excitation, energy followers). | **Unified interface** — swap between ModalResonatorBank and WaveguideString without changing voice architecture. |

### Exciter Models — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **ImpactExciter** | `dsp/include/krate/dsp/processors/impact_exciter.h` | **Primary exciter.** Asymmetric pulse + shaped noise, SVF-filtered, strike position comb, micro-bounce. Velocity-dependent hardness/brightness/duration. Maps directly to Impulse, Mallet, and Noise Burst exciter types. |
| **BowExciter** | `dsp/include/krate/dsp/processors/bow_exciter.h` | **Friction exciter.** Stick-slip friction model (STK power-law), bow velocity from ADSR, rosin jitter, 2x oversampling. Maps directly to the Friction exciter type. |
| **NoiseGenerator** | `dsp/include/krate/dsp/processors/noise_generator.h` | **Noise component.** 13 noise types (white, pink, brown, blue, violet, velvet, etc.). Noise Burst exciter and Noise Body backing. |
| **FMOperator** | `dsp/include/krate/dsp/processors/fm_operator.h` | **FM Impulse exciter.** DX7-style phase modulation with self-feedback. Short FM burst for metallic transients. |

### Resonator Alternatives — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **WaveguideString** | `dsp/include/krate/dsp/processors/waveguide_string.h` | **String body model.** Dispersion allpass cascade, Thiran fractional delay, pick position, stiffness/inharmonicity. Implements IResonator. |
| **KarplusStrong** | `dsp/include/krate/dsp/processors/karplus_strong.h` | **Alternative string body.** Allpass interpolation, brightness control, pick position comb, stretch via allpass cascade, continuous bowing mode. |

### Sympathetic Resonance — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **SympatheticResonanceSIMD** | `dsp/systems/sympathetic_resonance_simd.h/.cpp` | **Cross-pad coupling engine.** SIMD-accelerated second-order driven resonators with Chaigne-Lambourg damping and envelope followers. This is exactly the sympathetic resonance feature we designed. |

### Tone Shaping — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **SVF** | `dsp/include/krate/dsp/primitives/svf.h` | **Per-voice filter.** TPT state variable filter, 8 modes (LP/HP/BP/Notch/Allpass/Peak/Shelf). Per-sample coefficient smoothing. Modulation-stable. |
| **Waveshaper** | `dsp/include/krate/dsp/primitives/waveshaper.h` | **Drive stage.** 9 transfer functions (Tanh, Atan, Cubic, Diode, Tube, etc.). Drive + asymmetry params. |
| **Wavefolder** | `dsp/include/krate/dsp/primitives/wavefolder.h` | **Fold stage.** Triangle, Sine (Serge), Lockhart (Lambert-W). |
| **SaturationProcessor** | `dsp/include/krate/dsp/processors/saturation_processor.h` | **High-level saturation.** Multiple types, input/output gain, makeup gain. |
| **TanhADAA / HardClipADAA** | `dsp/include/krate/dsp/primitives/tanh_adaa.h`, `hard_clip_adaa.h` | **Alias-free distortion** for feedback loops. |
| **DCBlocker** | `dsp/include/krate/dsp/primitives/dc_blocker.h` | Post-saturation DC offset removal. |

### Envelopes — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **ADSREnvelope** | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | **Amplitude & filter envelopes.** Exponential/linear/log curves, velocity scaling, retrigger modes, Bezier curves. |
| **MultiStageEnvelope** | `dsp/include/krate/dsp/processors/multi_stage_envelope.h` | **Complex pitch envelopes.** N-stage with time and level per stage. Perfect for kick pitch sweeps. |
| **EnvelopeFollower** | `dsp/include/krate/dsp/processors/envelope_follower.h` | **Dynamic response.** Separate attack/release, RMS/amplitude modes. For velocity-responsive feedback control. |

### Voice Management — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **VoiceAllocator** | `dsp/include/krate/dsp/systems/voice_allocator.h` | **Core voice routing.** Up to 32 voices, multiple stealing strategies (RoundRobin, Oldest, LowestVelocity, HighestNote). Pre-allocated, real-time safe. Configurable max polyphony. |
| **SynthVoice** | `dsp/include/krate/dsp/systems/synth_voice.h` | **Reference voice architecture.** Composites oscillators + filter + envelopes. Adapt for modal drum voice by swapping resonator for oscillator+filter. |

### Oscillators — Available for Sub/Tone Layers

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **PolyBlepOscillator** | `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` | Sub-oscillator for kick reinforcement. FM/PM inputs for metallic tones. |
| **HarmonicOscillatorBank** | `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` | Up to 64 SIMD-accelerated partials. Could supplement modal bank for additive excitation or Mode Inject. |
| **WavetableOscillator** | `dsp/include/krate/dsp/primitives/wavetable_oscillator.h` | Custom percussion wavetables, mipmap anti-aliasing. |

### Delay / Feedback Infrastructure — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **DelayLine** | `dsp/include/krate/dsp/primitives/delay_line.h` | Waveguide delay loops, comb filters, feedback paths. Multiple interpolation modes. |
| **FeedbackNetwork** | `dsp/include/krate/dsp/systems/feedback_network.h` | Self-oscillation safe feedback with filter+saturation in loop, freeze mode. For the Feedback exciter type. |
| **CombFilter** | `dsp/include/krate/dsp/primitives/comb_filter.h` | Strike position effects, harmonic enhancement. |

### Core Utilities — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **Gordon-Smith phasor** | Proven in `particle_oscillator.h` line 402+ and `modal_resonator_bank_simd.cpp` | 2 muls + 2 adds per partial. 30% faster than sin() lookup. Already SIMD-vectorized. |
| **XorShift32** | `dsp/core/xorshift32.h` | Per-voice deterministic randomness for micro-variation. |
| **FastMath** | `dsp/core/fast_math.h` | Fast tanh, sqrt, sigmoid for feedback loops. |
| **PitchUtils** | `dsp/core/pitch_utils.h` | MIDI note to frequency, semitone/cent calculations. |
| **Interpolation** | `dsp/core/interpolation.h` | Linear, cubic Hermite, Lagrange for sub-sample accuracy. |
| **OnePoleSmoother** | `dsp/include/krate/dsp/primitives/smoother.h` | Click-free parameter updates. |
| **LFO** | `dsp/include/krate/dsp/primitives/lfo.h` | Modulation source for future mod matrix. |
| **ModulationMatrix** | `dsp/systems/modulation_matrix.h` | Full mod routing system for future expansion. |

### Shared Plugin Infrastructure — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **PresetManager** | `plugins/shared/src/preset/preset_manager.h` | Kit + per-pad preset save/load/browse. Generalized via PresetManagerConfig. |
| **PresetBrowserView** | `plugins/shared/src/ui/preset_browser_view.h` | Modal preset browser with categories, search, save dialogs. |
| **MidiEventDispatcher** | `plugins/shared/src/midi/midi_event_dispatcher.h` | Template-based VST3 event dispatch. Auto-detects handler signatures (basic/MPE/NoteExpression). Zero-copy. |
| **MidiCCManager** | `plugins/shared/src/midi/midi_cc_manager.h` | CC-to-parameter mapping, MIDI Learn, 14-bit CC support. |
| **ArcKnob** | `plugins/shared/src/ui/arc_knob.h` | Gradient-trail knob with modulation ring and value popup. |
| **ToggleButton** | `plugins/shared/src/ui/toggle_button.h` | Vector-drawn toggle with configurable icons. Mute/solo/choke group toggles. |
| **ActionButton** | `plugins/shared/src/ui/action_button.h` | Press-only button for pad triggers. |
| **StepPatternEditor** | `plugins/shared/src/ui/step_pattern_editor.h` | Step sequencer UI with drag editing, velocity, 32 steps. If we ever add a built-in sequencer. |
| **ADSR Display** | `plugins/shared/src/ui/adsr_display.h` | Visual envelope editor. For per-pad envelope visualization. |
| **XY Morph Pad** | `plugins/shared/src/ui/xy_morph_pad.h` | 2D morphing control. Material Morph or Mode Stretch/Decay Skew control. |
| **Platform Paths** | `plugins/shared/src/platform/preset_paths.h` | Cross-platform preset directory discovery. |

### Plugin Architecture Patterns — Follow Existing Instruments

The instrument plugins (Ruinae, Innexus, Gradus) establish proven patterns:

| Pattern | Reference Plugin | Key Files |
|---------|-----------------|-----------|
| Entry point & factory | Ruinae | `plugins/ruinae/src/entry.cpp` |
| Plugin IDs & param blocks | Ruinae | `plugins/ruinae/src/plugin_ids.h` |
| Param group structs (atomic) | Ruinae | `plugins/ruinae/src/parameters/*.h` |
| Processor (MIDI → voice → audio) | Innexus | `plugins/innexus/src/processor/processor.h` |
| Controller (UI + presets) | Ruinae | `plugins/ruinae/src/controller/controller.h` |
| Voice struct pattern | Innexus | `plugins/innexus/src/processor/` |
| Preset config | Ruinae | `plugins/ruinae/src/preset/ruinae_preset_config.h` |
| AU configuration | Gradus | `plugins/gradus/resources/au-info.plist`, `auv3/audiounitconfig.h` |
| CMake plugin target | Gradus | `plugins/gradus/CMakeLists.txt` |

### Test Infrastructure — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **Signal Metrics** | `tests/test_helpers/signal_metrics.h` | SNR, THD, crest factor for sound quality validation. |
| **Spectral Analysis** | `tests/test_helpers/spectral_analysis.h` | FFT-based aliasing measurement, spectral peaks, THD verification. |
| **Artifact Detection** | `tests/test_helpers/artifact_detection.h` | Click/pop/clipping detection for regression testing. |
| **Allocation Detector** | `tests/test_helpers/allocation_detector.h` | Real-time safety verification (no allocs in process). |
| **Test Signals** | `tests/test_helpers/test_signals.h` | Standard sine, sweep, noise, impulse generators. |
| **Parameter Sweep** | `tests/test_helpers/parameter_sweep.h` | Systematic parameter sensitivity analysis. |

### Coverage Summary

| Membrum Feature | Existing Components | New Code Needed |
|----------------|--------------------|-----------------| 
| Corpus body (modal) | ModalResonatorBank + SIMD | Body model presets (mode frequency tables for membrane/plate/shell/bell) |
| Exciter (impulse/mallet) | ImpactExciter | Minimal — parameter mapping |
| Exciter (friction) | BowExciter | Minimal — parameter mapping |
| Exciter (noise burst) | NoiseGenerator + ADSREnvelope | Combine existing components |
| Exciter (FM) | FMOperator | Short-burst envelope wrapper |
| Exciter (feedback) | FeedbackNetwork | Route body output → exciter input |
| String body | WaveguideString / KarplusStrong | Already done |
| Noise body | NoiseGenerator + SVF | Combine existing components |
| Tone shaper | SVF + Waveshaper + Wavefolder | Combine into per-voice chain |
| Envelopes | ADSREnvelope + MultiStageEnvelope | Done |
| Pitch envelope | MultiStageEnvelope | Done |
| Voice management | VoiceAllocator | Configure for drum use (choke groups need new logic) |
| Cross-pad coupling | SympatheticResonanceSIMD | Already SIMD-accelerated |
| Unnatural: Mode Stretch | ModalResonatorBank stretch param | Already exists as "stretch" |
| Unnatural: Mode Inject | HarmonicOscillatorBank | Mix into modal bank's mode set |
| Unnatural: Decay Skew | ModalResonatorBank | **New**: invert decay-vs-frequency curve |
| Unnatural: Material Morph | ModalResonatorBank material | **New**: per-hit automation envelope for material |
| MIDI dispatch | MidiEventDispatcher | Done |
| Presets | PresetManager + browser | Config struct only |
| UI controls | ArcKnob, ToggleButton, etc. | Done |
| Pad grid UI | — | **New**: custom CView for 4×8 pad grid |
| Choke groups | — | **New**: choke group logic in voice allocator |
| Output routing | — | **New**: per-pad output bus assignment |
| Plugin scaffold | Follow Ruinae/Gradus patterns | Boilerplate from templates |

**Bottom line: ~80% of the DSP engine exists.** The main new work is:
1. Drum voice class compositing existing components
2. Choke group logic
3. Body model preset tables (modal frequency ratios for each body type)
4. Decay Skew and Material Morph automation
5. Pad grid UI
6. Output routing
7. Plugin scaffold (entry, IDs, CMake — boilerplate)

## Physical Modeling Reference

This section documents the scientific foundations for each body model and exciter type. All formulas and values are sourced from peer-reviewed acoustics literature.

### Circular Membrane Modes (Bessel Function Zeros)

The vibration modes of a circular membrane clamped at its edge are governed by the 2D wave equation in polar coordinates. Modal frequencies are proportional to zeros of Bessel functions J_m:

```
f_mn = (j_mn / (2*pi*a)) * sqrt(T / sigma)
```

where `a` = radius, `T` = tension, `sigma` = surface density, and `j_mn` is the n-th zero of J_m(x).

**Frequency ratios relative to fundamental (j_mn / j_01):**

| Mode (m,n) | j_mn   | Ratio | Description |
|------------|--------|-------|-------------|
| (0,1)      | 2.4048 | 1.000 | Fundamental |
| (1,1)      | 3.8317 | 1.593 | 1 nodal diameter |
| (2,1)      | 5.1356 | 2.136 | 2 nodal diameters |
| (0,2)      | 5.5201 | 2.296 | 1 nodal circle |
| (3,1)      | 6.3802 | 2.653 | 3 nodal diameters |
| (1,2)      | 7.0156 | 2.918 | 1 dia + 1 circle |
| (4,1)      | 7.5883 | 3.156 | 4 nodal diameters |
| (2,2)      | 8.4172 | 3.501 | 2 dia + 1 circle |
| (0,3)      | 8.6537 | 3.600 | 2 nodal circles |
| (5,1)      | 8.7715 | 3.649 | 5 nodal diameters |
| (3,2)      | 9.7610 | 4.060 | 3 dia + 1 circle |
| (1,3)      | 10.1735| 4.231 | 1 dia + 2 circles |
| (4,2)      | 11.0647| 4.602 | 4 dia + 1 circle |
| (2,3)      | 11.6198| 4.832 | 2 dia + 2 circles |
| (0,4)      | 11.7915| 4.903 | 3 nodal circles |
| (5,2)      | 12.3386| 5.131 | 5 dia + 1 circle |

These ratios are **inharmonic** — none are integer multiples. This is why drums have indefinite pitch.

**Sources:** Abramowitz & Stegun (1964), Wolfram MathWorld, NIST DLMF 10.21

### Timpani: Air Coupling Shifts Toward Harmonicity

Enclosed air in a kettledrum preferentially shifts the (m,1) modes toward a near-harmonic series. The **Air Coupling** parameter in the Membrane body model interpolates between ideal and air-coupled ratios:

| Mode   | Ideal Membrane | Timpani (air-coupled) | Near-harmonic |
|--------|---------------|----------------------|---------------|
| (1,1)  | 1.593         | 1.000 (reference)    | 1.00          |
| (2,1)  | 2.136         | 1.500                | 1.50          |
| (3,1)  | 2.653         | 1.980                | ~2.00         |
| (4,1)  | 3.156         | 2.440                | ~2.50         |
| (5,1)  | 3.649         | ~2.94                | ~3.00         |

The (0,n) axisymmetric modes are suppressed by air coupling. This gives timpani (and kick drums with ports) a clearer sense of pitch.

**Source:** Rossing, "Science of Percussion Instruments" (2000); Sound on Sound timpani synthesis series

### Rectangular Plate Modes (Kirchhoff Theory)

Plates differ from membranes: **bending stiffness** dominates (4th-order biharmonic equation vs 2nd-order wave equation). For a simply-supported rectangular plate:

```
f_mn = (pi/2) * sqrt(D / (rho*h)) * (m^2/a^2 + n^2/b^2)
```

where `D = 2*h^3*E / (3*(1-nu^2))` is bending stiffness. Ratios for a **square plate** (a=b):

| Mode (m,n) | Ratio |
|------------|-------|
| (1,1)      | 1.00  |
| (1,2)=(2,1)| 2.50  |
| (2,2)      | 4.00  |
| (1,3)=(3,1)| 5.00  |
| (2,3)=(3,2)| 6.50  |
| (1,4)=(4,1)| 8.50  |
| (3,3)      | 9.00  |
| (2,4)=(4,2)| 10.00 |

Key difference: plate modes scale as m^2+n^2 (wider spacing at high freq) vs membrane modes scaling as sqrt(m^2+n^2).

**808 Cowbell reference:** Two resonant frequencies at 587 Hz and 845 Hz (ratio 1:1.44), bandpass at 2640 Hz, decay ~50-100ms.

**Source:** Leissa, "Vibration of Plates" (NASA SP-160, 1969)

### Free-Free Beam Modes (Bars, Xylophones, Marimbas)

For the Shell body model and bar-type percussion:

| Mode k | f_k/f_1 ratio |
|--------|--------------|
| 1      | 1.000        |
| 2      | 2.757        |
| 3      | 5.404        |
| 4      | 8.933        |
| 5      | 13.344       |
| 6      | 18.637       |

These are **extremely inharmonic**. Xylophone/marimba makers undercut bars to shift mode 2 toward 3x or 4x the fundamental.

**Tubular bells:** Modes 4-6 approximate 2:3:4 ratio — the ear perceives a virtual pitch one octave below mode 4. The three lowest modes are too weak to contribute to pitch.

**Source:** Fletcher & Rossing, "The Physics of Musical Instruments" (1998)

### Bell Partials (Chladni's Law)

Church bell partials follow a modified Chladni's law: `f_m = C * (m+b)^p`

Named partials relative to the **nominal** (the strike tone):

| Partial     | Ratio to Nominal | Interval              |
|-------------|------------------|-----------------------|
| Hum         | 0.250            | 2 octaves below       |
| Prime       | 0.500            | 1 octave below        |
| Tierce      | 0.600            | Minor 10th below      |
| Quint       | 0.750            | Perfect 5th below     |
| **Nominal** | **1.000**        | **Reference**         |
| Superquint  | ~1.500           | ~Perfect 5th above    |
| Octave Nom. | ~2.000           | ~Octave above         |

Gamelan instruments are intentionally inharmonic with beating between paired instruments (ombak, 5-7 beats/sec).

**Source:** Hibberts, "Partial Frequencies and Chladni's Law" (Open J. Acoustics, 2014); Perrin et al., JASA (1983)

### Frequency-Dependent Damping (Chaigne-Lambourg Model)

The standard model for how modes decay at different rates:

```
R_k = b1 + b3 * f_k^2
T60_k = 6.91 / R_k
```

where:
- `b1` = frequency-independent damping (air drag, viscous losses) [Hz]
- `b3` = frequency-dependent damping (internal material friction) [seconds]
- `f_k` = frequency of mode k [Hz]

**Material parameter values:**

| Material    | b1 (s^-1) | b3 (s)      | Loss Factor (eta) | Q Factor | Character |
|-------------|-----------|-------------|-------------------|----------|-----------|
| Steel       | 0.1-0.5   | 1e-11–1e-10 | 0.0001–0.01      | 100–10000 | Very resonant, long sustain |
| Brass       | 0.2-1.0   | 1e-11–1e-10 | 0.0002–0.001     | 1000–5000 | Bright, good sustain |
| Glass       | 0.1-0.5   | 1e-11–1e-10 | 0.0001–0.005     | 200–10000 | Crystalline, resonant |
| Aluminum    | 0.2-1.0   | 1e-10–1e-9  | 0.0001–0.02      | 50–10000 | Bright, moderate sustain |
| Ceramic     | 1.0-5.0   | 1e-9–1e-8   | 0.001–0.01       | 100–1000 | Medium sustain |
| Wood        | 10-30     | 1e-8–1e-7   | 0.005–0.05       | 20–200   | Short sustain, warm |
| Nylon/Rubber| 50-200    | 1e-6–1e-5   | 0.05–2.0         | 0.5–20   | Very damped, dead thud |

**Key insight:** b1 controls overall sustain length. b3 controls how much faster high modes decay relative to low modes. Metal: low b1 (long), low b3 (even across spectrum). Wood: high b1 (short), moderate b3 (darker decay). Rubber: extreme b1 (almost no sustain).

The **Material** knob in Membrum sweeps through these b1/b3 value pairs continuously.

**Source:** Chaigne & Lambourg, JASA (2001); Chaigne & Askenfelt, JASA (1994); Nathan Ho

### Strike Position Mathematics

The amplitude of mode (m,n) excited by a point strike at position (r_0, theta_0) on a circular membrane:

```
A_mn ~ J_m(j_mn * r_0 / a) * cos(m * theta_0)
```

**Center strike (r_0 = 0):** Only axisymmetric modes (m=0) excited. Boomy, pitched.
**Edge strike (r_0 ~ a):** Many high-m modes excited. Bright, cutting, inharmonic.
**Off-center (r_0 ~ 0.3-0.7 * a):** Broadest range of modes. Typical playing position.

For rectangular plates/bars: `A_mn ~ sin(m*pi*x_0/a) * sin(n*pi*y_0/b)` — much simpler.

Striking at a nodal line of a particular mode suppresses that mode. The (0,2) mode has a nodal circle at r = 0.436*a.

**Source:** Fletcher & Rossing (1998); Kinsler, "Fundamentals of Acoustics"

### Power-Law Contact Force (Mallet-Membrane Interaction)

The mallet-membrane interaction follows a nonlinear power law:

```
F = K * delta^alpha
```

where `delta` = felt compression, `K` = stiffness, `alpha` = nonlinearity exponent.

| Mallet Type         | alpha    | Contact Duration | Character |
|---------------------|----------|------------------|-----------|
| Soft felt           | 1.5–2.5  | 5–8 ms           | Warm, low BW excitation |
| Hard rubber/plastic | 2.5–3.5  | 2–5 ms           | Bright, broadband |
| Wood stick tip      | 3.0–4.0  | 1–3 ms           | Very bright, snappy |

The spectral turnover frequency (where excitation rolls off): `f_turnover ~ 1 / (pi * tau_contact)`

This means harder mallets produce ~10x wider bandwidth excitation than soft ones — the fundamental reason velocity changes timbre, not just volume.

**Implementation:** Generate the excitation as a parametric force pulse (half-cosine or Gaussian) whose width scales inversely with velocity, filtered through a lowpass whose cutoff tracks hardness. This is what ImpactExciter already does.

**Source:** Chaigne & Doutaut (mallet percussion); Hunt-Crossley contact model; Boutillon (piano hammers)

### Nonlinear Tension Modulation (Pitch Glide)

Real drum heads exhibit amplitude-dependent pitch rise: large displacements stretch the membrane, increasing tension and raising pitch. This has been experimentally measured at up to **65 cents** on hard hits.

```
f_k(t) = f_k0 * (1 + alpha * E_total(t))
```

where `E_total(t)` is total instantaneous energy across all modes (decays with the sound). The **Nonlinear Pitch** parameter controls `alpha`. This is distinct from the Pitch Envelope in the tone shaper — it's physics-based and affects all modes simultaneously, proportional to the total energy.

**Source:** Bilbao, Touze; Nathan Ho (modal synthesis)

### FM Synthesis Ratios for Metallic Percussion

When using the FM Impulse exciter, these carrier:modulator ratios produce specific metallic characters:

| Sound Target   | C:M Ratio      | Mod Index | Notes |
|---------------|----------------|-----------|-------|
| Bell/Gong      | 1 : 1.4        | 4–8       | Classic Chowning bell |
| Cymbal/Gong    | 1 : sqrt(2)    | 6–10+     | Maximum inharmonicity |
| Stria (Chowning)| 1 : phi (1.618)| variable  | Maximally non-repeating spectrum |
| Tubular bell   | 2 : 5          | moderate  | Harmonic with gaps |
| Metallic chime | 1 : 2.005      | 3–6       | Near-integer = slow beating |
| Marimba        | 1 : 2.4        | 0.5–2     | Wood-like metallic |
| Wood drum      | 1 : 1.4        | 1–3       | Also 1:0.6875 |

**Key technique:** The modulation index must decay faster than the carrier amplitude — this makes the attack bright (many sidebands) while the tail is pure (few sidebands), mimicking real metallic percussion where high-frequency modes decay faster.

**Source:** Chowning, JAES (1973); CCRMA percussion synthesis tutorials

### Cymbal Synthesis: The Hybrid Approach

Cymbals have ~21 modes/kHz (Weinreich formula), yielding ~400 modes below 20 kHz — far too many for brute-force modal synthesis. The recommended hybrid approach:

1. **Modal component** (20-40 modes): Captures the tonal body and prominent resonances. Use plate mode ratios with frequency-dependent damping.
2. **Noise component**: Filtered noise (bandpass, time-varying center frequency and bandwidth) for the high-frequency wash and shimmer.
3. **Nonlinear coupling**: At high amplitudes, cymbal modes become chaotic — model as amplitude-dependent spectral broadening of the noise component.
4. **Spectral evolution**: Energy migrates upward through the spectrum over the first ~100ms, then high frequencies decay first.

**Hi-hat specifics:** Model pedal position as continuous control over damping coefficients of all modes. Closed = extreme damping (10-50ms decay). Open = long sustain (seconds). "Chick" = rapid closure creating short noise burst.

**808 alternative:** Six inharmonic oscillators at ~142, 211, 297, 385, 540, 800 Hz through steep highpass (~6kHz) and envelope shaping. CPU-efficient, iconic electronic sound.

**Source:** Dahl et al., DAFx 2019; Rossing JASA cymbal studies; TR-808 circuit analyses

### Snare Wire Physics

The snare drum's characteristic buzz comes from snare wires rattling against the bottom membrane in a highly nonlinear collision process.

**Bilbao's penalty method:** `F_collision = K_c * [delta]_+^alpha_c` where `[delta]_+` = max(0, delta) ensures one-sided contact only.

**Simplified real-time model:**
1. Take membrane resonator output
2. Amplitude-modulate a bandpass-filtered noise source (1-8 kHz) — when displacement exceeds threshold, snare noise activates
3. Apply comb filter to noise (delay = snare wire length) for metallic resonance
4. Add 0.5-2ms propagation delay (air cavity coupling)
5. Mix at adjustable snare amount

**Source:** Bilbao, JASA (2012); Torin, Hamilton & Bilbao, DAFx (2014)

### Brush Excitation Physics

Wire brushes (50-200 bristles) produce sound through stochastic friction and micro-impacts.

**LuGre friction model** (most practical for real-time):
```
dz/dt = v - sigma_0 * |v| * z / g(v)
F = sigma_0 * z + sigma_1 * dz/dt + sigma_2 * v
g(v) = F_coulomb + (F_static - F_coulomb) * exp(-(v/v_stribeck)^2)
```

**Simplified approach for Membrum:** Filtered noise as excitation with bandwidth proportional to brush velocity, plus granular micro-impulses at rate proportional to brush speed (modeling bristle snaps). Spectral centroid modulates with pressure.

**Source:** Bilbao et al., IEEE (2022); Avanzini, DAFx (2002)

### Self-Oscillation Conditions

For bowed/sustained percussion (Friction exciter), self-oscillation occurs when:

```
Energy_input_per_cycle >= Energy_lost_per_cycle
```

The negative slope region of the friction curve (Stribeck effect) acts as negative resistance, pumping energy into the system. Minimum bow force for sustained oscillation:

```
F_bow_min ~ (f_k / Q_k) * (v_bow / delta_v_stribeck)
```

Higher modes require more bow pressure to sustain. BowExciter already implements this via STK power-law friction.

**Source:** Essl & Cook, ICMC (1999); "Banded Waveguides for Bowed Bar Percussion"

### Quick Reference: Mode Ratios by Instrument

| Instrument      | Model         | First 8 ratios |
|----------------|---------------|----------------|
| Ideal membrane  | Bessel/j_01   | 1.000, 1.593, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501 |
| Timpani         | Air-coupled   | 1.000, 1.500, 1.980, 2.440, 2.940, 3.430, 3.890, 4.350 |
| Kick drum       | Lightly coupled | Between ideal membrane and timpani |
| Square plate    | Kirchhoff     | 1.000, 2.500, 4.000, 5.000, 6.500, 8.500, 9.000, 10.000 |
| Free-free beam  | Euler-Bernoulli | 1.000, 2.757, 5.404, 8.933, 13.344, 18.637, 24.812, 31.870 |
| Church bell     | Chladni       | 0.250, 0.500, 0.600, 0.750, 1.000, 1.500, 2.000, 2.600 |
| 808 Cowbell     | Two-mode      | 587 Hz, 845 Hz (ratio 1:1.44) |
| Tubular bell    | Modes 4-6     | Perceived pitch at mode4/2, modes 4-6 ≈ 2:3:4 |

### Academic References

1. Rossing, T.D. — "Science of Percussion Instruments" (World Scientific, 2000)
2. Fletcher, N.H. & Rossing, T.D. — "The Physics of Musical Instruments" (Springer, 1998)
3. Chaigne, A. & Lambourg, C. — "Time-domain simulation of damped impacted plates" (JASA, 2001)
4. Chaigne, A. & Askenfelt, A. — "Numerical simulations of piano strings" (JASA, 1994)
5. Bilbao, S. — "Time domain simulation and sound synthesis for the snare drum" (JASA, 2012)
6. Chowning, J.M. — "The Synthesis of Complex Audio Spectra by Means of FM" (JAES, 1973)
7. Essl, G. & Cook, P.R. — "Banded Waveguides for Bowed Bar Percussion" (ICMC, 1999)
8. Dahl, L. et al. — "Real-Time Modal Synthesis of Crash Cymbals" (DAFx, 2019)
9. Leissa, A.W. — "Vibration of Plates" (NASA SP-160, 1969)
10. Hibberts, W.A. — "Partial Frequencies and Chladni's Law in Church Bells" (OJA, 2014)
11. Ho, N. — "Exploring Modal Synthesis" (nathan.ho.name, 2023)
12. Werner, K.J. et al. — "TR-808 Bass Drum Circuit Model" (DAFx, 2014)
13. Delle Monache, S. et al. — "Perceptual Evaluation of Modal Synthesis" (SMC, 2019)
14. Avanzini, F. — "Friction Models for Sound Synthesis" (DAFx, 2002)
15. Shier, J. — "Differentiable Modeling of Percussive Audio" (2024)

## Open Questions / Future Versions

### Resolved by Research
- ~~Exact partial counts~~: **16 default is validated** (SMC 2019 perceptual study). 8 min, 32 max, 20-30 for high quality.
- ~~Cross-pad coupling topology~~: **Bandpass-filtered coupling matrix** with per-pair gain coefficients (0-0.05 range). Already implemented in SympatheticResonanceSIMD.

### Still Open
- **Sample layer**: v2 could add sample playback for hybrid sounds
- **Per-pad effects**: reverb, delay sends per pad (or rely on DAW routing via separate outs?)
- **Modulation**: LFOs, envelope followers, or a mod matrix per pad?
- **Microtuning**: custom tuning tables for the tonal body models?
- **Double membrane coupling**: Model batter + resonant heads with air cavity spring? Would improve tom/snare realism but doubles the modal computation per voice.
- **Nonlinear mode coupling strength**: The cubic coupling coefficients from von Karman plate theory — how to expose this as a musically useful parameter? Currently proposed as "Nonlinear Coupling" in the Unnatural Zone.
- **Cymbal mode count vs CPU**: Need profiling to determine if 30-40 modes + noise is viable for multiple simultaneous cymbals within the voice budget.
- **Differentiable synthesis / neural acceleration**: DMSP (2024) and DrumBlender show promise for O(1) inference with physical accuracy. v2 investigation.
- **MODO Drum comparison**: IK Multimedia uses samples for cymbals (suggesting modal cymbal synthesis was too expensive for them). Our hybrid approach needs validation.

## Version History

| Version | Date       | Notes                    |
|---------|------------|--------------------------|
| 0.1     | 2026-04-07 | Initial brainstorm spec  |
| 0.2     | 2026-04-07 | Added existing building blocks analysis |
| 0.3     | 2026-04-07 | Added physics reference, refined body models, snare wires, cymbal hybrid, 808 template, nonlinear pitch, FM ratios |
