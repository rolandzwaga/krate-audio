# Disrumpo - Multiband Morphing Distortion Plugin
## Software Requirements Specification

| Field | Value |
|-------|-------|
| Project | Disrumpo |
| Version | 1.1 |
| Date | January 2026 |
| Platforms | Windows, macOS, Linux |
| Format | VST3 |
| Framework | VST3 SDK, VSTGUI |
| Updated | Expanded from 14 to 26 distortion types using DST-ROADMAP and FLT-ROADMAP |

**Related Documents:**
- [plan.md](plan.md) - System architecture and development roadmap
- [tasks.md](tasks.md) - Task breakdown and milestones
- [ui-mockups.md](ui-mockups.md) - Detailed UI specifications
- [dsp-details.md](dsp-details.md) - DSP implementation details and code examples
- [custom-controls.md](custom-controls.md) - Custom VSTGUI control specifications
- [vstgui-implementation.md](vstgui-implementation.md) - Complete VSTGUI implementation specification
- [roadmap.md](roadmap.md) - 16-week development roadmap

> **⚠️ IMPLEMENTATION SPEC DERIVATION RULE**
>
> When creating implementation specs (e.g., `001-plugin-skeleton`, `002-band-management`) from this specification, you **MUST** consult ALL of the following documents:
>
> | Document | Required Content |
> |----------|------------------|
> | **spec.md** (this file) | Functional requirements (FR-xxx), success criteria |
> | **plan.md** | System architecture diagrams, signal flow, layer structure |
> | **tasks.md** | Task breakdown, condensed task IDs (T2.1-T2.4 style) |
> | **roadmap.md** | Detailed task IDs (T2.1-T2.9 style), milestone criteria, dependencies |
> | **dsp-details.md** | Parameter ID encoding, data structures, DSP algorithms |
>
> Failure to consult all documents results in incomplete specs missing architectural context, task mappings, or implementation details.

---

## 1. Executive Summary

Disrumpo is a multiband morphing distortion plugin for synthesizer processing and sound design.

### Key Differentiators
- **26 distortion types** spanning saturation, wavefold, digital, dynamic, hybrid, and experimental categories
- Per-band morphable distortion with multi-point morph spaces (A↔B↔C↔D)
- Sweeping distortion traversing the frequency spectrum
- Morph-frequency linking - distortion character changes with sweep position
- **Advanced morph drivers**: Chaos attractors, envelope following, pitch tracking
- Intelligent per-band oversampling based on active distortion types
- Progressive disclosure UI scaling from simple to expert
- **12 modulation sources** including chaos, sample & hold, pitch follower, and transient detector

### Target Users
- Sound designers
- Electronic music producers
- Synthesists and experimentalists
- Audio post-production professionals

### Success Criteria
| Metric | Target |
|--------|--------|
| CPU usage (4 bands, 4x OS) | < 15% single core @ 44.1kHz/512 samples |
| Latency | < 10ms at highest quality |
| Morph transition | Artifact-free at all speeds |
| UI responsiveness | < 16ms frame time (60fps) |
| Preset recall | < 50ms |

---

## 1a. KrateDSP Implementation Coverage

> **Note**: This section analyzes which Disrumpo requirements are already implemented in the KrateDSP library.

### Distortion Types (FR-DIST-001)

