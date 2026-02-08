# Ruinae Development Roadmap

**Status**: Planning | **Created**: 2026-02-07 | **Source**: [Ruinae.md](Ruinae.md)

A comprehensive, dependency-ordered development roadmap for the Ruinae chaos/spectral hybrid synthesizer. Every phase maps directly to existing codebase building blocks, identifies gaps, and provides implementation-level detail.

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Existing Building Block Inventory](#existing-building-block-inventory)
3. [Gap Analysis](#gap-analysis)
4. [Phase 0: PolySynthEngine Foundation (COMPLETE)](#phase-0-polysynthengine-foundation-complete)
5. [Phase 1: Trance Gate](#phase-1-trance-gate)
6. [Phase 2: Reverb](#phase-2-reverb)
7. [Phase 3: Ruinae Voice Architecture](#phase-3-ruinae-voice-architecture)
8. [Phase 4: Extended Modulation System](#phase-4-extended-modulation-system)
9. [Phase 5: Effects Section](#phase-5-effects-section)
10. [Phase 6: Ruinae Engine Composition](#phase-6-ruinae-engine-composition)
11. [Phase 7: Plugin Shell](#phase-7-plugin-shell)
12. [Phase 8: UI Design](#phase-8-ui-design)
13. [Phase 9: Presets & Polish](#phase-9-presets--polish)
14. [Dependency Graph](#dependency-graph)
15. [Risk Analysis](#risk-analysis)

---

## Executive Summary

Ruinae is a **chaos/spectral hybrid synthesizer** built around controlled chaos and spectral manipulation. The KrateDSP library already provides **~75% of the required DSP components**. The remaining work falls into three categories:

1. **Orchestration** (composing existing components): ~40% of remaining effort
2. **New DSP components** (Trance Gate, Reverb): ~25% of remaining effort
3. **Plugin shell & UI** (VST3 integration): ~35% of remaining effort

### Critical Path

```
[PolySynthEngine (038) ✅ COMPLETE]
         │
         │   Trance Gate ──────────────┐
         │                             │
         └──────────────────────► Ruinae Voice ──► Ruinae Engine ──► Plugin Shell ──► UI
                                       ▲                ▲
                                       │                │
                              Reverb ──┘                │
                   Extended Mod System ─────────────────┘
```

---

## Existing Building Block Inventory

### Verified Available Components (Cross-Referenced Against Codebase)

Every component below has been verified to exist in the codebase with its exact file path, public API, and layer.

#### OSC A Candidates (Chaos/Classic)

| Component | File | Layer | Key API | Ruinae Role |
|-----------|------|-------|---------|-------------|
| **PolyBlepOscillator** | `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` | 1 | `setWaveform()`, `setFrequency()`, `process()` | Classic waveforms (Sine, Saw, Square, Tri, Pulse) |
| **ChaosOscillator** | `dsp/include/krate/dsp/processors/chaos_oscillator.h` | 2 | `setAttractor()`, `setParams()`, `process()` | Lorenz, Rossler attractor audio |
| **WavetableOscillator** | `dsp/include/krate/dsp/primitives/wavetable_oscillator.h` | 1 | `setFrequency()`, `setPhase()`, `process()` | Wavetable synthesis |
| **PhaseDistortionOscillator** | `dsp/include/krate/dsp/processors/phase_distortion_oscillator.h` | 2 | `setDistortion()`, `process()` | CZ-style synthesis |
| **SyncOscillator** | `dsp/include/krate/dsp/processors/sync_oscillator.h` | 2 | `setSlave()`, `setMasterFrequency()`, `process()` | Hard sync timbres |
| **AdditiveOscillator** | `dsp/include/krate/dsp/processors/additive_oscillator.h` | 2 | `setPartial()`, `process()` | Additive synthesis (up to 64 partials) |
| **SubOscillator** | `dsp/include/krate/dsp/processors/sub_oscillator.h` | 2 | `setFrequency()`, `setWaveform()`, `process()` | Sub-bass reinforcement |
| **NoiseOscillator** | `dsp/include/krate/dsp/primitives/noise_oscillator.h` | 1 | `setColor()`, `process()` | Noise layer |

#### OSC B Candidates (Particle/Formant)

| Component | File | Layer | Key API | Ruinae Role |
|-----------|------|-------|---------|-------------|
| **ParticleOscillator** | `dsp/include/krate/dsp/processors/particle_oscillator.h` | 2 | `setDensity()`, `setFrequencyScatter()`, `processBlock()` | Swarm synthesis (up to 64 particles, magic circle phasor) |
| **FormantOscillator** | `dsp/include/krate/dsp/processors/formant_oscillator.h` | 2 | `setFormant()`, `setGrain()`, `process()` | FOF vocal synthesis |
| **SpectralFreezeOscillator** | `dsp/include/krate/dsp/processors/spectral_freeze_oscillator.h` | 2 | `freeze()`, `process()` | Frozen spectrum playback |

#### Spectral Processing

| Component | File | Layer | Key API | Ruinae Role |
|-----------|------|-------|---------|-------------|
| **SpectralMorphFilter** | `dsp/include/krate/dsp/processors/spectral_morph_filter.h` | 2 | `setMorphAmount()`, `process()` | **Core**: Dual-input spectral morph between OSC A and OSC B |
| **FFT** | `dsp/include/krate/dsp/primitives/fft.h` | 1 | `forward()`, `inverse()` | SIMD-accelerated (pffft) |
| **STFT** | `dsp/include/krate/dsp/primitives/stft.h` | 1 | `analyze()`, `synthesize()` | Short-time Fourier transform |
| **SpectralDistortion** | `dsp/include/krate/dsp/processors/spectral_distortion.h` | 2 | `setSpectralWeight()`, `process()` | FFT-domain distortion |
| **SpectralGate** | `dsp/include/krate/dsp/processors/spectral_gate.h` | 2 | `setThreshold()`, `process()` | Frequency-domain gating |
| **SpectralTilt** | `dsp/include/krate/dsp/processors/spectral_tilt.h` | 2 | `setTilt()`, `process()` | Brightness control |
| **FrequencyShifter** | `dsp/include/krate/dsp/processors/frequency_shifter.h` | 2 | `setShift()`, `process()` | Hilbert-based shifting |
| **HilbertTransform** | `dsp/include/krate/dsp/primitives/hilbert_transform.h` | 1 | `process()` | Analytic signal |

#### Filters

| Component | File | Layer | Key API | Ruinae Role |
|-----------|------|-------|---------|-------------|
| **SVF** | `dsp/include/krate/dsp/primitives/svf.h` | 1 | `setCutoff()`, `setResonance()`, `setMode()`, `process()` | LP/HP/BP/Notch (12dB/oct) |
| **LadderFilter** | `dsp/include/krate/dsp/primitives/ladder_filter.h` | 1 | `setCutoff()`, `setResonance()`, `setDrive()`, `process()` | Moog-style 24dB/oct |
| **FormantFilter** | `dsp/include/krate/dsp/processors/formant_filter.h` | 2 | `setFormant()`, `process()` | Vowel-like filtering |
| **CombFilter** (FF/FB/Schroeder) | `dsp/include/krate/dsp/primitives/comb_filter.h` | 1 | `process()` | Comb/allpass for metallic tones |
| **MultimodeFilter** | `dsp/include/krate/dsp/processors/multimode_filter.h` | 2 | `setCutoff()`, `setResonance()`, `process()` | Multi-output SVF |
| **SelfOscillatingFilter** | `dsp/include/krate/dsp/processors/self_oscillating_filter.h` | 2 | `setCutoff()`, `process()` | Screaming filter FX |
| **EnvelopeFilter** | `dsp/include/krate/dsp/processors/envelope_filter.h` | 2 | `gate()`, `process()` | Auto-wah |
| **Biquad / BiquadCascade** | `dsp/include/krate/dsp/primitives/biquad.h` | 1 | `setCoefficients()`, `process()` | Shelving, peak, parametric EQ |
| **DCBlocker** | `dsp/include/krate/dsp/primitives/dc_blocker.h` | 1 | `process()` | DC offset removal |
| **CrossoverLR4 / 3-Way / 4-Way** | `dsp/include/krate/dsp/processors/crossover_filter.h` | 2 | `setCrossover()`, `process()` | Multiband processing |

#### Distortion

| Component | File | Layer | Key API | Ruinae Role |
|-----------|------|-------|---------|-------------|
| **ChaosWaveshaper** | `dsp/include/krate/dsp/primitives/chaos_waveshaper.h` | 1 | `process()` | Lorenz/Rossler-driven waveshaping |
| **GranularDistortion** | `dsp/include/krate/dsp/processors/granular_distortion.h` | 2 | `setGrainSize()`, `process()` | Micro-grain saturation |
| **SpectralDistortion** | `dsp/include/krate/dsp/processors/spectral_distortion.h` | 2 | (see above) | FFT-domain harmonics |
| **TemporalDistortion** | `dsp/include/krate/dsp/processors/temporal_distortion.h` | 2 | `setGrainSize()`, `process()` | Time-domain manipulation |
| **Waveshaper** | `dsp/include/krate/dsp/primitives/waveshaper.h` | 1 | `setAmount()`, `setType()`, `process()` | Classic waveshaping |
| **Wavefolder** | `dsp/include/krate/dsp/primitives/wavefolder.h` | 1 | `setFrequency()`, `process()` | Sine folding |
| **TapeSaturator** | `dsp/include/krate/dsp/processors/tape_saturator.h` | 2 | `setSaturation()`, `process()` | Tape warmth |
| **TubeStage** | `dsp/include/krate/dsp/processors/tube_stage.h` | 2 | `setDrive()`, `setBias()`, `process()` | Tube emulation |
| **DiodeClipper** | `dsp/include/krate/dsp/processors/diode_clipper.h` | 2 | `setDrive()`, `process()` | Diode nonlinearity |
| **FuzzProcessor** | `dsp/include/krate/dsp/processors/fuzz_processor.h` | 2 | `setFuzz()`, `process()` | Fuzz FX |
| **HardClipADAA** | `dsp/include/krate/dsp/primitives/hard_clip_adaa.h` | 1 | `process()` | Anti-aliased hard clip |
| **TanhADAA** | `dsp/include/krate/dsp/primitives/tanh_adaa.h` | 1 | `process()` | Anti-aliased soft clip |
| **ChebyshevShaper** | `dsp/include/krate/dsp/primitives/chebyshev_shaper.h` | 1 | `setHarmonics()`, `process()` | Harmonic series control |
| **DistortionRack** | `dsp/include/krate/dsp/systems/distortion_rack.h` | 3 | `addDistortion()`, `process()` | Composable distortion chain |

#### Envelopes & Dynamics

| Component | File | Layer | Key API | Ruinae Role |
|-----------|------|-------|---------|-------------|
| **ADSREnvelope** | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | 1 | `gate()`, `process()`, `getStage()` | Amp envelope, filter envelope, mod envelope |
| **MultiStageEnvelope** | `dsp/include/krate/dsp/processors/multi_stage_envelope.h` | 2 | `addStage()`, `gate()`, `process()` | Complex modulation envelopes, loop points |
| **EnvelopeFollower** | `dsp/include/krate/dsp/processors/envelope_follower.h` | 2 | `setAttack()`, `setRelease()`, `process()` | RMS-based tracking |
| **TransientDetector** | `dsp/include/krate/dsp/processors/transient_detector.h` | 2 | `process()`, `getCurrentValue()` | Transient-triggered modulation |
| **DynamicsProcessor** | `dsp/include/krate/dsp/processors/dynamics_processor.h` | 2 | `setThreshold()`, `setRatio()`, `process()` | Compressor/expander |

#### Modulation Sources

| Component | File | Layer | Key API | Ruinae Role |
|-----------|------|-------|---------|-------------|
| **LFO** | `dsp/include/krate/dsp/primitives/lfo.h` | 1 | `setFrequency()`, `setWaveform()`, `process()` | LFO 1 & LFO 2 (Sine/Tri/Saw/Sq/S&H/SmoothRandom, tempo sync) |
| **ChaosModSource** | `dsp/include/krate/dsp/processors/chaos_mod_source.h` | 2 | `process()`, `getCurrentValue()`, implements `ModulationSource` | Chaos LFO (Lorenz attractor as mod source) |
| **SampleHoldSource** | `dsp/include/krate/dsp/processors/sample_hold_source.h` | 2 | `setInput()`, `process()`, implements `ModulationSource` | Sample & Hold modulation |
| **RandomSource** | `dsp/include/krate/dsp/processors/random_source.h` | 2 | `setNoiseColor()`, `process()`, implements `ModulationSource` | Random modulation |
| **PitchFollowerSource** | `dsp/include/krate/dsp/processors/pitch_follower_source.h` | 2 | `getPitch()`, `getCurrentValue()`, implements `ModulationSource` | Pitch-tracking modulation |
| **Rungler** | `dsp/include/krate/dsp/processors/rungler.h` | 2 | `clock()`, `reset()`, `getOutput()` | Shift-register chaos pattern |

#### Modulation Routing

| Component | File | Layer | Key API | Ruinae Role |
|-----------|------|-------|---------|-------------|
| **ModulationMatrix** | `dsp/include/krate/dsp/systems/modulation_matrix.h` | 3 | `registerSource()`, `registerDestination()`, `createRoute()`, `process()` | Central mod routing (16 sources, 16 destinations, 32 routes) |
| **ModulationEngine** | `dsp/include/krate/dsp/systems/modulation_engine.h` | 3 | `registerSource()`, `routeToParameter()`, `getMacroValue()` | Higher-level mod orchestration with 4 macros |

#### Voice Management

| Component | File | Layer | Key API | Ruinae Role |
|-----------|------|-------|---------|-------------|
| **SynthVoice** | `dsp/include/krate/dsp/systems/synth_voice.h` | 3 | `noteOn()`, `noteOff()`, `processBlock()` | Reference pattern (2 PolyBLEP + SVF + 2 ADSR) |
| **VoiceAllocator** | `dsp/include/krate/dsp/systems/voice_allocator.h` | 3 | `noteOn()`, `noteOff()`, `voiceFinished()` | Up to 32 voices, 4 allocation modes, unison |
| **MonoHandler** | `dsp/include/krate/dsp/processors/mono_handler.h` | 2 | `noteOn()`, `noteOff()`, `processPortamento()` | Legato, portamento, note priority |
| **NoteProcessor** | `dsp/include/krate/dsp/processors/note_processor.h` | 2 | `getFrequency()`, `mapVelocity()`, `processPitchBend()` | Pitch bend, velocity curves, tuning |
| **UnisonEngine** | `dsp/include/krate/dsp/systems/unison_engine.h` | 3 | `setVoiceCount()`, `setDetune()`, `setSpread()`, `process()` | Supersaw-style detuned unison |

#### Effects (from Iterum)

| Component | File | Layer | Key API | Ruinae Role |
|-----------|------|-------|---------|-------------|
| **DigitalDelay** | `dsp/include/krate/dsp/effects/digital_delay.h` | 4 | `setTime()`, `setFeedback()`, `setMix()`, `process()` | Clean digital delay FX |
| **TapeDelay** | `dsp/include/krate/dsp/effects/tape_delay.h` | 4 | `setSpeed()`, `setTapeAge()`, `process()` | Tape delay with wow/flutter |
| **PingPongDelay** | `dsp/include/krate/dsp/effects/ping_pong_delay.h` | 4 | `setTime()`, `setSpread()`, `process()` | Stereo ping-pong |
| **GranularDelay** | `dsp/include/krate/dsp/effects/granular_delay.h` | 4 | `setGrainSize()`, `process()` | Granular in feedback |
| **SpectralDelay** | `dsp/include/krate/dsp/effects/spectral_delay.h` | 4 | `setDelayTime()`, `process()` | Per-bin delay |
| **ShimmerDelay** | `dsp/include/krate/dsp/effects/shimmer_delay.h` | 4 | `setShift()`, `process()` | Pitch-shifted feedback |
| **FreezeMode** | `dsp/include/krate/dsp/effects/freeze_mode.h` | 4 | `freeze()`, `process()` | Spectral freeze |

#### Utility

| Component | File | Layer | Key API | Ruinae Role |
|-----------|------|-------|---------|-------------|
| **Oversampler** | `dsp/include/krate/dsp/processors/oversampler.h` | 2 | `upsample()`, `downsample()` | 2x-8x for distortion quality |
| **DiffusionNetwork** | `dsp/include/krate/dsp/processors/diffusion_network.h` | 2 | `setDiffusion()`, `process()` | Allpass diffusion (reverb building block) |
| **PitchShiftProcessor** | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` | 2 | `setShiftSemitones()`, `process()` | Granular/phase vocoder pitch shift |
| **MidSideProcessor** | `dsp/include/krate/dsp/processors/midside_processor.h` | 2 | `setMidLevel()`, `setSideLevel()`, `process()` | Stereo field manipulation |
| **StereoField** | `dsp/include/krate/dsp/systems/stereo_field.h` | 3 | `setWidth()`, `setPan()`, `process()` | Spatial processing |
| **VectorMixer** | `dsp/include/krate/dsp/systems/vector_mixer.h` | 3 | `setPosition()`, `getWeights()` | XY morphing between 4 sources |
| **Sigmoid::tanh** | `dsp/include/krate/dsp/core/sigmoid.h` | 0 | `tanh()` | Soft limiting |
| **EuclideanPattern** | `dsp/include/krate/dsp/core/euclidean_pattern.h` | 0 | `generate()`, `isStepActive()` | Euclidean rhythms (trance gate) |
| **Xorshift32** | `dsp/include/krate/dsp/core/random.h` | 0 | `next()`, `normalized()` | Deterministic PRNG |
| **SequencerCore** | `dsp/include/krate/dsp/primitives/sequencer_core.h` | 1 | `tick()`, `getCurrentStep()` | Step sequencer logic |
| **BlockContext** | `dsp/include/krate/dsp/core/block_context.h` | 0 | `samplesPerBeat()`, `tempoBPM` | Tempo-sync context |

---

## Gap Analysis

### Components That MUST Be Built

| # | Component | Layer | Complexity | Blocked By | Ruinae Signal Path Role | Status |
|---|-----------|-------|-----------|------------|------------------------|--------|
| 1 | ~~PolySynthEngine~~ | 3 | HIGH | -- | Foundation for voice pool management | **COMPLETE** (038) |
| 2 | **TranceGate** | 2 | MEDIUM | Nothing | Rhythmic VCA (post-distortion, pre-VCA) | Pending |
| 3 | **Reverb** | 4 | HIGH | DiffusionNetwork (exists) | Spatial depth in effects chain | Pending |
| 4 | **RuinaeVoice** | 3 | VERY HIGH | TranceGate | Extended voice with full Ruinae signal chain | Pending |
| 5 | **RuinaeEngine** | 3 | HIGH | RuinaeVoice, Reverb | Complete synth engine composition | Pending |
| 6 | **Ruinae Plugin** | N/A | HIGH | RuinaeEngine | VST3 processor, controller, parameters, UI | Pending |

### Components Already Spec'd in Ruinae.md but Existing

| Spec'd Component | Existing Equivalent | Notes |
|-----------------|-------------------|-------|
| ADSR Envelope | `ADSREnvelope` (L1) | Fully implemented with exp/lin/log curves, velocity scaling |
| LFO | `LFO` (L1) | 6 waveforms, tempo sync, all shapes from spec |
| Voice Manager | `VoiceAllocator` (L3) | 32 voices, 4 modes, unison, stealing |
| Modulation Matrix | `ModulationMatrix` (L3) | 16 src, 16 dest, 32 routes, via routing |
| MIDI Handler | Built into VST3 SDK | `IEventList` provides sample-accurate MIDI; `NoteProcessor` handles conversion |
| Sample & Hold | `SampleHoldSource` (L2) | Implements ModulationSource interface |
| Delay Effect | 7 delay types in L4 | DigitalDelay is the simplest; all support tempo sync |

### Components Partially Available

| Need | What Exists | Gap |
|------|------------|-----|
| Chaos LFO (Lorenz mod) | `ChaosModSource` (L2) | Already wraps Lorenz as ModulationSource. May need range/scale config for Ruinae |
| Spectral Freeze (effect) | `FreezeMode` (L4) | Exists as delay feedback processor; need standalone effect wrapper |
| Stereo Delay | All delay types are stereo | Need simple wrapper or use DigitalDelay directly |

---

## Phase 0: PolySynthEngine Foundation -- COMPLETE

**Status**: **COMPLETE** (branch `038-polyphonic-synth-engine`, implemented by parallel agent)

### What This Delivered
- `SynthVoice::setFrequency()` -- legato pitch changes without envelope retrigger
- `PolySynthEngine` class composing: 16 SynthVoice + VoiceAllocator + MonoHandler + NoteProcessor + global SVF + soft limiter
- Poly/Mono mode switching with legato and portamento
- Global filter, gain compensation (1/sqrt(N)), tanh soft limiting
- Unified parameter forwarding to all voices
- Full test suite (lifecycle, poly mode, mono mode, mode switching, global filter, master output, edge cases, performance)

### What Ruinae Inherits
The PolySynthEngine establishes the **voice pool management pattern** that the RuinaeEngine will replicate. The key patterns now proven and available:

1. **Voice pool with pre-allocated array** (`std::array<Voice, 16>`) -- no runtime allocation
2. **VoiceAllocator dispatch** -- noteOn/noteOff event processing, voice stealing, deferred voiceFinished
3. **Mono/Poly mode switching** -- MonoHandler integration, portamento per-sample update, mode transition logic
4. **Gain compensation** -- `1/sqrt(N)` polyphony compensation formula
5. **Soft limiting** -- `Sigmoid::tanh()` as transparent safety limiter
6. **Parameter forwarding** -- iterate all 16 voices for each setter
7. **processBlock pattern** -- pitch bend update, active voice processing, voice lifecycle check

**Decision for Ruinae**: **(B) Copy the pattern** with RuinaeVoice-specific logic. The voice chains are different enough that a template would be over-abstracted. The orchestration code is ~200 lines and well-understood from 038.

---

## Phase 1: Trance Gate

**Layer**: 2 (Processor)
**Blocks**: Phase 2 (part of Ruinae Voice signal chain)
**Effort**: ~2-3 days
**Depends On**: Nothing (can start immediately)

### Why This Exists

The Trance Gate is a **rhythmic energy shaper** placed post-distortion, pre-VCA in the Ruinae voice. It is NOT a hard mute -- it's a pattern-driven, shaped VCA that imposes macro-rhythm while preserving internal chaos. This is a novel component with no existing equivalent in the DSP library.

### Existing Building Blocks to Compose

| Component | Role in TranceGate |
|-----------|--------------------|
| `EuclideanPattern` (L0) | Euclidean rhythm generation (Bjorklund algorithm) |
| `SequencerCore` (L1) | Step sequencer clock and position tracking |
| `BlockContext` (L0) | Tempo sync (BPM, time signature, transport position) |
| `Xorshift32` (L0) | Deterministic PRNG for probability mode |
| `OnePoleSmoother` (L1) | Edge smoothing (attack/release ramps) |

### API Design (from Ruinae.md Functional Spec)

```cpp
namespace Krate::DSP {

struct GateStep {
    float level{1.0f};  // 0.0 - 1.0 (float, not boolean)
};

struct TranceGateParams {
    int numSteps{16};                     // 8, 16, or 32
    float rateHz{4.0f};                   // Free-run rate
    float depth{1.0f};                    // 0 = bypass, 1 = full gate
    float attackMs{2.0f};                 // Step ramp up (1-20ms)
    float releaseMs{10.0f};              // Step ramp down (1-50ms)
    float phaseOffset{0.0f};             // Pattern rotation (0.0-1.0)
    bool tempoSync{true};
    float noteValue{0.25f};              // Step length (1 = quarter note)
    bool perVoice{true};                 // Per-voice vs global clock
};

class TranceGate {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParams(const TranceGateParams& params) noexcept;
    void setTempo(double bpm) noexcept;

    // Pattern control (float levels, not boolean!)
    void setStep(int index, float level) noexcept;
    void setPattern(const std::array<GateStep, 32>& pattern, int numSteps) noexcept;
    void setEuclidean(int hits, int steps, int rotation = 0) noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

    // Modulation output (gate envelope value as mod source)
    [[nodiscard]] float getGateValue() const noexcept;
    [[nodiscard]] int getCurrentStep() const noexcept;

private:
    // Uses EuclideanPattern, SequencerCore, OnePoleSmoother internally
};

} // namespace Krate::DSP
```

### Implementation Details

1. **Pattern Engine**: Float-level steps (not boolean). 0.0 = silent, 1.0 = full, 0.2-0.4 = ghost notes, 0.7-1.0 = accents.
2. **Edge Shaping**: Per-sample one-pole smoother for attack/release ramps. Hard gating impossible by default.
3. **Depth Control**: `g_final(t) = lerp(1.0, g_pattern(t), depth)` -- depth=0 bypasses, depth=1 full effect.
4. **Tempo Sync**: Uses `BlockContext.samplesPerBeat()` for sample-accurate step timing.
5. **Euclidean Patterns**: Delegates to existing `EuclideanPattern` (L0).
6. **Modulation Output**: Gate envelope value exposed as modulation source for routing to filter cutoff, etc.
7. **Per-Voice vs Global**: Mode flag; per-voice resets phase on noteOn, global uses shared clock.

### Test Plan
- Step pattern accuracy (binary, float levels, Euclidean)
- Edge smoothing (no clicks, attack/release ramp measurements)
- Depth control (bypass at 0, full at 1)
- Tempo sync accuracy (verify step boundaries align with beat grid)
- Modulation output range [0, 1]
- Gate does not affect voice lifetime (audio continues through)

### File Locations
- Header: `dsp/include/krate/dsp/processors/trance_gate.h`
- Tests: `dsp/tests/unit/processors/trance_gate_test.cpp`

**status** Finished

---

## Phase 2: Reverb

**Layer**: 4 (Effect) or 3 (System)
**Blocks**: Phase 4 (Effects Section)
**Effort**: ~3-5 days
**Depends On**: Nothing (can run in parallel with Phase 1)

### Why This Is Needed

The Ruinae effects chain requires: Spectral Freeze -> Delay -> **Reverb** -> Output. No reverb exists in the codebase. This is a critical gap for any synthesizer.

### Existing Building Blocks

| Component | Role in Reverb |
|-----------|---------------|
| `DiffusionNetwork` (L2) | Allpass-based diffusion stages -- core reverb building block |
| `DelayLine` (L1) | Delay taps for early reflections and late reverb |
| `OnePoleLP` (L1) | Damping filters (high-frequency absorption) |
| `DCBlocker` (L1) | Remove DC offset from feedback loops |
| `FeedbackComb / SchroederAllpass` (L1) | Comb filter and allpass stages |
| `BiquadCascade` (L1) | Pre/post EQ (tone shaping) |

### Recommended Algorithm: Dattorro Plate Reverb

The Dattorro algorithm is widely used, sounds excellent, and maps cleanly to existing primitives:

```
Input → Pre-delay → Input Diffusion (4 allpass stages)
                          ↓
        ┌─── Tank Loop A ────────────┐
        │   Delay → Damping → Allpass │
        └────────────────────────────┘
                   ↕ cross-feed
        ┌─── Tank Loop B ────────────┐
        │   Delay → Damping → Allpass │
        └────────────────────────────┘
                          ↓
        Output taps (8 taps from tank delays → stereo mix)
```

### API Design

```cpp
namespace Krate::DSP {

struct ReverbParams {
    float roomSize{0.5f};       // 0-1 (maps to tank delay times)
    float damping{0.5f};        // 0-1 (high-frequency absorption)
    float width{1.0f};          // 0-1 (stereo decorrelation)
    float mix{0.3f};            // Dry/wet
    float preDelayMs{20.0f};    // 0-100ms
    float diffusion{0.7f};      // 0-1 (input smearing)
    bool freeze{false};         // Infinite decay
    float modRate{0.5f};        // Internal LFO for chorus (0-2 Hz)
    float modDepth{0.0f};       // 0-1 (subtle pitch modulation in tank)
};

class Reverb {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setParams(const ReverbParams& params) noexcept;

    void process(float& left, float& right) noexcept;
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

private:
    // Input diffusion: 4x SchroederAllpass
    // Tank A/B: DelayLine + OnePoleLP + SchroederAllpass
    // Pre-delay: DelayLine
    // Output: 8 taps mixed to stereo
};

} // namespace Krate::DSP
```

### Implementation Notes
- **Freeze mode**: Set tank feedback to 1.0, input to 0.0 -- infinite sustain
- **Modulation**: Small LFO modulating tank delay times prevents metallic buildup
- **Damping**: OnePoleLP in feedback path -- higher damping = faster HF decay
- **Pre-delay**: Simple DelayLine (up to 100ms)
- **Diffusion**: Uses existing DiffusionNetwork or SchroederAllpass stages
- **Stereo**: Decorrelated output taps from tank at different positions

### File Locations
- Header: `dsp/include/krate/dsp/effects/reverb.h` (or `dsp/include/krate/dsp/systems/reverb.h` if complex enough for L3)
- Tests: `dsp/tests/unit/effects/reverb_test.cpp`

---

## Phase 3: Ruinae Voice Architecture

**Layer**: 3 (System)
**Blocks**: Phase 5 (Ruinae Engine)
**Effort**: ~5-7 days
**Depends On**: Phase 0 (PolySynthEngine pattern -- COMPLETE), Phase 1 (TranceGate)

### Signal Flow (Per Voice)

This is the core of Ruinae -- each voice has this signal chain:

```
┌──────────────────────────────────────────────────────────────────┐
│                       RUINAE VOICE                                │
│                                                                  │
│  ┌─────────────┐     ┌─────────────┐                            │
│  │   OSC A     │     │   OSC B     │                            │
│  │ Selector:   │     │ Selector:   │                            │
│  │  PolyBLEP   │     │  Particle   │                            │
│  │  Chaos      │     │  Formant    │                            │
│  │  Wavetable  │     │  Spectral   │                            │
│  │  PhaseDist  │     │  Additive   │                            │
│  │  Sync       │     │  PolyBLEP   │                            │
│  │  Additive   │     │  Chaos      │                            │
│  └──────┬──────┘     └──────┬──────┘                            │
│         │                   │                                    │
│         └────────┬──────────┘                                    │
│                  ▼                                                │
│  ┌───────────────────────────┐                                   │
│  │  Mixer / SpectralMorph    │◄── Morph Position (mod dest)     │
│  │  Mode: CrossfadeMix |     │◄── Morph Mode select             │
│  │        SpectralMorph      │                                   │
│  └───────────┬───────────────┘                                   │
│              ▼                                                    │
│  ┌───────────────────────────┐                                   │
│  │    Filter Section         │◄── Cutoff (mod dest)             │
│  │    Selector:              │◄── Resonance (mod dest)          │
│  │     SVF (LP/HP/BP/Notch)  │◄── Filter Env Amount            │
│  │     Ladder (24dB)         │◄── Key Tracking                  │
│  │     Formant               │                                   │
│  │     Comb                  │                                   │
│  └───────────┬───────────────┘                                   │
│              ▼                                                    │
│  ┌───────────────────────────┐                                   │
│  │   Distortion Section      │◄── Drive (mod dest)             │
│  │   Selector:               │◄── Character (mod dest)          │
│  │    ChaosWaveshaper        │                                   │
│  │    SpectralDistortion     │                                   │
│  │    GranularDistortion     │                                   │
│  │    Wavefolder             │                                   │
│  │    TapeSaturator          │                                   │
│  │    Clean (bypass)         │                                   │
│  └───────────┬───────────────┘                                   │
│              ▼                                                    │
│  ┌───────────────────────────┐                                   │
│  │     Trance Gate           │◄── Pattern, Depth (mod dest)    │
│  │     (Rhythmic VCA)        │◄── Rate (mod dest)               │
│  │     Optional, per-voice   │──► Gate Value (mod source)       │
│  └───────────┬───────────────┘                                   │
│              ▼                                                    │
│  ┌───────────────────────────┐                                   │
│  │       VCA                 │◄── Amp Envelope                  │
│  │  output *= ampEnv.process │                                   │
│  └───────────┬───────────────┘                                   │
│              │                                                    │
│  Modulation Sources (per-voice):                                 │
│    ENV 1 (Amp ADSR)                                              │
│    ENV 2 (Filter ADSR)                                           │
│    ENV 3 (Mod ADSR, optional)                                    │
│    Voice-local LFO                                               │
│                                                                  │
└──────────────┬───────────────────────────────────────────────────┘
               ▼ (to engine mixer)
```

### Oscillator Selection Architecture

Rather than hard-coding two oscillator types, each oscillator slot should be a **selectable oscillator** that wraps multiple implementations:

```cpp
enum class OscType : uint8_t {
    // Classic (OSC A primary candidates)
    PolyBLEP,          // Saw, Square, Triangle, Pulse, Sine
    Wavetable,         // Arbitrary wavetable
    PhaseDistortion,   // CZ-style
    Sync,              // Hard sync
    Additive,          // Sum of partials

    // Experimental (OSC B primary candidates)
    Chaos,             // Lorenz, Rossler attractors
    Particle,          // Swarm synthesis
    Formant,           // FOF vocal
    SpectralFreeze,    // Frozen spectrum
    Noise,             // Colored noise

    NumTypes
};
```

**Implementation strategy**: A `SelectableOscillator` class that holds one instance of each type (pre-allocated) and delegates `process()` to the active type. This avoids runtime polymorphism (virtual dispatch) and maintains real-time safety.

### Mixer Architecture

Two mixing modes:
1. **CrossfadeMix**: Simple `output = oscA * (1-mix) + oscB * mix` -- CPU-cheap, used for basic timbral blending
2. **SpectralMorph**: Routes both oscillators through `SpectralMorphFilter` -- CPU-expensive, used for spectral interpolation between timbres

### Filter Section

Selectable filter types, all already implemented:
- SVF (LP/HP/BP/Notch) -- lightweight, versatile
- LadderFilter -- Moog-style resonant character
- FormantFilter -- vowel-like filtering
- CombFilter -- metallic/flanging tones

Each with modulated cutoff (envelope + key tracking + mod matrix destination) and resonance.

### Distortion Section

Selectable distortion algorithms, all already implemented. Uses the existing `DistortionRack` (L3) pattern or a simpler selector:
- ChaosWaveshaper -- the signature Ruinae sound
- SpectralDistortion -- FFT-domain harmonics
- GranularDistortion -- micro-grain saturation
- Wavefolder -- sine folding
- TapeSaturator -- warmth
- Clean (bypass)

### Per-Voice Modulation

Each voice has its own modulation sources:
- **ENV 1** (ADSREnvelope) -- amplitude
- **ENV 2** (ADSREnvelope) -- filter modulation
- **ENV 3** (ADSREnvelope, optional) -- general modulation
- **Voice LFO** (LFO) -- per-voice modulation with optional key tracking

These connect to voice-local mod destinations (filter cutoff, morph position, distortion drive, gate depth) via a lightweight per-voice mod routing.

### API Design

```cpp
namespace Krate::DSP {

class RuinaeVoice {
public:
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Note control
    void noteOn(float frequency, float velocity) noexcept;
    void noteOff() noexcept;
    void setFrequency(float hz) noexcept;  // Legato pitch change
    [[nodiscard]] bool isActive() const noexcept;

    // OSC A
    void setOscAType(OscType type) noexcept;
    void setOscAParams(/* type-specific params */) noexcept;

    // OSC B
    void setOscBType(OscType type) noexcept;
    void setOscBParams(/* type-specific params */) noexcept;

    // Mixer
    void setMixMode(MixMode mode) noexcept;  // Crossfade or SpectralMorph
    void setMixPosition(float mix) noexcept;  // 0=A, 1=B

    // Filter
    void setFilterType(RuinaeFilterType type) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float q) noexcept;
    void setFilterEnvAmount(float semitones) noexcept;
    void setFilterKeyTrack(float amount) noexcept;

    // Distortion
    void setDistortionType(RuinaeDistortionType type) noexcept;
    void setDistortionDrive(float drive) noexcept;
    void setDistortionCharacter(float character) noexcept;

    // Trance Gate
    void setTranceGateEnabled(bool enabled) noexcept;
    void setTranceGateParams(const TranceGateParams& params) noexcept;
    void setTranceGateTempo(double bpm) noexcept;

    // Envelopes
    ADSREnvelope& getAmpEnvelope() noexcept;
    ADSREnvelope& getFilterEnvelope() noexcept;
    ADSREnvelope& getModEnvelope() noexcept;

    // Processing
    void processBlock(float* output, size_t numSamples) noexcept;

private:
    // Oscillators (pre-allocated, one of each type)
    PolyBlepOscillator polyBlepOscA_, polyBlepOscB_;
    ChaosOscillator chaosOscA_, chaosOscB_;
    ParticleOscillator particleOsc_;
    FormantOscillator formantOsc_;
    // ... etc

    // Mixer
    SpectralMorphFilter spectralMorph_;

    // Filter (selectable)
    SVF svfFilter_;
    LadderFilter ladderFilter_;
    FormantFilter formantFilter_;
    FeedbackComb combFilter_;

    // Distortion (selectable)
    ChaosWaveshaper chaosWaveshaper_;
    SpectralDistortion spectralDistortion_;
    GranularDistortion granularDistortion_;
    Wavefolder wavefolder_;
    TapeSaturator tapeSaturator_;

    // Gate
    TranceGate tranceGate_;

    // Envelopes
    ADSREnvelope ampEnv_, filterEnv_, modEnv_;

    // Scratch buffers
    std::vector<float> oscABuffer_, oscBBuffer_, mixBuffer_;
};

} // namespace Krate::DSP
```

### Memory Considerations

Pre-allocating all oscillator/filter/distortion variants per voice will be memory-heavy. Estimated per-voice footprint:

| Component | Estimated Size | Notes |
|-----------|---------------|-------|
| All oscillators (2 slots x ~8 types) | ~20 KB | ParticleOscillator is largest (~17KB for 64 particles) |
| SpectralMorphFilter | ~16 KB | FFT buffers (1024-point) |
| All filters | ~1 KB | SVF/Ladder/Formant/Comb are small |
| All distortions | ~8 KB | SpectralDistortion has FFT buffers |
| TranceGate | ~0.5 KB | Pattern + smoothers |
| 3 ADSR Envelopes | ~0.1 KB | Very lightweight |
| Scratch buffers | ~8 KB | 3 x maxBlockSize x float |
| **Total per voice** | **~54 KB** | |
| **16 voices** | **~864 KB** | Under 1MB, acceptable |

**Optimization opportunity**: Lazy-initialize oscillator types (only the active type gets prepared). This reduces working set significantly since most patches use 2-3 oscillator types total.

### File Locations
- Header: `dsp/include/krate/dsp/systems/ruinae_voice.h`
- Types: `dsp/include/krate/dsp/systems/ruinae_types.h` (enums, param structs)
- Tests: `dsp/tests/unit/systems/ruinae_voice_test.cpp`

---

## Phase 4: Extended Modulation System

**Layer**: 3 (System)
**Blocks**: Phase 6 (Ruinae Engine)
**Effort**: ~3-4 days
**Depends On**: Phase 3 (RuinaeVoice exposes mod destinations)

### Architecture

Ruinae's modulation system operates at two levels:

#### Level 1: Per-Voice Modulation (Voice-Local)

Each voice has private modulation sources and destinations:

**Sources** (per-voice):
| Source | Implementation | Output Range |
|--------|---------------|-------------|
| ENV 1 (Amp) | `ADSREnvelope` | [0, 1] |
| ENV 2 (Filter) | `ADSREnvelope` | [0, 1] |
| ENV 3 (Mod) | `ADSREnvelope` | [0, 1] |
| Voice LFO | `LFO` | [-1, +1] or [0, 1] |
| Gate Output | `TranceGate::getGateValue()` | [0, 1] |
| Velocity | Stored float | [0, 1] |
| Key Track | Note number / frequency | Normalized |
| Aftertouch | Per-note (MPE future) | [0, 1] |

**Destinations** (per-voice):
| Destination | Parameter | Range |
|-------------|-----------|-------|
| Filter Cutoff | `setFilterCutoff()` | 20-20000 Hz |
| Filter Resonance | `setFilterResonance()` | 0.1-30.0 |
| Morph Position | `setMixPosition()` | 0.0-1.0 |
| Distortion Drive | `setDistortionDrive()` | 0.0-1.0 |
| Trance Gate Depth | via TranceGateParams | 0.0-1.0 |
| OSC A Pitch | Semitones offset | -48 to +48 |
| OSC B Pitch | Semitones offset | -48 to +48 |
| OSC A Level | Gain | 0.0-1.0 |
| OSC B Level | Gain | 0.0-1.0 |

**Implementation**: A lightweight `VoiceModRouter` that computes mod values per-sample and applies them to voice parameters. This is simpler than the full ModulationMatrix because it operates within a single voice scope:

```cpp
struct VoiceModRoute {
    VoiceModSource source;
    VoiceModDest destination;
    float amount;  // bipolar: -1 to +1
};

class VoiceModRouter {
    static constexpr int kMaxRoutes = 16;
    std::array<VoiceModRoute, kMaxRoutes> routes_;
    int routeCount_{0};

    void computeModulations(/* source values */) noexcept;
    float getModulatedValue(VoiceModDest dest, float baseValue) const noexcept;
};
```

#### Level 2: Global Modulation (Engine-Wide)

The engine-level modulation uses the existing `ModulationMatrix` (L3) and `ModulationEngine` (L3):

**Global Sources**:
| Source | Implementation | Notes |
|--------|---------------|-------|
| LFO 1 | `LFO` (L1) | Shared across all voices |
| LFO 2 | `LFO` (L1) | Shared across all voices |
| Chaos Mod | `ChaosModSource` (L2) | Lorenz attractor output |
| Rungler | `Rungler` (L2) | Shift-register chaos |
| Env Follower | `EnvelopeFollower` (L2) | Sidechain / input tracking |
| Macro 1-4 | `ModulationEngine` (L3) | User-assignable macros |
| Pitch Bend | `NoteProcessor` (L2) | MIDI pitch wheel |
| Mod Wheel | CC#1 value | MIDI mod wheel |

**Global Destinations**:
| Destination | What It Controls |
|-------------|-----------------|
| Global Filter Cutoff | Post-mix SVF |
| Global Filter Resonance | Post-mix SVF |
| Master Volume | Engine output gain |
| Effect Mix (Freeze/Delay/Reverb) | Effect wet/dry |
| All Voice Filter Cutoff | Forwarded to all voices |
| All Voice Morph Position | Forwarded to all voices |
| Trance Gate Rate | Forwarded to all voice gates |

**Implementation**: Compose the existing `ModulationEngine` into the RuinaeEngine. Register sources and destinations at `prepare()` time. Call `process()` once per block to update all modulated values.

### File Locations
- Voice mod router: Inside `ruinae_voice.h` or separate `dsp/include/krate/dsp/systems/voice_mod_router.h`
- Engine mod setup: Inside `ruinae_engine.h`
- Tests: `dsp/tests/unit/systems/ruinae_modulation_test.cpp`

---

## Phase 5: Effects Section

**Layer**: 4 (Effects) / 3 (System)
**Blocks**: Phase 6 (Ruinae Engine)
**Effort**: ~2-3 days
**Depends On**: Phase 2 (Reverb)

### Effects Chain

```
Voice Sum ──► Spectral Freeze ──► Delay ──► Reverb ──► Output
                   │                │          │
                   ▼                ▼          ▼
              [freeze toggle]  [selectable]  [from Phase 2]
```

### Spectral Freeze (Already Exists)

The `FreezeMode` (L4) at `dsp/include/krate/dsp/effects/freeze_mode.h` implements freeze as a feedback processor. For Ruinae, wrap it as a standalone insert effect:

```cpp
class SpectralFreezeEffect {
    FreezeFeedbackProcessor freezeProcessor_;
    bool frozen_{false};

    void setFreeze(bool freeze) noexcept;
    void processBlock(float* left, float* right, size_t numSamples) noexcept;
};
```

**Alternatively**, the `SpectralFreezeOscillator` (L2) can be used as a "frozen grain" sustain effect. Decision deferred to implementation.

### Delay (Already Exists)

Multiple delay types available. Ruinae should offer a selectable delay:
- **Digital** (`DigitalDelay`, L4) -- clean, tempo-synced
- **Tape** (`TapeDelay`, L4) -- warm, degraded
- **Ping-Pong** (`PingPongDelay`, L4) -- stereo bounce
- **Granular** (`GranularDelay`, L4) -- texture
- **Spectral** (`SpectralDelay`, L4) -- per-bin delays

All are already fully implemented and tested in the Iterum plugin.

### Reverb (From Phase 2)

The Dattorro reverb built in Phase 2.

### Effects Router

```cpp
class RuinaeEffectsChain {
public:
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Freeze
    void setFreezeEnabled(bool enabled) noexcept;
    void setFreeze(bool frozen) noexcept;

    // Delay
    void setDelayType(RuinaeDelayType type) noexcept;
    void setDelayTime(float ms) noexcept;
    void setDelayFeedback(float amount) noexcept;
    void setDelayMix(float mix) noexcept;
    void setDelayTempo(double bpm) noexcept;

    // Reverb
    void setReverbParams(const ReverbParams& params) noexcept;

    // Processing (stereo)
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

private:
    SpectralFreezeEffect freeze_;
    DigitalDelay digitalDelay_;
    TapeDelay tapeDelay_;
    PingPongDelay pingPongDelay_;
    GranularDelay granularDelay_;
    SpectralDelay spectralDelay_;
    Reverb reverb_;
    RuinaeDelayType activeDelayType_;
};
```

### File Locations
- Header: `dsp/include/krate/dsp/systems/ruinae_effects_chain.h`
- Tests: `dsp/tests/unit/systems/ruinae_effects_chain_test.cpp`

---

## Phase 6: Ruinae Engine Composition

**Layer**: 3 (System)
**Blocks**: Phase 7 (Plugin Shell)
**Effort**: ~4-5 days
**Depends On**: Phase 0, Phase 3, Phase 4, Phase 5

### Architecture

The RuinaeEngine is the top-level DSP system that composes everything:

```
MIDI Events ──► RuinaeEngine
                    │
                    ├── NoteProcessor (pitch bend, velocity)
                    ├── VoiceAllocator (poly) / MonoHandler (mono)
                    │
                    ├── Voice Pool (16 x RuinaeVoice)
                    │     ├── OSC A + OSC B
                    │     ├── SpectralMorph / CrossfadeMix
                    │     ├── Filter (SVF/Ladder/Formant/Comb)
                    │     ├── Distortion (Chaos/Spectral/Granular/...)
                    │     ├── TranceGate (rhythmic VCA)
                    │     └── VCA (amp envelope)
                    │
                    ├── Voice Mixer (sum + gain compensation)
                    │
                    ├── Global Modulation
                    │     ├── LFO 1 + LFO 2
                    │     ├── Chaos Mod (Lorenz)
                    │     ├── Rungler
                    │     ├── Sample & Hold
                    │     └── ModulationEngine (matrix + macros)
                    │
                    ├── Global Filter (optional SVF)
                    │
                    ├── Effects Chain
                    │     ├── Spectral Freeze
                    │     ├── Delay (selectable type)
                    │     └── Reverb
                    │
                    └── Master Output (gain comp + soft limiter)
```

### API Design

```cpp
namespace Krate::DSP {

class RuinaeEngine {
public:
    static constexpr size_t kMaxPolyphony = 16;

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Note dispatch
    void noteOn(uint8_t note, uint8_t velocity) noexcept;
    void noteOff(uint8_t note) noexcept;

    // Mode
    void setMode(VoiceMode mode) noexcept;
    void setPolyphony(size_t count) noexcept;

    // Pitch bend
    void setPitchBend(float bipolar) noexcept;

    // === Voice Parameters (forwarded to all voices) ===

    // OSC A
    void setOscAType(OscType type) noexcept;
    // ... type-specific params

    // OSC B
    void setOscBType(OscType type) noexcept;
    // ... type-specific params

    // Mixer
    void setMixMode(MixMode mode) noexcept;
    void setMixPosition(float mix) noexcept;

    // Filter
    void setFilterType(RuinaeFilterType type) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float q) noexcept;
    void setFilterEnvAmount(float semitones) noexcept;
    void setFilterKeyTrack(float amount) noexcept;

    // Distortion
    void setDistortionType(RuinaeDistortionType type) noexcept;
    void setDistortionDrive(float drive) noexcept;

    // Trance Gate
    void setTranceGateEnabled(bool enabled) noexcept;
    void setTranceGateParams(const TranceGateParams& params) noexcept;

    // Envelopes (forwarded to all voices)
    void setAmpAttack(float ms) noexcept;
    void setAmpDecay(float ms) noexcept;
    void setAmpSustain(float level) noexcept;
    void setAmpRelease(float ms) noexcept;
    void setFilterAttack(float ms) noexcept;
    // ... etc

    // === Global Modulation ===
    void setLFO1Rate(float hz) noexcept;
    void setLFO1Shape(LFOShape shape) noexcept;
    void setLFO2Rate(float hz) noexcept;
    void setLFO2Shape(LFOShape shape) noexcept;
    void setChaosModRate(float rate) noexcept;
    void setModRoute(int slot, ModSource src, ModDest dest, float amount) noexcept;

    // === Global Filter ===
    void setGlobalFilterEnabled(bool enabled) noexcept;
    void setGlobalFilterCutoff(float hz) noexcept;
    void setGlobalFilterResonance(float q) noexcept;

    // === Effects ===
    void setFreezeEnabled(bool enabled) noexcept;
    void setFreeze(bool frozen) noexcept;
    void setDelayType(RuinaeDelayType type) noexcept;
    void setDelayTime(float ms) noexcept;
    void setDelayFeedback(float amount) noexcept;
    void setDelayMix(float mix) noexcept;
    void setReverbSize(float size) noexcept;
    void setReverbDamping(float damping) noexcept;
    void setReverbMix(float mix) noexcept;

    // === Master ===
    void setMasterGain(float gain) noexcept;
    void setSoftLimitEnabled(bool enabled) noexcept;

    // === Processing ===
    void processBlock(float* left, float* right, size_t numSamples) noexcept;

    // === Tempo ===
    void setTempo(double bpm) noexcept;
    void setBlockContext(const BlockContext& ctx) noexcept;

    // === Queries ===
    [[nodiscard]] size_t getActiveVoiceCount() const noexcept;
    [[nodiscard]] VoiceMode getMode() const noexcept;

private:
    std::array<RuinaeVoice, kMaxPolyphony> voices_;
    VoiceAllocator allocator_;
    MonoHandler monoHandler_;
    NoteProcessor noteProcessor_;

    // Global modulation
    LFO lfo1_, lfo2_;
    ChaosModSource chaosMod_;
    Rungler rungler_;
    SampleHoldSource sampleHold_;
    ModulationEngine modEngine_;

    // Global filter
    SVF globalFilter_;
    bool globalFilterEnabled_{false};

    // Effects
    RuinaeEffectsChain effects_;

    // Master
    float masterGain_{1.0f};
    float gainCompensation_;
    bool softLimitEnabled_{true};

    // Voice mixing scratch buffers
    std::vector<float> voiceScratch_;
    std::vector<float> mixBufferL_, mixBufferR_;
};

} // namespace Krate::DSP
```

### Processing Flow (processBlock)

```
1. Update tempo/block context on all tempo-synced components
2. Process global modulation sources (LFO1, LFO2, Chaos, Rungler, S&H)
3. Run modulation matrix (compute all modulated parameter values)
4. Apply global mod values to voice parameters (filter cutoff offset, morph, etc.)
5. Process pitch bend smoother
6. For each active voice:
   a. Update frequency (pitch bend + portamento in mono mode)
   b. processBlock into voice scratch buffer
   c. Sum into mix buffers (stereo: L/R based on voice pan/spread)
7. Apply global filter (if enabled)
8. Process effects chain (freeze -> delay -> reverb)
9. Apply master gain with 1/sqrt(N) compensation
10. Apply soft limiter (if enabled)
11. Check voice lifecycle (deferred voiceFinished notifications)
```

### Stereo Considerations

The existing SynthVoice outputs mono. For Ruinae, stereo output is important:
- **Voice panning**: Each voice gets a pan position (from unison spread or mod)
- **Stereo delay/reverb**: Effects chain is inherently stereo
- **Width control**: Post-voice-sum stereo width via `StereoField` (L3)

### File Locations
- Header: `dsp/include/krate/dsp/systems/ruinae_engine.h`
- Tests: `dsp/tests/unit/systems/ruinae_engine_test.cpp`

---

## Phase 7: Plugin Shell

**Layer**: Plugin (not DSP layer)
**Blocks**: Phase 8 (UI)
**Effort**: ~5-7 days
**Depends On**: Phase 6 (RuinaeEngine)

### Plugin Structure (Following Iterum/Disrumpo Pattern)

```
plugins/ruinae/
├── CMakeLists.txt
├── version.json
├── src/
│   ├── entry.cpp                    # VST3 factory registration
│   ├── plugin_ids.h                 # GUIDs + parameter IDs
│   ├── version.h.in                 # Version template
│   ├── processor/
│   │   ├── processor.h
│   │   └── processor.cpp            # Audio thread: RuinaeEngine + MIDI dispatch
│   ├── controller/
│   │   ├── controller.h
│   │   └── controller.cpp           # UI thread: parameter registration + editor
│   ├── parameters/
│   │   ├── osc_a_params.h           # OSC A atomic params + registration
│   │   ├── osc_b_params.h           # OSC B atomic params + registration
│   │   ├── mixer_params.h           # Mixer params
│   │   ├── filter_params.h          # Filter params
│   │   ├── distortion_params.h      # Distortion params
│   │   ├── trance_gate_params.h     # Trance Gate params
│   │   ├── envelope_params.h        # 3 ADSR envelope params
│   │   ├── modulation_params.h      # LFO, chaos, mod matrix params
│   │   ├── effects_params.h         # Freeze, delay, reverb params
│   │   ├── master_params.h          # Master gain, limiter, polyphony
│   │   └── global_filter_params.h   # Global filter params
│   ├── ui/
│   │   └── (custom VSTGUI views)
│   └── preset/
│       └── ruinae_preset_config.h   # Preset categories
├── resources/
│   ├── editor.uidesc                # VSTGUI XML layout
│   ├── bitmaps/                     # UI graphics
│   └── win32resource.rc.in          # Windows resources
└── tests/
    ├── unit/                        # Parameter tests
    └── integration/                 # Plugin + DSP integration tests
```

### Parameter ID Allocation

Following the project convention (`k{Section}{Parameter}Id`), with 100-ID gaps:

```
Parameter ID Ranges:
  0-99:    Global (Mode, Polyphony, Master Gain, Soft Limit)
  100-199: OSC A (Type, Waveform, Tune, Detune, Level, Phase, ...)
  200-299: OSC B (Type, Waveform, Tune, Detune, Level, Phase, ...)
  300-399: Mixer (Mode, Position, SpectralMorphMode)
  400-499: Filter (Type, Cutoff, Resonance, EnvAmount, KeyTrack)
  500-599: Distortion (Type, Drive, Character, Mix)
  600-699: Trance Gate (Enabled, Pattern, Rate, Depth, Attack, Release, ...)
  700-799: Amp Envelope (Attack, Decay, Sustain, Release, Curves)
  800-899: Filter Envelope (Attack, Decay, Sustain, Release, Curves)
  900-999: Mod Envelope (Attack, Decay, Sustain, Release, Curves)
  1000-1099: LFO 1 (Rate, Shape, Depth, Sync, ...)
  1100-1199: LFO 2 (Rate, Shape, Depth, Sync, ...)
  1200-1299: Chaos Mod (Rate, Type, Depth)
  1300-1399: Modulation Matrix (Source, Dest, Amount x 8 slots)
  1400-1499: Global Filter (Enabled, Type, Cutoff, Resonance)
  1500-1599: Freeze Effect (Enabled, Freeze Toggle)
  1600-1699: Delay Effect (Type, Time, Feedback, Mix, Sync, ...)
  1700-1799: Reverb (Size, Damping, Width, Mix, PreDelay, ...)
  1800-1899: Mono Mode (Priority, Legato, Portamento Time, PortaMode)
  1900-1999: Reserved for expansion
```

### Processor Implementation

```cpp
class RuinaeProcessor : public Steinberg::Vst::AudioEffect {
    Krate::DSP::RuinaeEngine engine_;

    // Atomic parameter packs (one per section)
    OscAParams oscAParams_;
    OscBParams oscBParams_;
    MixerParams mixerParams_;
    FilterParams filterParams_;
    // ... etc

    // process():
    //   1. processParameterChanges(data.inputParameterChanges)
    //   2. processEvents(data.inputEvents)  // MIDI noteOn/noteOff
    //   3. Build BlockContext from host tempo
    //   4. engine_.setBlockContext(ctx)
    //   5. engine_.processBlock(outputL, outputR, numSamples)
};
```

### Build Integration

Add to root `CMakeLists.txt`:
```cmake
add_subdirectory(plugins/ruinae)
```

Plugin `CMakeLists.txt` follows the Iterum pattern:
```cmake
smtg_add_vst3plugin(Ruinae ...)
target_link_libraries(Ruinae PRIVATE sdk vstgui_support KrateDSP KratePluginsShared)
```

---

## Phase 8: UI Design

**Layer**: Plugin (VSTGUI)
**Effort**: ~5-10 days
**Depends On**: Phase 7 (Plugin Shell with parameters)

### UI Layout Concept

```
┌─────────────────────────────────────────────────────────────────────────┐
│  RUINAE                                                    [Preset ▼]  │
├─────────────┬──────────────┬───────────────┬───────────────┬───────────┤
│   OSC A     │    OSC B     │    MIXER      │   FILTER      │   DIST    │
│ [Type ▼]    │ [Type ▼]     │ [Mode ▼]      │ [Type ▼]      │ [Type ▼]  │
│  Tune       │  Tune        │  Position     │  Cutoff       │  Drive    │
│  Detune     │  Detune      │    ◯          │  Resonance    │  Char     │
│  Level      │  Level       │               │  Env Amt      │  Mix      │
│  [params]   │  [params]    │               │  Key Track    │           │
├─────────────┴──────────────┴───────────────┴───────────────┴───────────┤
│  TRANCE GATE              │  ENVELOPES                                 │
│  [On/Off] [Pattern View]  │  ┌─AMP──┐ ┌─FILTER┐ ┌─MOD───┐           │
│  Rate  Depth  Atk  Rel    │  │ ADSR │ │ ADSR  │ │ ADSR  │           │
│  [Steps: ○●○●○●○●○●○●○●]  │  └──────┘ └───────┘ └───────┘           │
├────────────────────────────┴───────────────────────────────────────────┤
│  MODULATION                                                            │
│  LFO1: Rate Shape Depth  │  LFO2: Rate Shape Depth  │  Chaos: Rate   │
│  [Matrix: 8 slots]       │  Src ▼  →  Dest ▼  Amt   │  Rungler  S&H  │
├────────────────────────────────────────────────────────────────────────┤
│  EFFECTS                                                               │
│  [FREEZE]    [DELAY]              [REVERB]              [MASTER]      │
│  ◯ On/Off    Type ▼               Size    Damping       Gain          │
│              Time  Feedback       Width   Mix           Polyphony ▼   │
│              Mix   Sync           PreDly  Freeze        Soft Limit    │
└────────────────────────────────────────────────────────────────────────┘
```

### Custom VSTGUI Views Needed

1. **StepPatternEditor**: Interactive trance gate pattern editor (similar to Iterum's `TapPatternEditor` but with float levels instead of boolean)
2. **XYMorphPad**: 2D pad for spectral morph position (reuse `VectorMixer` concept)
3. **ADSRDisplay**: Visual ADSR curve display (common synth UI element)
4. **ModMatrixGrid**: Modulation routing display (source x destination grid)
5. **OscillatorTypeSelector**: Visual oscillator type chooser with waveform previews

### VSTGUI Patterns (From Existing Codebase)

All custom views follow the patterns in `plugins/shared/src/ui/` and use:
- `CView` / `CControl` base classes
- `IDependent` for parameter change notifications
- Sub-controllers for section visibility
- `editor.uidesc` XML for layout

---

## Phase 9: Presets & Polish

**Effort**: ~3-5 days
**Depends On**: Phase 8 (UI complete)

### Factory Presets

Organize presets by character category:

```
presets/
├── Pads/
│   ├── Chaos Drift.vstpreset       (Chaos osc A + Particle osc B, slow morph, long ADSR)
│   ├── Spectral Cathedral.vstpreset (Additive + Formant, spectral morph, reverb heavy)
│   └── Frozen Time.vstpreset        (Any osc + freeze effect, infinite sustain)
├── Leads/
│   ├── Aggressive Chaos.vstpreset   (Chaos osc, ladder filter, chaos waveshaper)
│   ├── Screaming Resonance.vstpreset (Sync osc, self-oscillating filter)
│   └── Rungler Lead.vstpreset       (Classic osc + Rungler mod on filter)
├── Bass/
│   ├── Sub Chaos.vstpreset          (Chaos osc + sub, ladder filter, mono mode)
│   ├── Formant Bass.vstpreset       (Formant osc, LP filter, mono legato)
│   └── Distorted Engine.vstpreset   (Saw + chaos waveshaper, mono)
├── Textures/
│   ├── Particle Cloud.vstpreset     (Particle osc, trance gate, granular delay)
│   ├── Glitch Fabric.vstpreset      (Granular distortion, spectral delay)
│   └── Lorenz Meditation.vstpreset  (Chaos mod → everything, slow LFO)
├── Rhythmic/
│   ├── Gated Chaos.vstpreset        (Trance gate, fast patterns, chaos osc)
│   ├── Euclidean Pulse.vstpreset    (Euclidean pattern gate, particle osc)
│   └── Polyrhythm Engine.vstpreset  (Multiple gate rates, complex pattern)
└── Experimental/
    ├── Pure Noise.vstpreset         (Noise + spectral morph + freeze)
    ├── Feedback Loop.vstpreset      (Comb filter + delay feedback > 1)
    └── Dimension Shift.vstpreset    (Freq shifter + shimmer + chaos)
```

### Performance Optimization Pass

Target: **< 10% CPU for 8 voices at 44.1kHz** (the Ruinae voice is much more complex than SynthVoice)

Optimization opportunities (based on Memory notes):
1. **Lazy oscillator init**: Only `prepare()` the active oscillator type per slot
2. **Skip inactive sections**: Bypass distortion/trance gate processing when disabled
3. **Block processing**: Ensure all components use `processBlock()` not per-sample `process()`
4. **SpectralMorph optimization**: Use smaller FFT (512 instead of 1024) when possible
5. **Shared FFT plans**: Reuse pffft plans across voices
6. **Cache-friendly iteration**: Process all voices for one stage before moving to next stage (if benchmarking shows benefit -- per memory notes, this may not help when data fits in L1)

### Testing Checklist
- [ ] All DSP tests pass
- [ ] Plugin builds on Windows (MSVC), macOS (Clang), Linux (GCC)
- [ ] Pluginval level 5 passes
- [ ] Clang-tidy clean
- [ ] No compiler warnings
- [ ] CPU benchmark within budget
- [ ] All presets load and produce expected sound
- [ ] State save/load round-trips correctly
- [ ] UI renders correctly on all platforms

---

## Dependency Graph

```
Phase 0: PolySynthEngine (038) ✅ COMPLETE
   │
   │  Phase 1: Trance Gate ──────────────────────┐
   │     │  [2-3 days]                           │
   │     │                                       │
   │  Phase 2: Reverb ──────────────────────┐    │
   │     │  [3-5 days, parallel with P1]    │    │
   │     │                                  │    │
   │     │                                  ▼    ▼
   └────────────────────────────────► Phase 3: Ruinae Voice
                                        │  [5-7 days]
                                        │
                                        ▼
                                     Phase 4: Ext Modulation
                                        │  [3-4 days]
                                        │
                                        ▼
                                     Phase 5: Effects Section
                                        │  [2-3 days]
                                        │
                                        ▼
                                     Phase 6: Ruinae Engine
                                        │  [4-5 days]
                                        │
                                        ▼
                                     Phase 7: Plugin Shell
                                        │  [5-7 days]
                                        │
                                        ▼
                                     Phase 8: UI Design
                                        │  [5-10 days]
                                        │
                                        ▼
                                     Phase 9: Presets & Polish
                                        │  [3-5 days]
                                        ▼
                                       DONE
```

### Parallelization Opportunities

| Parallel Track A | Parallel Track B |
|-----------------|-----------------|
| Phase 1: TranceGate | Phase 2: Reverb |
| (2-3 days) | (3-5 days) |
| ↓ | ↓ |
| **Merge point: Phase 3 starts (both complete)** | |

Phase 0 (PolySynthEngine) is already complete, so Phase 1 and Phase 2 can start immediately in parallel. After Phase 3, the remaining phases are mostly sequential because they compose previous work.

### Estimated Total Timeline

| Phase | Duration | Cumulative (Serial) | Cumulative (Parallel) |
|-------|----------|--------------------|-----------------------|
| Phase 0 | ✅ COMPLETE | — | — |
| Phase 1 | 2-3 days | 2-3 days | 3-5 days (P1+P2 parallel) |
| Phase 2 | 3-5 days | 5-8 days | (included above) |
| Phase 3 | 5-7 days | 10-15 days | 8-12 days |
| Phase 4 | 3-4 days | 13-19 days | 11-16 days |
| Phase 5 | 2-3 days | 15-22 days | 13-19 days |
| Phase 6 | 4-5 days | 19-27 days | 17-24 days |
| Phase 7 | 5-7 days | 24-34 days | 22-31 days |
| Phase 8 | 5-10 days | 29-44 days | 27-41 days |
| Phase 9 | 3-5 days | 32-49 days | 30-46 days |

**Estimated total: 30-46 working days (6-9 weeks) with parallelization.** Phase 0 is already complete and not counted.

---

## Risk Analysis

### High Risk

| Risk | Impact | Mitigation |
|------|--------|-----------|
| **RuinaeVoice memory footprint** (all oscillator types pre-allocated) | 16 voices x ~54KB = ~864KB; may cause cache thrashing | Lazy init: only prepare active oscillator type. Measure L1/L2 cache pressure. |
| **SpectralMorphFilter per-voice CPU** | FFT per voice per block is expensive | Smaller FFT (512 vs 1024), or offer SpectralMorph as a "quality mode" that uses fewer voices |
| **Parameter explosion** | ~150+ parameters → complex state save/load, UI sprawl | Group into parameter packs (per-section structs), use macro controls for quick access |

### Medium Risk

| Risk | Impact | Mitigation |
|------|--------|-----------|
| **Trance Gate click-free edge shaping** | Audible clicks on step transitions | One-pole smoother with 1-5ms attack/release (well-understood technique) |
| **Reverb tuning** | Dattorro requires careful coefficient tuning for good sound | Reference implementations available (Freeverb, Sean Costello's work) |
| **Cross-platform build** | New plugin target adds complexity | Follow Iterum/Disrumpo CMake pattern exactly |

### Low Risk

| Risk | Impact | Mitigation |
|------|--------|-----------|
| **Voice management** | Already proven with PolySynthEngine | Reuse exact same pattern |
| **Modulation matrix** | Already exists (ModulationMatrix L3) | Direct composition |
| **Delay effects** | All 7 types implemented and tested | Direct reuse from Iterum |

---

## Summary: What Exists vs What's New

```
EXISTING (reuse directly):          ~75% of DSP functionality
├── 10+ oscillator types
├── 8+ filter types
├── 14+ distortion types
├── 6+ modulation sources
├── ModulationMatrix + ModulationEngine
├── VoiceAllocator + MonoHandler + NoteProcessor
├── ADSR Envelope + MultiStage Envelope
├── LFO (6 waveforms, tempo sync)
├── 7 delay effects
├── SpectralMorphFilter
├── Spectral Freeze
├── StereoField, Oversampler, DiffusionNetwork
└── All Layer 0 utilities (sigmoid, pitch, MIDI, math)

COMPLETE (Phase 0):                 Foundation established
└── PolySynthEngine ✅ (voice pool, allocation, mono/poly, gain comp, soft limiting)

NEW (must build):                   ~25% of DSP functionality
├── TranceGate (spec'd in Ruinae.md)
├── Reverb (Dattorro, composes existing primitives)
├── RuinaeVoice (orchestration of existing components)
├── RuinaeEffectsChain (orchestration of existing effects)
├── RuinaeEngine (top-level composition)
├── VoiceModRouter (lightweight per-voice mod routing)
└── Plugin shell (processor, controller, UI, parameters)
```

The vast majority of "new" code is **orchestration and composition** of existing, tested components -- not new DSP algorithms. The only genuinely new DSP is the **TranceGate** and **Reverb**. With PolySynthEngine already complete, the voice pool management pattern is proven and ready to replicate for RuinaeEngine.