| Disrumpo ID | Type | KrateDSP Component | Status |
|------------|------|-------------------|--------|
| D01 | Soft Clip | `SaturationProcessor::Tape`, `Waveshaper::Tanh` | **EXISTS** |
| D02 | Hard Clip | `SaturationProcessor::Digital`, `Sigmoid::hardClip()` | **EXISTS** |
| D03 | Tube | `TubeStage`, `SaturationProcessor::Tube` | **EXISTS** |
| D04 | Tape | `TapeSaturator` (Simple + Hysteresis models) | **EXISTS** |
| D05 | Fuzz | `FuzzProcessor::Germanium`, `FuzzProcessor::Silicon` | **EXISTS** |
| D06 | Asymmetric Fuzz | `FuzzProcessor` with bias control | **EXISTS** |
| D07 | Sine Fold | `Wavefolder::Sine`, `WavefolderProcessor::Serge` | **EXISTS** |
| D08 | Triangle Fold | `Wavefolder::Triangle`, `WavefolderProcessor::Simple` | **EXISTS** |
| D09 | Serge Fold | `Wavefolder::Lockhart`, `WavefolderProcessor::Buchla259` | **EXISTS** |
| D10 | Full Rectify | `std::abs()` pattern | **COMPOSABLE** |
| D11 | Half Rectify | `std::max(0, x)` pattern | **COMPOSABLE** |
| D12 | Bitcrush | `BitCrusher`, `BitcrusherProcessor` | **EXISTS** |
| D13 | Sample Reduce | `SampleRateReducer` | **EXISTS** |
| D14 | Quantize | `BitCrusher` (built-in quantization) | **EXISTS** |
| D15 | Temporal | `TemporalDistortion` | **EXISTS** |
| D16 | Ring Saturation | `RingSaturation` | **EXISTS** |
| D17 | Feedback | `FeedbackDistortion` | **EXISTS** |
| D18 | Aliasing | `AliasingEffect` | **EXISTS** |
| D19 | Bitwise Mangler | `BitwiseMangler` | **EXISTS** |
| D20 | Chaos | `ChaosWaveshaper` | **EXISTS** |
| D21 | Formant | `FormantDistortion` | **EXISTS** |
| D22 | Granular | `GranularDistortion` | **EXISTS** |
| D23 | Spectral | `SpectralDistortion` | **EXISTS** |
| D24 | Fractal | `FractalDistortion` | **EXISTS** |
| D25 | Stochastic | `StochasticShaper` | **EXISTS** |
| D26 | Allpass Resonant | `AllpassSaturator` | **EXISTS** |

### Supporting Components

| Component | KrateDSP Location | Status |
|-----------|------------------|--------|
| Crossover Filters | `CrossoverLR4` (spec 079) | **EXISTS** |
| Oversampler | `Oversampler` (2x/4x, IIR/FIR) | **EXISTS** |
| LFO | `LFO` (6 waveforms, tempo sync) | **EXISTS** |
| Envelope Follower | `EnvelopeFollower` (3 modes) | **EXISTS** |
| Parameter Smoother | `OnePoleSmoother`, `LinearRamp`, `SlewLimiter` | **EXISTS** |
| DC Blocker | `DCBlocker` | **EXISTS** |
| Pitch Detector | `PitchDetector` (autocorrelation) | **EXISTS** |
| Frequency Shifter | `FrequencyShifter` (Hilbert transform) | **EXISTS** |
| Chaos Attractor | `ChaosWaveshaper` | **EXISTS** |
| Formant Filter | `FormantFilter` | **EXISTS** (spec 078) |
| Granular Engine | `GranularEngine` | **EXISTS** |

### Implementation Summary

- **24 of 26 distortion types** have dedicated implementations
- **2 distortion types** (rectification) are trivially composable
- **All modulation sources** have existing implementations
- **Crossover network** uses existing `CrossoverLR4` from FLT-ROADMAP (spec 079)

---

## 2. Product Vision

### Problem Statement
Existing distortion plugins offer either:
- Single distortion type with parameter control
- Multiband processing with fixed distortion per band
- Limited morphing between two states

No current solution provides fluid morphing across multiple distortion types per band, combined with frequency-sweeping distortion that evolves as it moves through the spectrum.

### Solution
Disrumpo provides a unified environment where:
1. Users split audio into configurable frequency bands (1-8)
2. Each band has a 2D morph space with up to 4 distortion nodes
3. A sweepable distortion focus traverses the spectrum
4. Morph position can be linked to sweep frequency
5. Deep modulation enables animated, evolving textures

---

## 3. Functional Requirements

### 3.1 Band Management

**FR-BAND-001: Configurable Band Count**
- Range: 1 to 8 bands
- Default: 4 bands
- Adding/removing bands redistributes crossover frequencies evenly unless manually configured

**FR-BAND-002: Adjustable Crossover Frequencies**
- Range: 20 Hz to 20,000 Hz
- Constraint: Minimum 0.5 octave between adjacent crossovers
- UI: Draggable dividers on spectrum display

**FR-BAND-003: Crossover Filter Type**
- Linkwitz-Riley 4th order (24 dB/octave)
- Phase-coherent summing, flat frequency response when bands recombine

**FR-BAND-004: Per-Band Controls**
- Gain: -24 dB to +24 dB
- Pan: -100% L to +100% R
- Solo, Bypass, Mute toggles

### 3.2 Distortion Engine

**FR-DIST-001: Supported Distortion Types**

| ID | Type | Category |
|----|------|----------|
| D01 | Soft Clip | Saturation |
| D02 | Hard Clip | Saturation |
| D03 | Tube | Saturation |
| D04 | Tape | Saturation |
| D05 | Fuzz | Saturation |
| D06 | Asymmetric Fuzz | Saturation |
| D07 | Sine Fold | Wavefold |
| D08 | Triangle Fold | Wavefold |
| D09 | Serge Fold | Wavefold |
| D10 | Full Rectify | Rectify |
| D11 | Half Rectify | Rectify |
| D12 | Bitcrush | Digital |
| D13 | Sample Reduce | Digital |
| D14 | Quantize | Digital |
| D15 | Temporal | Dynamic |
| D16 | Ring Saturation | Hybrid |
| D17 | Feedback | Hybrid |
| D18 | Aliasing | Digital |
| D19 | Bitwise Mangler | Digital |
| D20 | Chaos | Experimental |
| D21 | Formant | Experimental |
| D22 | Granular | Experimental |
| D23 | Spectral | Experimental |
| D24 | Fractal | Experimental |
| D25 | Stochastic | Experimental |
| D26 | Allpass Resonant | Hybrid |

**FR-DIST-002: Common Parameters**
- Drive: 0.0 to 10.0
- Mix: 0% to 100%
- Tone: 200 Hz to 8000 Hz

**FR-DIST-003: Type-Specific Parameters**

Saturation: Bias (-1.0 to +1.0), Sag (0.0 to 1.0)
Wavefold: Folds (1-8), Shape (0.0-1.0), Symmetry (0.0-1.0)
Digital: Bit Depth (1.0-16.0), Sample Rate Ratio (1.0-32.0), Smoothness (0.0-1.0)
Dynamic: Sensitivity (0.0-1.0), Attack (1-100ms), Release (10-500ms), Mode (Envelope/Inverse/Derivative)
Hybrid: Feedback (0.0-1.5), Delay (1-100ms), Stages (1-4), Modulation Depth (0.0-1.0)
Experimental: Chaos Amount (0.0-1.0), Attractor Speed (0.1-10.0), Grain Size (5-100ms), Formant Shift (-12 to +12 semitones)

### 3.3 Morph System

**FR-MORPH-001: Multi-Node Morph Space**
- 2 to 4 morph nodes per band
- Each node contains distortion type and parameters
- Default: 2 nodes (A and B)

**FR-MORPH-002: Morph Modes**
| Mode | Description | Control |
|------|-------------|---------|
| 1D Linear | Single axis A↔B↔C↔D | Slider |
| 2D Planar | XY position in node space | XY pad |
| 2D Radial | Angle + distance from center | Knob + slider |

**FR-MORPH-003: Morph Interpolation**
- Inverse distance weighting between nodes
- Artifact-free transitions at all speeds
- Configurable smoothing time: 0ms to 500ms

**FR-MORPH-004: Morph Families**
| Family | Types | Method |
|--------|-------|--------|
| Saturation | D01-D06 | Transfer function interpolation |
| Wavefold | D07-D09 | Parameter interpolation |
| Digital | D12-D14 | Parameter interpolation |
| Rectify | D10-D11 | Parameter interpolation |

Cross-family morphs use parallel processing with output crossfade.

**FR-MORPH-005: Advanced Morph Drivers**
| Driver | Description |
|--------|-------------|
| Manual | Direct user control via XY pad or sliders |
| LFO | Standard oscillator-driven movement |
| Chaos | Lorenz/Rossler attractor creates swirling, organic paths |
| Envelope | Input loudness drives morph position (louder = higher morph) |
| Pitch | Detected pitch maps to morph position (low notes = A, high = D) |
| Transient | Attack detection triggers morph position jumps |

**FR-MORPH-006: Morph Categories**
| Category | Types | Behavior |
|----------|-------|----------|
| Saturation | D01-D06 | Transfer function interpolation |
| Wavefold | D07-D09 | Parameter interpolation |
| Digital | D12-D14, D18-D19 | Parameter interpolation |
| Rectify | D10-D11 | Parameter interpolation |
| Dynamic | D15, D17 | Parameter interpolation with envelope coupling |
| Hybrid | D16, D26 | Parallel blend with output crossfade |
| Experimental | D20-D25 | Parallel blend with output crossfade |

### 3.4 Sweep System

**FR-SWEEP-001: Sweeping Distortion Band**
- Focused distortion intensity moving through frequency spectrum
- Global on/off toggle

**FR-SWEEP-002: Sweep Parameters**
| Parameter | Range |
|-----------|-------|
| Center Frequency | 20 Hz - 20 kHz |
| Width | 0.5 - 4.0 octaves |
| Intensity | 0% - 200% |
| Falloff | Sharp / Smooth |

**FR-SWEEP-003: Sweep-Morph Linking**
- None: Independent control
- Linear: Low freq = morph 0, high freq = morph 1
- Inverse: Low freq = morph 1, high freq = morph 0
- Custom: User-defined curve

**FR-SWEEP-004: Sweep Automation**
- Host automation
- Internal LFO
- Envelope follower
- MIDI CC

### 3.5 Modulation System

**FR-MOD-001: Modulation Sources**
| Source | Count | Parameters |
|--------|-------|------------|
| LFO | 2 | Rate, Shape, Phase, Sync, Retrigger |
| Envelope Follower | 1 | Attack, Release, Sensitivity, Source |
| Random | 1 | Rate, Smoothness, Sync |
| Macro | 4 | Min, Max, Curve |
| Chaos | 1 | Model (Lorenz/Rossler/Chua), Speed, Coupling |
| Sample & Hold | 1 | Rate, Source (LFO/Random/External), Slew |
| Pitch Follower | 1 | Min Hz, Max Hz, Confidence Threshold, Tracking Speed |
| Transient Detector | 1 | Sensitivity, Attack, Decay |

**FR-MOD-002: LFO Specifications**
- Rate (Free): 0.01 Hz - 20 Hz
- Rate (Sync): 8 bars - 1/64T
- Shapes: Sine, Tri, Saw, Square, S&H, Smooth Random
- Phase: 0° - 360°
- Unipolar option

**FR-MOD-003: Modulation Destinations**
Global: Input Gain, Output Gain, Global Mix, Sweep Freq/Width/Intensity
Per-Band: Morph X/Y, Drive, Mix, Band Gain, Band Pan

**FR-MOD-004: Modulation Routing**
- Max 32 routings
- Amount: -100% to +100%
- Curves: Linear, Exponential, S-Curve, Stepped

### 3.6 Oversampling

**FR-OS-001: Global Limit**
- Options: 1x, 2x, 4x, 8x
- Default: 4x

**FR-OS-002: Intelligent Per-Band Selection**
- Automatic based on active distortion types
- Weighted average based on morph weights
- Never exceeds global limit

**FR-OS-003: Recommended Profiles**
| Type | Factor | Rationale |
|------|--------|-----------|
| Soft Clip | 2x | Mild harmonics |
| Hard Clip, Fuzz | 4x | Strong harmonics |
| Wavefolders | 4x | Many harmonics |
| Rectifiers | 4x | Frequency doubling |
| Digital (D12-D14) | 1x | Aliasing intentional |
| Temporal | 2x | Moderate harmonics |
| Ring Saturation | 4x | Inharmonic sidebands |
| Feedback | 2x | Controlled by limiter |
| Aliasing (D18) | 1x | Aliasing is the effect |
| Bitwise | 1x | Artifacts are the effect |
| Chaos | 2x | Moderate, unpredictable |
| Formant | 2x | Resonances focus energy |
| Granular | 2x | Per-grain varies |
| Spectral | 1x | FFT domain processing |
| Fractal | 2x | Varies by iteration depth |
| Stochastic | 2x | Randomized curves |
| Allpass Resonant | 4x | Self-oscillation potential |

### 3.7 Global Controls

- Input Gain: -24 to +24 dB
- Output Gain: -24 to +24 dB
- Global Mix: 0% to 100%
- Metering: Input/Output stereo peak+RMS, per-band indicators

---

## 4. Non-Functional Requirements

### Performance
- CPU: < 15% single-core at 44.1kHz, 512 samples, 4 bands, 4x OS
- Latency: 0 samples (1x) to 256 samples (8x)
- Memory: < 100 MB RAM at max config

### Audio Quality
- Artifact-free morphing (automated detection tests)
- Bit-transparent bypass
- Flat crossover summation (±0.1 dB)

### Compatibility
- VST3: Windows, macOS, Linux
- Tested: Ableton, Bitwig, Cubase, FL Studio, Reaper, Studio One

### Usability
- Progressive disclosure (basic accessible in 30s)
- Preset recall < 50ms
- Undo/redo (50+ levels)

---

## Appendix A: Parameter Ranges

| Parameter | Min | Max | Default |
|-----------|-----|-----|---------|
| Input/Output Gain | -24 dB | +24 dB | 0 dB |
| Global Mix | 0% | 100% | 100% |
| Band Count | 1 | 8 | 4 |
| Crossover Freq | 20 Hz | 20 kHz | varies |
| Band Gain | -24 dB | +24 dB | 0 dB |
| Band Pan | -100% | +100% | 0% |
| Morph X/Y | 0 | 1 | 0 |
| Drive | 0 | 10 | 1 |
| Mix | 0% | 100% | 100% |
| Tone | 200 Hz | 8 kHz | 4 kHz |
| Sweep Freq | 20 Hz | 20 kHz | 1 kHz |
| Sweep Width | 0.5 oct | 4 oct | 1.5 oct |
| Sweep Intensity | 0% | 200% | 50% |
| LFO Rate (Free) | 0.01 Hz | 20 Hz | 1 Hz |
| **Dynamic Parameters** | | | |
| Sensitivity | 0 | 1 | 0.5 |
| Attack Time | 1 ms | 100 ms | 10 ms |
| Release Time | 10 ms | 500 ms | 100 ms |
| **Hybrid Parameters** | | | |
| Modulation Depth | 0 | 1 | 0.5 |
| Stages | 1 | 4 | 1 |
| Feedback | 0 | 1.5 | 0.5 |
| Delay Time | 1 ms | 100 ms | 10 ms |
| **Aliasing Parameters** | | | |
| Downsample Factor | 2 | 32 | 4 |
| Frequency Shift | -1000 Hz | 1000 Hz | 0 Hz |
| **Bitwise Parameters** | | | |
| Rotate Amount | -16 | 16 | 0 |
| XOR Pattern | 0x0000 | 0xFFFF | 0xAAAA |
| **Experimental Parameters** | | | |
| Chaos Amount | 0 | 1 | 0.5 |
| Attractor Speed | 0.1 | 10 | 1 |
| Grain Size | 5 ms | 100 ms | 50 ms |
| Formant Shift | -12 st | +12 st | 0 st |
| **Spectral Parameters** | | | |
| FFT Size | 512 | 4096 | 2048 |
| Magnitude Bits | 1 | 16 | 16 |
| **Fractal Parameters** | | | |
| Iterations | 1 | 8 | 4 |
| Scale Factor | 0.3 | 0.9 | 0.5 |
| Frequency Decay | 0 | 1 | 0.5 |
| **Stochastic Parameters** | | | |
| Jitter Amount | 0 | 1 | 0.2 |
| Jitter Rate | 0.1 Hz | 100 Hz | 10 Hz |
| Coefficient Noise | 0 | 1 | 0.1 |
| **Allpass Resonant Parameters** | | | |
| Resonant Frequency | 20 Hz | 2000 Hz | 440 Hz |
| Allpass Feedback | 0 | 0.99 | 0.7 |
| Decay Time | 0.01 s | 10 s | 1 s |
