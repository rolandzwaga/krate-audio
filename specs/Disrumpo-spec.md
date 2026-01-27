# Disrumpo - Multiband Morphing Distortion Plugin
## Software Requirements Specification & Development Roadmap

| Field | Value |
|-------|-------|
| Project | Disrumpo |
| Version | 1.1 |
| Date | January 2026 |
| Platforms | Windows, macOS, Linux |
| Format | VST3 |
| Framework | VST3 SDK, VSTGUI |
| Updated | Expanded from 14 to 26 distortion types using DST-ROADMAP and FLT-ROADMAP |

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

## 5. System Architecture

### High-Level Component Diagram
```
┌─────────────────────────────────────────────────────────┐
│                    Disrumpo Plugin                        │
├─────────────────────────────────────────────────────────┤
│  UI Layer (VSTGUI)                                      │
│  ├─ Spectrum Display                                    │
│  ├─ Band Controls                                       │
│  ├─ Morph Pads                                          │
│  └─ Modulation Matrix                                   │
├─────────────────────────────────────────────────────────┤
│  Controller Layer                                        │
│  ├─ Preset Manager                                      │
│  ├─ Undo/Redo Stack                                     │
│  └─ Parameter Smoother                                  │
├─────────────────────────────────────────────────────────┤
│  Processor Layer                                         │
│  ├─ Input Stage                                         │
│  ├─ Crossover Network                                   │
│  ├─ Band Processors (1-8)                               │
│  │   ├─ Oversampler                                     │
│  │   └─ Morph Engine                                    │
│  ├─ Sweep Processor                                     │
│  ├─ Modulation Engine                                   │
│  └─ Output Stage                                        │
├─────────────────────────────────────────────────────────┤
│  DSP Building Blocks (existing algorithms)              │
└─────────────────────────────────────────────────────────┘
```

### Signal Flow
```
Input L/R
    │
    ▼
Input Gain
    │
    ▼
Crossover Network ──► Band 1 ──┐
    │                Band 2 ──┤
    │                Band 3 ──┤
    │                Band 4 ──┘
    │                   │
    │                   ▼
    │            Band Summation
    │                   │
    │                   ▼
    │             Sweep Apply
    │                   │
    │                   ▼
    └──────────► Global Mix
                       │
                       ▼
                  Output Gain
                       │
                       ▼
                   Output L/R
```

### Per-Band Processing
```
Band Input
    │
    ▼
Sweep Intensity Multiply
    │
    ▼
Upsample (intelligent factor)
    │
    ▼
┌─────────────────────────────┐
│     Morph Processor         │
│  ┌─────────────────────┐   │
│  │ Compute Weights     │   │
│  │ A:0.4 B:0.35 C:0.25 │   │
│  └─────────────────────┘   │
│         │                   │
│    ┌────┼────┐              │
│    ▼    ▼    ▼              │
│  Dist  Dist  Dist           │
│   A     B     C             │
│    │    │    │              │
│    └────┼────┘              │
│         ▼                   │
│  Weighted Sum (Crossfade)   │
└─────────────────────────────┘
    │
    ▼
Downsample
    │
    ▼
Band Gain/Pan
    │
    ▼
Band Output
```

---

## 6. User Interface Specification

### Window Dimensions
| Config | Width | Height |
|--------|-------|--------|
| Minimum | 800 | 500 |
| Default | 1000 | 600 |
| Maximum | 1400 | 900 |

### Progressive Disclosure Levels
| Level | Name | Content |
|-------|------|---------|
| 1 | Essential | Spectrum, band types, basic controls |
| 2 | Standard | Morph controls, sweep, per-band detail |
| 3 | Expert | Modulation matrix, advanced params |

### Level 1: Essential View
```
┌────────────────────────────────────────────────────────────┐
│  Disrumpo                               [≡ Menu] [? Help]   │
├────────────────────────────────────────────────────────────┤
│  ┌──────────────────────────────────────────────────────┐ │
│  │              SPECTRUM DISPLAY                         │ │
│  │  ▓▓▓▓▓▓▓▓▓│▓▓▓▓▓▓▓▓▓▓▓▓│▓▓▓▓▓▓▓▓▓│▓▓▓▓▓▓▓▓▓▓▓▓▓▓   │ │
│  │   20Hz  200Hz      2kHz       8kHz        20kHz      │ │
│  │        [Band1]   [Band2]   [Band3]     [Band4]       │ │
│  └──────────────────────────────────────────────────────┘ │
│                                                            │
│  ┌─────────────┬─────────────┬─────────────┬─────────────┐│
│  │   BAND 1    │   BAND 2    │   BAND 3    │   BAND 4    ││
│  │  < 200 Hz   │ 200 - 2kHz  │  2k - 8kHz  │   > 8kHz    ││
│  ├─────────────┼─────────────┼─────────────┼─────────────┤│
│  │Type [Tube▼] │Type [Fuzz▼] │Type [Fold▼] │Type[Crush▼] ││
│  │Drive (◯)    │Drive (●)    │Drive (◯)    │Drive (◯)    ││
│  │Mix   (◯)    │Mix   (◯)    │Mix   (●)    │Mix   (◯)    ││
│  │[S] [B] [M]  │[S] [B] [M]  │[S] [B] [M]  │[S] [B] [M]  ││
│  └─────────────┴─────────────┴─────────────┴─────────────┘│
│                                                            │
│  ┌─────────────────────────┬──────────────────────────────┐│
│  │        GLOBAL           │           SWEEP              ││
│  │ Input(◯) Output(◯) Mix(●)│ [●]Enable Freq(◯) Width(◯) ││
│  └─────────────────────────┴──────────────────────────────┘│
│                                                            │
│  Preset: [Init            ▼]  [<] [>] [Save]  [▼ Expand]  │
└────────────────────────────────────────────────────────────┘
```

### Level 2: Standard View (Band Expanded)
```
┌─ BAND 2: 200Hz - 2kHz ───────────────────────────────────┐
│                                                           │
│  ┌─ MORPH SPACE ─────────────┐  ┌─ NODES ───────────────┐│
│  │       ● Fuzz              │  │ [+] Add Node          ││
│  │        ╲                  │  │                       ││
│  │         ╲                 │  │ ● Fuzz (A)            ││
│  │   ● ─────●                │  │   Type [Fuzz      ▼]  ││
│  │  Tube   Fold              │  │   Drive(●) Bias(◯)    ││
│  │         ○ ← cursor        │  │                       ││
│  │                           │  │ ● Tube (B)            ││
│  │  Mode: [2D Planar ▼]      │  │   Type [Tube      ▼]  ││
│  └───────────────────────────┘  │   Drive(◯) Bias(◯)    ││
│                                 │                       ││
│  X: [═══════●════] 0.72        │ ● Fold (C)            ││
│     Link: [Sweep Freq    ▼]    │   Type [Sine Fold ▼]  ││
│                                 │   Drive(◯) Folds(●)   ││
│  Y: [═══●════════] 0.31        └───────────────────────┘│
│     Link: [None          ▼]                              │
│                                                           │
│  OUTPUT: Gain(◯) Pan(◯) [S][B][M]                        │
└───────────────────────────────────────────────────────────┘
```

### Level 3: Modulation Panel
```
┌── MODULATION ────────────────────────────────────────────┐
│  ┌─ SOURCES ─────────────┐  ┌─ ROUTING MATRIX ─────────┐│
│  │ LFO 1                 │  │ Source    Dest      Amt  ││
│  │ ├─ Rate  (●) 0.5 Hz   │  │ LFO 1  → Sweep Freq [+70%]│
│  │ ├─ Shape [Sine    ▼]  │  │ LFO 2  → B2 Morph X [-30%]│
│  │ └─ Phase (◯) 0°       │  │ Env    → Glbl Drive [+50%]│
│  │                       │  │ Macro1 → B1 Morph Y [+80%]│
│  │ LFO 2                 │  │                          ││
│  │ ├─ Rate  (◯) 1/4      │  │ [+ Add Routing]          ││
│  │ ├─ Shape [Triangle▼]  │  └──────────────────────────┘│
│  │ └─ Sync  [Tempo   ▼]  │                              │
│  │                       │  ┌─ MACROS ─────────────────┐│
│  │ Envelope Follower     │  │ Macro1(◯) Macro2(◯)      ││
│  │ ├─ Attack  (◯) 10ms   │  │ Macro3(◯) Macro4(◯)      ││
│  │ └─ Release (●) 150ms  │  └──────────────────────────┘│
│  └───────────────────────┘                              │
└──────────────────────────────────────────────────────────┘
```

### Color Scheme
| Element | Hex |
|---------|-----|
| Background Primary | #1A1A1E |
| Background Secondary | #252529 |
| Accent Primary | #FF6B35 |
| Accent Secondary | #4ECDC4 |
| Text Primary | #FFFFFF |
| Text Secondary | #8888AA |
| Band 1-4 | #FF6B35, #4ECDC4, #95E86B, #C792EA |
| Band 5-8 | #FFCB6B, #FF5370, #89DDFF, #F78C6C |

### Interaction Specs
- **Knobs**: Vertical drag, Shift+drag for fine, double-click reset, Ctrl+click direct entry
- **Spectrum**: Drag dividers for crossovers, click band to select, scroll to zoom
- **Morph Pad**: Drag cursor, Alt+drag node to reposition, Shift for fine

---

## 7. Data Structures

### Parameter ID Encoding

Following the Iterum plugin pattern from `plugins/iterum/src/plugin_ids.h`:

```cpp
// Disrumpo/src/plugin_ids.h
#pragma once

#include "pluginterfaces/vst/vsttypes.h"

namespace Disrumpo {

// Parameter IDs follow kNameId convention per CLAUDE.md
// Encoding: band index (0-7) in bits 8-11, node index (0-3) in bits 12-15

enum ParameterID : Steinberg::Vst::ParamID {
    // Global Parameters
    kInputGainId        = 0x0F00,
    kOutputGainId       = 0x0F01,
    kGlobalMixId        = 0x0F02,
    kBandCountId        = 0x0F03,
    kOversampleMaxId    = 0x0F04,

    // Sweep Parameters (band = 0xE)
    kSweepEnableId      = 0x0E00,
    kSweepFrequencyId   = 0x0E01,
    kSweepWidthId       = 0x0E02,
    kSweepIntensityId   = 0x0E03,
    kSweepMorphLinkId   = 0x0E04,

    // Per-band parameters use helper: makeBandParamId(bandIndex, paramType)
    // Per-node parameters use helper: makeNodeParamId(bandIndex, nodeIndex, paramType)
};

// Band-level parameter types (node = 0xF)
enum BandParamType : uint8_t {
    kBandGain       = 0x00,
    kBandPan        = 0x01,
    kBandSolo       = 0x02,
    kBandBypass     = 0x03,
    kBandMute       = 0x04,
    kBandMorphX     = 0x08,
    kBandMorphY     = 0x09,
    kBandMorphMode  = 0x0A,
};

// Node-level parameter types
enum NodeParamType : uint8_t {
    kNodeType       = 0x00,
    kNodeDrive      = 0x01,
    kNodeMix        = 0x02,
    kNodeTone       = 0x03,
    kNodeBias       = 0x04,
    kNodeFolds      = 0x05,
    kNodeBitDepth   = 0x06,
};

constexpr Steinberg::Vst::ParamID makeBandParamId(uint8_t band, BandParamType param) {
    return static_cast<Steinberg::Vst::ParamID>((0xF << 12) | (band << 8) | param);
}

constexpr Steinberg::Vst::ParamID makeNodeParamId(uint8_t band, uint8_t node, NodeParamType param) {
    return static_cast<Steinberg::Vst::ParamID>((node << 12) | (band << 8) | param);
}

} // namespace Disrumpo
```

### Core Structures

Following KrateDSP conventions (`Krate::DSP` namespace, `kConstantName` constants):

```cpp
// dsp/include/krate/dsp/systems/Disrumpo/distortion_types.h
#pragma once

#include <cstdint>

namespace Krate::DSP {

/// @brief Distortion algorithm types for Disrumpo morph engine.
/// Maps to existing KrateDSP processors where available.
enum class DistortionType : uint8_t {
    // Saturation (use SaturationProcessor, TubeStage, TapeSaturator, FuzzProcessor)
    SoftClip = 0,       ///< Waveshaper::Tanh
    HardClip,           ///< Sigmoid::hardClip
    Tube,               ///< TubeStage processor
    Tape,               ///< TapeSaturator processor
    Fuzz,               ///< FuzzProcessor::Germanium
    AsymmetricFuzz,     ///< FuzzProcessor with bias

    // Wavefold (use WavefolderProcessor)
    SineFold,           ///< WavefolderProcessor::Serge
    TriangleFold,       ///< WavefolderProcessor::Simple
    SergeFold,          ///< WavefolderProcessor::Lockhart

    // Rectify (composable from core math)
    FullRectify,        ///< std::abs(x)
    HalfRectify,        ///< std::max(0.0f, x)

    // Digital (use BitcrusherProcessor, SampleRateReducer, AliasingEffect, BitwiseMangler)
    Bitcrush,           ///< BitcrusherProcessor
    SampleReduce,       ///< SampleRateReducer
    Quantize,           ///< BitCrusher quantization
    Aliasing,           ///< AliasingEffect (intentional aliasing without AA)
    BitwiseMangler,     ///< BitwiseMangler (XOR, bit rotation, overflow wrap)

    // Dynamic (envelope-reactive distortion)
    Temporal,           ///< TemporalDistortion (envelope-following drive)

    // Hybrid (complex routing)
    RingSaturation,     ///< RingSaturation (signal × saturate(signal))
    FeedbackDist,       ///< FeedbackDistortion (delay + saturation + feedback)

    // Experimental (sound design)
    Chaos,              ///< ChaosWaveshaper (Lorenz/Rossler/Chua attractors)
    Formant,            ///< FormantDistortion (vowel shaping + saturation)
    Granular,           ///< GranularDistortion (per-grain variable distortion)
    Spectral,           ///< SpectralDistortion (per-bin FFT domain saturation)
    Fractal,            ///< FractalDistortion (recursive multi-scale distortion)
    Stochastic,         ///< StochasticShaper (randomized transfer function)

    // Hybrid (complex routing) - additional
    AllpassResonant,    ///< AllpassSaturator (resonant distortion networks)
};

/// @brief Morph interpolation mode.
enum class MorphMode : uint8_t {
    Linear1D = 0,   ///< Single axis A↔B↔C↔D
    Planar2D,       ///< XY position in node space
    Radial2D,       ///< Angle + distance from center
};

/// @brief Sweep-to-morph linking mode.
enum class SweepMorphLink : uint8_t {
    None = 0,       ///< Independent control
    Linear,         ///< Low freq = morph 0, high freq = morph 1
    Inverse,        ///< Low freq = morph 1, high freq = morph 0
    Custom,         ///< User-defined curve
};

// Constants following kPascalCase convention
inline constexpr float kMinDrive = 0.0f;
inline constexpr float kMaxDrive = 10.0f;
inline constexpr float kDefaultDrive = 1.0f;

inline constexpr float kMinToneHz = 200.0f;
inline constexpr float kMaxToneHz = 8000.0f;
inline constexpr float kDefaultToneHz = 4000.0f;

inline constexpr float kMinBandGainDb = -24.0f;
inline constexpr float kMaxBandGainDb = +24.0f;

inline constexpr int kMinBands = 1;
inline constexpr int kMaxBands = 8;
inline constexpr int kDefaultBands = 4;

inline constexpr float kMinCrossoverHz = 20.0f;
inline constexpr float kMaxCrossoverHz = 20000.0f;
inline constexpr float kMinCrossoverSpacingOctaves = 0.5f;

} // namespace Krate::DSP
```

```cpp
// dsp/include/krate/dsp/systems/Disrumpo/morph_node.h
#pragma once

#include "distortion_types.h"

namespace Krate::DSP {

/// @brief Parameters for a single distortion algorithm.
/// Not all fields apply to all types - use the appropriate subset.
struct DistortionParams {
    // Common parameters
    float drive = kDefaultDrive;    ///< Drive amount [0, 10]
    float mix = 1.0f;               ///< Wet/dry mix [0, 1]
    float tone = kDefaultToneHz;    ///< Tone filter frequency [200, 8000] Hz

    // Saturation-specific
    float bias = 0.0f;              ///< Asymmetry bias [-1, 1]
    float sag = 0.0f;               ///< Sag/compression [0, 1]

    // Wavefold-specific
    float folds = 1.0f;             ///< Number of folds [1, 8]
    float shape = 0.0f;             ///< Fold shape [0, 1]
    float symmetry = 0.5f;          ///< Fold symmetry [0, 1]

    // Digital-specific
    float bitDepth = 16.0f;         ///< Bit depth [1, 16]
    float srRatio = 1.0f;           ///< Sample rate reduction [1, 32]
    float smoothness = 0.0f;        ///< Anti-aliasing smoothness [0, 1]

    // Dynamic-specific (Temporal)
    float sensitivity = 0.5f;       ///< Envelope sensitivity [0, 1]
    float attackMs = 10.0f;         ///< Envelope attack [1, 100] ms
    float releaseMs = 100.0f;       ///< Envelope release [10, 500] ms

    // Hybrid-specific (RingSaturation, FeedbackDist)
    float modDepth = 0.5f;          ///< Modulation depth [0, 1]
    float stages = 1.0f;            ///< Processing stages [1, 4]
    float feedback = 0.5f;          ///< Feedback amount [0, 1.5]
    float delayMs = 10.0f;          ///< Delay time [1, 100] ms

    // Aliasing-specific
    float freqShift = 0.0f;         ///< Frequency shift before downsample [-1000, 1000] Hz

    // Bitwise-specific
    float rotateAmount = 0.0f;      ///< Bit rotation amount [-16, 16]

    // Experimental-specific (Chaos, Formant, Granular)
    float chaosAmount = 0.5f;       ///< Chaos blend [0, 1]
    float attractorSpeed = 1.0f;    ///< Attractor evolution speed [0.1, 10]
    float grainSizeMs = 50.0f;      ///< Grain size [5, 100] ms
    float formantShift = 0.0f;      ///< Formant shift [-12, +12] semitones
};

/// @brief A morph node containing distortion type and position.
struct MorphNode {
    int id = 0;                     ///< Unique node identifier (0-3)
    DistortionType type = DistortionType::SoftClip;
    DistortionParams params;
    float posX = 0.0f;              ///< X position in morph space [0, 1]
    float posY = 0.0f;              ///< Y position in morph space [0, 1]
};

/// @brief State for a single frequency band.
struct BandState {
    float lowFreqHz = kMinCrossoverHz;
    float highFreqHz = kMaxCrossoverHz;

    std::array<MorphNode, 4> nodes; ///< Up to 4 morph nodes (fixed-size for RT safety)
    int activeNodeCount = 2;        ///< How many nodes are active (2-4)

    MorphMode morphMode = MorphMode::Linear1D;
    float morphX = 0.0f;            ///< Current morph X position [0, 1]
    float morphY = 0.0f;            ///< Current morph Y position [0, 1]

    float gainDb = 0.0f;            ///< Band output gain [-24, +24] dB
    float pan = 0.0f;               ///< Band pan [-1, +1]

    bool solo = false;
    bool bypass = false;
    bool mute = false;
};

} // namespace Krate::DSP
```

```cpp
// dsp/include/krate/dsp/systems/Disrumpo/modulation_types.h
#pragma once

#include <cstdint>

namespace Krate::DSP {

/// @brief Modulation source type.
enum class ModSource : uint8_t {
    None = 0,
    LFO1,
    LFO2,
    EnvFollower,
    Random,
    Macro1,
    Macro2,
    Macro3,
    Macro4,
    Chaos,          ///< Lorenz/Rossler/Chua attractor output
    SampleHold,     ///< Sample & hold (stepped modulation)
    PitchFollower,  ///< Detected input pitch
    Transient,      ///< Transient detector output
};

/// @brief Modulation curve shape.
enum class ModCurve : uint8_t {
    Linear = 0,
    Exponential,
    SCurve,
    Stepped,
};

/// @brief A single modulation routing.
struct ModRouting {
    ModSource source = ModSource::None;
    uint32_t destParamId = 0;       ///< Target parameter ID
    float amount = 0.0f;            ///< Modulation depth [-1, +1]
    ModCurve curve = ModCurve::Linear;
};

inline constexpr int kMaxModRoutings = 32;

} // namespace Krate::DSP
```

---

## 8. Preset System

### Standard VST3 Preset Format

Disrumpo uses the **standard VST3 preset format** (`.vstpreset` files). This ensures:
- Full host integration (preset browsers, save/load dialogs)
- Cross-DAW compatibility
- No custom file I/O code required

### Implementation

The Processor implements `getState()`/`setState()` to serialize all parameters:

```cpp
// processor.cpp
tresult PLUGIN_API Processor::getState(IBStream* state) {
    IBStreamer streamer(state, kLittleEndian);

    // Version for future compatibility
    streamer.writeInt32(kPresetVersion);

    // Global parameters
    streamer.writeFloat(inputGain_);
    streamer.writeFloat(outputGain_);
    streamer.writeFloat(globalMix_);
    streamer.writeInt32(bandCount_);
    streamer.writeInt32(maxOversample_);

    // Sweep parameters
    streamer.writeBool(sweepEnabled_);
    streamer.writeFloat(sweepFrequency_);
    streamer.writeFloat(sweepWidth_);
    streamer.writeFloat(sweepIntensity_);
    streamer.writeInt32(static_cast<int32>(sweepMorphLink_));

    // Per-band state (fixed 8 bands for format stability)
    for (int b = 0; b < kMaxBands; ++b) {
        const auto& band = bands_[b];
        streamer.writeFloat(band.lowFreqHz);
        streamer.writeFloat(band.highFreqHz);
        streamer.writeInt32(static_cast<int32>(band.morphMode));
        streamer.writeFloat(band.morphX);
        streamer.writeFloat(band.morphY);
        streamer.writeInt32(band.activeNodeCount);
        streamer.writeFloat(band.gainDb);
        streamer.writeFloat(band.pan);
        streamer.writeBool(band.solo);
        streamer.writeBool(band.bypass);
        streamer.writeBool(band.mute);

        // Per-node state (fixed 4 nodes)
        for (int n = 0; n < 4; ++n) {
            const auto& node = band.nodes[n];
            streamer.writeInt32(static_cast<int32>(node.type));
            streamer.writeFloat(node.params.drive);
            streamer.writeFloat(node.params.mix);
            streamer.writeFloat(node.params.tone);
            streamer.writeFloat(node.params.bias);
            streamer.writeFloat(node.params.folds);
            streamer.writeFloat(node.params.bitDepth);
            streamer.writeFloat(node.posX);
            streamer.writeFloat(node.posY);
        }
    }

    // Modulation routings
    streamer.writeInt32(activeRoutingCount_);
    for (int r = 0; r < kMaxModRoutings; ++r) {
        const auto& routing = modRoutings_[r];
        streamer.writeInt32(static_cast<int32>(routing.source));
        streamer.writeInt32(routing.destParamId);
        streamer.writeFloat(routing.amount);
        streamer.writeInt32(static_cast<int32>(routing.curve));
    }

    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state) {
    IBStreamer streamer(state, kLittleEndian);

    int32 version;
    if (!streamer.readInt32(version)) return kResultFalse;
    if (version > kPresetVersion) return kResultFalse;  // Future version

    // Read all parameters (mirror of getState)...
    return kResultOk;
}
```

### Factory Presets

Factory presets are `.vstpreset` files installed to the standard location:
- **Windows**: `C:\Program Files\Common Files\VST3\Presets\Krate Audio\Disrumpo\`
- **macOS**: `/Library/Audio/Presets/Krate Audio/Disrumpo/`
- **Linux**: `~/.vst3/presets/Krate Audio/Disrumpo/`

| Category | Count |
|----------|-------|
| Init | 5 |
| Sweep | 15 |
| Morph | 15 |
| Bass | 10 |
| Leads | 10 |
| Pads | 10 |
| Drums | 10 |
| Experimental | 15 |
| Chaos | 10 |
| Dynamic | 10 |
| Lo-Fi | 10 |
| **Total** | **120** |

### Preset Versioning

The preset format includes a version number to handle future additions:
- Version 1: Initial release
- Future versions add parameters at the end
- `setState()` gracefully handles older versions by using defaults for missing data

---

## 9. Development Roadmap

### Phase Overview
```
Phase 1: Foundation     ████████░░░░░░░░  Weeks 1-4
Phase 2: Core Features  ░░░░░░░░████████  Weeks 5-10
Phase 3: Advanced       ░░░░░░░░░░░░████  Weeks 11-14
Phase 4: Polish         ░░░░░░░░░░░░░░██  Weeks 15-16
```

### Phase 1: Foundation (Weeks 1-4)

**Week 1: Project Setup**
- Project scaffolding (VST3 SDK, VSTGUI, CMake)
- Parameter system with ID encoding
- Basic processor/controller skeletons
- State serialization framework

**Week 2: Band Management**
- CrossoverNetwork (Linkwitz-Riley 4th order)
- Dynamic band count support
- Per-band routing and summing
- Solo/bypass/mute logic

**Week 3: Distortion Integration**
- Distortion algorithm adapter layer
- Per-band single distortion type
- Common parameter mapping
- Oversampler integration

**Week 4: Basic UI**
- VSTGUI integration
- Custom knob control
- Spectrum display (placeholder)
- Band strip component
- Global controls

### Phase 2: Core Features (Weeks 5-10)

**Week 5-6: Morph System**
- MorphNode data structure
- MorphEngine (1D then 2D)
- Weight computation (inverse distance)
- Parameter interpolation
- Artifact-free smoothing

**Week 7: Morph UI**
- XY morph pad control
- Node visualization/dragging
- Node editor panel
- Mode selector

**Week 8: Sweep System**
- SweepProcessor class
- Gaussian intensity distribution
- Per-band intensity modulation
- Sweep-morph linking
- UI controls

**Week 9-10: Modulation**
- LFO implementation (2x)
- Envelope follower
- Random source
- Macro parameters
- Routing matrix
- UI panel

### Phase 3: Advanced (Weeks 11-14)

**Week 11: Intelligent Oversampling**
- Per-type profiles
- Weighted calculation
- Dynamic switching
- CPU optimization

**Week 12: Preset System**
- JSON serialization
- Browser UI
- Save dialog
- Factory presets
- Tagging/search

**Week 13: Undo/Redo & Polish**
- Undo stack
- Edit grouping
- Real spectrum analyzer
- Metering

**Week 14: Progressive Disclosure**
- Expand/collapse panels
- Level transitions
- Keyboard shortcuts
- Tooltips
- Window resize

### Phase 4: Polish (Weeks 15-16)

**Week 15: Testing**
- Cross-platform testing
- DAW compatibility
- Performance profiling
- Bug fixing

**Week 16: Release**
- User manual
- Factory presets (90)
- Installers
- Code signing

### Milestones
| Milestone | Week | Deliverable |
|-----------|------|-------------|
| M1 | 1 | Plugin loads in DAW |
| M2 | 3 | Working multiband distortion |
| M3 | 4 | Level 1 UI functional |
| M4 | 6 | 2D morph system |
| M5 | 8 | Sweep with morph link |
| M6 | 10 | Full modulation |
| M7 | 12 | Preset system |
| M8 | 14 | Complete UI |
| M9 | 15 | Beta |
| M10 | 16 | Release 1.0 |

---

## 10. Testing Strategy

### Unit Tests
- Morph weight computation
- Crossover frequency response
- Modulation routing

### Integration Tests
| ID | Test |
|----|------|
| IT-001 | Full signal path |
| IT-002 | Preset recall |
| IT-003 | Automation |
| IT-004 | Band count change |
| IT-005 | Morph automation |
| IT-006 | Sweep + morph link |

### Performance Targets
| Config | CPU Target |
|--------|------------|
| 1 band, 1x OS | < 2% |
| 4 bands, 4x OS | < 15% |
| 8 bands, 8x OS | < 40% |

### DAW Compatibility
Ableton Live, Bitwig, Cubase, FL Studio, Reaper, Studio One, Logic Pro

---

## 11. Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Morph artifacts | Medium | High | Extensive smoothing, automated testing |
| CPU overload | Medium | High | Profile early, optimize hot paths |
| UI complexity | High | Medium | Strict progressive disclosure |
| Cross-platform | Low | Medium | CI/CD multi-platform builds |

---

## Appendix: New Distortion Types (from DST-ROADMAP)

This section documents the additional distortion types added from the DST-ROADMAP, extending the original 14 types to 22.

### D15: Temporal Distortion
**Category:** Dynamic | **Status:** Implemented (spec 108)

Envelope-following drive modulation. The distortion intensity varies based on the input signal's amplitude.

| Mode | Behavior |
|------|----------|
| EnvelopeFollow | Louder = more distortion |
| InverseEnvelope | Quiet = more distortion (expansion) |
| Derivative | Transients get different curve |
| Hysteresis | Deep memory of recent samples |

**Use Cases:** Expressive dynamics, breathing textures, transient shaping

### D16: Ring Saturation
**Category:** Hybrid | **Status:** Implemented (spec 109)

Self-modulation: `output = input * saturate(input * drive)`. Creates inharmonic sidebands like ring mod, but signal-coherent.

**Character:** Metallic, bell-like, aggressive at high settings

### D17: Feedback Distortion
**Category:** Hybrid | **Status:** Implemented (spec 110)

Delay + saturator + feedback loop with limiting. Creates sustained, singing distortion that can approach self-oscillation.

**Use Cases:** Screaming leads, feedback effects, infinite sustain

### D18: Aliasing Effect
**Category:** Digital | **Status:** Implemented (spec 112)

Intentional aliasing via downsampling without anti-aliasing filter. Optional frequency shifting before downsample creates unique spectral folding patterns.

**Character:** Digital grunge, lo-fi, metallic artifacts

### D19: Bitwise Mangler
**Category:** Digital | **Status:** Implemented (spec 111)

Operations on the bit representation of samples: XOR, bit rotation, bit shuffle, overflow wrap.

| Operation | Character |
|-----------|-----------|
| XorPattern | Harmonically complex noise |
| XorPrevious | Sample-correlated artifacts |
| BitRotate | Extreme tonal shifts |
| OverflowWrap | Hard digital clipping with wraparound |

### D20: Chaos Distortion
**Category:** Experimental | **Status:** Implemented

Chaotic attractor waveshaping using Lorenz, Rossler, Chua, or Henon systems. The transfer function evolves over time, creating living, breathing distortion.

**Character:** Organic, unpredictable, evolving textures

### D21: Formant Distortion
**Category:** Experimental | **Status:** Implemented

Distortion through vocal-tract-like resonances. Combines FormantFilter with waveshaping for "talking distortion" effects.

**Character:** Vowel-shaped, vocal, alien textures

### D22: Granular Distortion
**Category:** Experimental | **Status:** Implemented

Per-grain variable distortion. Each micro-grain (5-100ms) gets different drive, algorithm, or parameters, creating evolving textured destruction.

**Character:** Glitchy, textured, pointillist

### D23: Spectral Distortion
**Category:** Experimental | **Status:** Implemented

Apply distortion algorithms per-frequency-bin in the spectral domain via FFT. Can saturate magnitudes while preserving phase perfectly—impossible in time domain.

| Mode | Behavior |
|------|----------|
| PerBinSaturate | Apply saturation to each bin's magnitude |
| MagnitudeOnly | Saturate magnitudes, preserve phase exactly |
| BinSelective | Different distortion per frequency range |
| SpectralBitcrush | Quantize magnitudes per bin |

**Character:** Impossible frequency-selective distortion, phase-coherent saturation

### D24: Fractal Distortion
**Category:** Experimental | **Status:** Implemented

Recursive multi-scale distortion creating harmonic structure that reveals new detail at every "zoom level."

| Mode | Behavior |
|------|----------|
| Residual | Distort progressively smaller residuals |
| Multiband | Split into octave bands, recurse each with depth scaling |
| Harmonic | Separate odd/even harmonics, different curves per level |
| Cascade | Different waveshaper at each iteration level |
| Feedback | Cross-feed between iteration levels (chaotic) |

**Character:** Self-similar harmonic stacking, "zoom into detail" effect

### D25: Stochastic Distortion
**Category:** Experimental | **Status:** Implemented

Randomized transfer function simulating analog component tolerance variation. Each sample gets slightly different curve.

**Use Cases:** Analog warmth, component drift simulation, organic variation

### D26: Allpass Resonant Distortion
**Category:** Hybrid | **Status:** Implemented

Place saturation inside allpass filter feedback loops for resonant, pitched distortion.

| Topology | Behavior |
|----------|----------|
| SingleAllpass | One allpass with saturation in feedback |
| AllpassChain | Series of allpass filters with saturation |
| KarplusStrong | Delay + saturator + feedback (plucked string) |
| FeedbackMatrix | 4x4 matrix of cross-fed saturators |

**Character:** Pitched/resonant distortion that can self-oscillate

### Oversampling Recommendations (Extended)

| Type | Factor | Rationale |
|------|--------|-----------|
| Soft Clip | 2x | Mild harmonics |
| Hard Clip, Fuzz | 4x | Strong harmonics |
| Wavefolders | 4x | Many harmonics from folding |
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
| Spectral | 1x | FFT domain, no aliasing |
| Fractal | 2x | Varies by iteration depth |
| Stochastic | 2x | Randomized curves |
| Allpass Resonant | 4x | Self-oscillation potential |

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

---

## Appendix B: File Structure

Following the Krate Audio monorepo pattern:

```
krate-audio/
├── dsp/                                    # Shared KrateDSP library (PLUGIN-AGNOSTIC)
│   ├── include/krate/dsp/
│   │   ├── systems/                       # Layer 3: Generic reusable systems
│   │   │   ├── crossover_network.h        # Linkwitz-Riley multiband crossover (NEW)
│   │   │   └── ... (existing systems)
│   │   ├── processors/                    # Layer 2: Existing processors (used as-is)
│   │   │   ├── saturation_processor.h     # → D01-D02
│   │   │   ├── tube_stage.h               # → D03
│   │   │   ├── tape_saturator.h           # → D04
│   │   │   ├── fuzz_processor.h           # → D05-D06
│   │   │   ├── wavefolder_processor.h     # → D07-D09
│   │   │   ├── bitcrusher_processor.h     # → D12-D14
│   │   │   └── envelope_follower.h        # Modulation source
│   │   └── primitives/                    # Layer 1: Existing primitives (used as-is)
│   │       ├── biquad.h                   # Filter building blocks
│   │       ├── oversampler.h              # Anti-aliasing
│   │       ├── lfo.h                      # Modulation source
│   │       ├── smoother.h                 # Parameter smoothing
│   │       └── sample_rate_reducer.h      # → D13
│   └── tests/
│       └── unit/systems/
│           └── crossover_network_test.cpp # Tests for new generic component
│
├── plugins/
│   └── Disrumpo/                           # Disrumpo VST3 plugin
│       ├── CMakeLists.txt
│       ├── src/
│       │   ├── entry.cpp                  # VST3 entry point
│       │   ├── plugin_ids.h               # Parameter ID definitions
│       │   ├── version.h                  # Plugin version info
│       │   ├── dsp/                       # PLUGIN-SPECIFIC DSP composition
│       │   │   ├── distortion_types.h     # Disrumpo distortion enum
│       │   │   ├── distortion_adapter.h   # Unified interface to KrateDSP processors
│       │   │   ├── morph_engine.h         # Weight computation, interpolation
│       │   │   ├── band_processor.h       # Per-band morph + distortion
│       │   │   ├── sweep_processor.h      # Frequency sweep with intensity
│       │   │   └── modulation_engine.h    # LFO/Env routing
│       │   ├── processor/
│       │   │   ├── processor.h
│       │   │   └── processor.cpp          # Audio processing (composes plugin DSP)
│       │   └── controller/
│       │       ├── controller.h
│       │       └── controller.cpp         # UI & parameter management
│       ├── tests/
│       │   ├── unit/                      # Plugin-specific unit tests
│       │   │   ├── distortion_adapter_test.cpp
│       │   │   ├── morph_engine_test.cpp
│       │   │   └── band_processor_test.cpp
│       │   ├── integration/               # Full signal path tests
│       │   └── approval/                  # Regression tests
│       └── resources/
│           ├── editor.uidesc              # VSTGUI UI definition
│           ├── presets/                   # Factory presets (.vstpreset)
│           └── installers/                # Platform installers
│
└── specs/
    └── Disrumpo/                           # Feature specifications
        ├── spec.md                        # Requirements
        ├── plan.md                        # Implementation plan
        └── tasks.md                       # Task breakdown
```

### Key Architecture Notes

1. **DSP library is plugin-agnostic**: KrateDSP contains only generic, reusable building blocks. No plugin-specific code lives there.

2. **Plugin-specific DSP in plugin folder**: Disrumpo-specific composition (morph engine, distortion adapter, band processor) lives in `plugins/Disrumpo/src/dsp/`. This follows the same pattern as Iterum.

3. **Compose existing processors**: The `DistortionAdapter` in the plugin composes existing KrateDSP processors (`SaturationProcessor`, `TubeStage`, `FuzzProcessor`, `WavefolderProcessor`, `BitcrusherProcessor`).

4. **Generic components may be promoted**: If `CrossoverNetwork` proves useful to multiple plugins, it can be added to `dsp/include/krate/dsp/systems/`. Start in the plugin, promote when reuse is proven.

5. **Test hierarchy**:
   - `dsp/tests/`: Tests for generic KrateDSP components
   - `plugins/Disrumpo/tests/`: Tests for Disrumpo-specific composition

---

## Appendix C: Composing Existing KrateDSP Processors

Example code showing how to use existing KrateDSP components for Disrumpo:

### Distortion Adapter Pattern

```cpp
// plugins/Disrumpo/src/dsp/distortion_adapter.h
#pragma once

#include <krate/dsp/processors/saturation_processor.h>
#include <krate/dsp/processors/tube_stage.h>
#include <krate/dsp/processors/tape_saturator.h>
#include <krate/dsp/processors/fuzz_processor.h>
#include <krate/dsp/processors/wavefolder_processor.h>
#include <krate/dsp/processors/bitcrusher_processor.h>
#include <krate/dsp/processors/temporal_distortion.h>
#include <krate/dsp/processors/feedback_distortion.h>
#include <krate/dsp/processors/aliasing_effect.h>
#include <krate/dsp/processors/spectral_distortion.h>
#include <krate/dsp/processors/fractal_distortion.h>
#include <krate/dsp/processors/formant_distortion.h>
#include <krate/dsp/processors/granular_distortion.h>
#include <krate/dsp/processors/allpass_saturator.h>
#include <krate/dsp/primitives/sample_rate_reducer.h>
#include <krate/dsp/primitives/ring_saturation.h>
#include <krate/dsp/primitives/bitwise_mangler.h>
#include <krate/dsp/primitives/chaos_waveshaper.h>
#include <krate/dsp/primitives/stochastic_shaper.h>
#include "distortion_types.h"
#include "morph_node.h"

#include <variant>
#include <cmath>

namespace Krate::DSP {

/// @brief Unified interface for all distortion types.
/// Real-time safe: no allocations after prepare().
class DistortionAdapter {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;

        // Prepare all processor variants
        saturation_.prepare(sampleRate);
        tube_.prepare(sampleRate);
        tape_.prepare(sampleRate);
        fuzz_.prepare(sampleRate);
        wavefolder_.prepare(sampleRate);
        bitcrusher_.prepare(sampleRate);
        srReducer_.prepare(sampleRate);
        // New processors from DST-ROADMAP
        temporal_.prepare(sampleRate, 512);
        ringSaturation_.prepare(sampleRate);
        feedbackDist_.prepare(sampleRate, 512);
        aliasing_.prepare(sampleRate, 512);
        bitwiseMangler_.prepare(sampleRate);
        // All experimental types now implemented
        chaos_.prepare(sampleRate);
        formant_.prepare(sampleRate, 512);
        granular_.prepare(sampleRate, 512);
        spectral_.prepare(sampleRate, 2048);
        fractal_.prepare(sampleRate, 512);
        stochastic_.prepare(sampleRate);
        allpassSaturator_.prepare(sampleRate, 512);
    }

    void reset() noexcept {
        saturation_.reset();
        tube_.reset();
        tape_.reset();
        fuzz_.reset();
        wavefolder_.reset();
        bitcrusher_.reset();
        srReducer_.reset();
        temporal_.reset();
        ringSaturation_.reset();
        feedbackDist_.reset();
        aliasing_.reset();
        bitwiseMangler_.reset();
        chaos_.reset();
        formant_.reset();
        granular_.reset();
        spectral_.reset();
        fractal_.reset();
        stochastic_.reset();
        allpassSaturator_.reset();
    }

    void setType(DistortionType type) noexcept {
        currentType_ = type;

        // Configure the appropriate processor
        switch (type) {
            case DistortionType::SoftClip:
                saturation_.setType(SaturationProcessor::Type::Tape);
                break;
            case DistortionType::HardClip:
                saturation_.setType(SaturationProcessor::Type::Digital);
                break;
            case DistortionType::Tube:
                // TubeStage is ready to use
                break;
            case DistortionType::Tape:
                tape_.setModel(TapeSaturator::Model::Hysteresis);
                break;
            case DistortionType::Fuzz:
                fuzz_.setType(FuzzProcessor::Type::Germanium);
                break;
            case DistortionType::AsymmetricFuzz:
                fuzz_.setType(FuzzProcessor::Type::Silicon);
                break;
            case DistortionType::SineFold:
                wavefolder_.setModel(WavefolderProcessor::Model::Serge);
                break;
            case DistortionType::TriangleFold:
                wavefolder_.setModel(WavefolderProcessor::Model::Simple);
                break;
            case DistortionType::SergeFold:
                wavefolder_.setModel(WavefolderProcessor::Model::Lockhart);
                break;
            case DistortionType::Bitcrush:
            case DistortionType::Quantize:
                // BitcrusherProcessor handles both
                break;
            case DistortionType::SampleReduce:
                // SampleRateReducer is ready
                break;
            case DistortionType::Temporal:
                temporal_.setMode(TemporalDistortion::Mode::EnvelopeFollow);
                break;
            case DistortionType::RingSaturation:
                // RingSaturation is ready
                break;
            case DistortionType::FeedbackDist:
                // FeedbackDistortion is ready
                break;
            case DistortionType::Aliasing:
                // AliasingEffect is ready
                break;
            case DistortionType::BitwiseMangler:
                bitwiseMangler_.setOperation(BitwiseMangler::Operation::XorPattern);
                break;
            default:
                break;
        }
    }

    void setParams(const DistortionParams& params) noexcept {
        // Apply parameters to the current processor type
        switch (currentType_) {
            case DistortionType::SoftClip:
            case DistortionType::HardClip:
                saturation_.setDrive(params.drive);
                saturation_.setMix(params.mix);
                break;
            case DistortionType::Tube:
                tube_.setDrive(params.drive);
                tube_.setMix(params.mix);
                break;
            case DistortionType::Tape:
                tape_.setDrive(params.drive);
                tape_.setMix(params.mix);
                break;
            case DistortionType::Fuzz:
            case DistortionType::AsymmetricFuzz:
                fuzz_.setFuzz(params.drive / kMaxDrive);
                fuzz_.setBias(params.bias);
                fuzz_.setTone(params.tone / kMaxToneHz);
                break;
            case DistortionType::SineFold:
            case DistortionType::TriangleFold:
            case DistortionType::SergeFold:
                wavefolder_.setFolds(static_cast<int>(params.folds));
                wavefolder_.setDrive(params.drive);
                wavefolder_.setMix(params.mix);
                break;
            case DistortionType::Bitcrush:
            case DistortionType::Quantize:
                bitcrusher_.setBitDepth(params.bitDepth);
                bitcrusher_.setMix(params.mix);
                break;
            case DistortionType::SampleReduce:
                srReducer_.setFactor(static_cast<int>(params.srRatio));
                break;
            case DistortionType::Temporal:
                temporal_.setBaseDrive(params.drive);
                temporal_.setDriveModulation(params.sensitivity);
                temporal_.setAttackTime(params.attackMs);
                temporal_.setReleaseTime(params.releaseMs);
                break;
            case DistortionType::RingSaturation:
                ringSaturation_.setDrive(params.drive);
                ringSaturation_.setModulationDepth(params.modDepth);
                ringSaturation_.setStages(static_cast<int>(params.stages));
                break;
            case DistortionType::FeedbackDist:
                feedbackDist_.setDrive(params.drive);
                feedbackDist_.setDelayTime(params.delayMs);
                feedbackDist_.setFeedback(params.feedback);
                break;
            case DistortionType::Aliasing:
                aliasing_.setDownsampleFactor(params.srRatio);
                aliasing_.setFrequencyShift(params.freqShift);
                aliasing_.setMix(params.mix);
                break;
            case DistortionType::BitwiseMangler:
                bitwiseMangler_.setIntensity(params.drive / kMaxDrive);
                bitwiseMangler_.setRotateAmount(static_cast<int>(params.rotateAmount));
                break;
            default:
                break;
        }
    }

    [[nodiscard]] float process(float input) noexcept {
        switch (currentType_) {
            case DistortionType::SoftClip:
            case DistortionType::HardClip:
                return saturation_.process(input);
            case DistortionType::Tube:
                return tube_.process(input);
            case DistortionType::Tape:
                return tape_.process(input);
            case DistortionType::Fuzz:
            case DistortionType::AsymmetricFuzz:
                return fuzz_.process(input);
            case DistortionType::SineFold:
            case DistortionType::TriangleFold:
            case DistortionType::SergeFold:
                return wavefolder_.process(input);
            case DistortionType::Bitcrush:
            case DistortionType::Quantize:
                return bitcrusher_.process(input);
            case DistortionType::SampleReduce:
                return srReducer_.process(input);
            case DistortionType::FullRectify:
                return std::abs(input);
            case DistortionType::HalfRectify:
                return std::max(0.0f, input);
            // New types from DST-ROADMAP
            case DistortionType::Temporal:
                return temporal_.process(input);
            case DistortionType::RingSaturation:
                return ringSaturation_.process(input);
            case DistortionType::FeedbackDist:
                return feedbackDist_.process(input);
            case DistortionType::Aliasing:
                return aliasing_.process(input);
            case DistortionType::BitwiseMangler:
                return bitwiseMangler_.process(input);
            case DistortionType::Chaos:
                return chaos_.process(input);
            case DistortionType::Formant:
                return formant_.process(input);
            case DistortionType::Granular:
                return granular_.process(input);
            case DistortionType::Spectral:
                return spectral_.process(input);
            case DistortionType::Fractal:
                return fractal_.process(input);
            case DistortionType::Stochastic:
                return stochastic_.process(input);
            case DistortionType::AllpassResonant:
                return allpassSaturator_.process(input);
            default:
                return input;
        }
    }

private:
    double sampleRate_ = 44100.0;
    DistortionType currentType_ = DistortionType::SoftClip;

    // Original processor instances
    SaturationProcessor saturation_;
    TubeStage tube_;
    TapeSaturator tape_;
    FuzzProcessor fuzz_;
    WavefolderProcessor wavefolder_;
    BitcrusherProcessor bitcrusher_;
    SampleRateReducer srReducer_;

    // New processors from DST-ROADMAP
    TemporalDistortion temporal_;
    RingSaturation ringSaturation_;
    FeedbackDistortion feedbackDist_;
    AliasingEffect aliasing_;
    BitwiseMangler bitwiseMangler_;

    // Experimental processors (all implemented)
    ChaosWaveshaper chaos_;
    FormantDistortion formant_;
    GranularDistortion granular_;
    SpectralDistortion spectral_;
    FractalDistortion fractal_;
    StochasticShaper stochastic_;
    AllpassSaturator allpassSaturator_;
};

} // namespace Krate::DSP
```

### Crossover Network (Linkwitz-Riley 4th Order)

> **Note:** The crossover network uses the existing `CrossoverLR4` from FLT-ROADMAP (spec 079).
> Location: `dsp/include/krate/dsp/processors/crossover_filter.h`

```cpp
// plugins/Disrumpo/src/dsp/crossover_network.h
// Wraps existing CrossoverLR4 for multi-band use
#pragma once

#include <krate/dsp/processors/crossover_filter.h>
#include <array>
#include <vector>

namespace Krate::DSP {

/// @brief Multi-band crossover network for 1-8 bands.
/// Uses existing CrossoverLR4 from KrateDSP (FLT-ROADMAP spec 079).
class CrossoverNetwork {
public:
    void prepare(double sampleRate, int numBands) noexcept {
        sampleRate_ = sampleRate;
        numBands_ = std::clamp(numBands, kMinBands, kMaxBands);

        // N bands requires N-1 crossovers
        crossovers_.resize(numBands_ - 1);
        for (auto& xover : crossovers_) {
            xover.prepare(sampleRate);
        }
    }

    void setCrossoverFrequencies(const std::vector<float>& frequencies) noexcept {
        for (size_t i = 0; i < crossovers_.size() && i < frequencies.size(); ++i) {
            crossovers_[i].setCrossoverFrequency(frequencies[i]);
        }
    }

    /// @brief Process input and output to band buffers.
    void process(float input, std::array<float, kMaxBands>& bands) noexcept {
        if (numBands_ == 1) {
            bands[0] = input;
            return;
        }

        // Cascaded splitting: split input, then split each output
        float remaining = input;
        for (int i = 0; i < numBands_ - 1; ++i) {
            auto outputs = crossovers_[i].process(remaining);
            bands[i] = outputs.low;
            remaining = outputs.high;
        }
        bands[numBands_ - 1] = remaining;
    }

private:
    double sampleRate_ = 44100.0;
    int numBands_ = kDefaultBands;
    std::vector<CrossoverLR4> crossovers_;  // Uses existing KrateDSP component
};

} // namespace Krate::DSP
```

### Using Existing LFO for Modulation

```cpp
// Example: Using existing LFO in modulation engine
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/processors/envelope_follower.h>

namespace Krate::DSP {

class DisrumpoModulationEngine {
public:
    void prepare(double sampleRate) noexcept {
        lfo1_.prepare(sampleRate);
        lfo2_.prepare(sampleRate);
        envFollower_.prepare(sampleRate);

        // Configure LFO defaults per spec
        lfo1_.setFrequency(1.0f);  // 1 Hz default
        lfo1_.setWaveform(Waveform::Sine);

        lfo2_.setFrequency(0.5f);
        lfo2_.setWaveform(Waveform::Triangle);
        lfo2_.setTempoSync(true);
        lfo2_.setNoteValue(NoteValue::Quarter);
    }

    void setTempo(float bpm) noexcept {
        lfo1_.setTempo(bpm);
        lfo2_.setTempo(bpm);
    }

    /// @brief Process modulation sources and return values.
    void process(float audioInput,
                 float& lfo1Out, float& lfo2Out, float& envOut) noexcept {
        lfo1Out = lfo1_.process();
        lfo2Out = lfo2_.process();
        envOut = envFollower_.process(audioInput);
    }

private:
    LFO lfo1_;
    LFO lfo2_;
    EnvelopeFollower envFollower_;
};

} // namespace Krate::DSP
```

### Intelligent Oversampling Selection

```cpp
// Example: Per-band oversampling based on distortion type
#include <krate/dsp/primitives/oversampler.h>

namespace Krate::DSP {

/// @brief Get recommended oversampling factor for a distortion type.
constexpr int getRecommendedOversample(DistortionType type) noexcept {
    switch (type) {
        case DistortionType::SoftClip:
            return 2;  // Mild harmonics
        case DistortionType::HardClip:
        case DistortionType::Fuzz:
        case DistortionType::AsymmetricFuzz:
            return 4;  // Strong harmonics
        case DistortionType::SineFold:
        case DistortionType::TriangleFold:
        case DistortionType::SergeFold:
            return 4;  // Many harmonics from folding
        case DistortionType::FullRectify:
        case DistortionType::HalfRectify:
            return 4;  // Frequency doubling
        case DistortionType::Bitcrush:
        case DistortionType::SampleReduce:
        case DistortionType::Quantize:
            return 1;  // Aliasing is intentional
        // New types from DST-ROADMAP
        case DistortionType::Temporal:
            return 2;  // Moderate harmonics
        case DistortionType::RingSaturation:
            return 4;  // Inharmonic sidebands
        case DistortionType::FeedbackDist:
            return 2;  // Controlled by limiter
        case DistortionType::Aliasing:
        case DistortionType::BitwiseMangler:
            return 1;  // Artifacts ARE the effect
        case DistortionType::Chaos:
        case DistortionType::Formant:
        case DistortionType::Granular:
        case DistortionType::Fractal:
        case DistortionType::Stochastic:
            return 2;  // Moderate, varies by settings
        case DistortionType::Spectral:
            return 1;  // FFT domain, no aliasing
        case DistortionType::AllpassResonant:
            return 4;  // Self-oscillation potential
        default:
            return 2;
    }
}

/// @brief Calculate weighted oversampling for morph position.
int calculateMorphOversample(const std::array<MorphNode, 4>& nodes,
                              int activeCount,
                              const std::array<float, 4>& weights,
                              int maxOversample) noexcept {
    float weightedSum = 0.0f;
    for (int i = 0; i < activeCount; ++i) {
        int recommended = getRecommendedOversample(nodes[i].type);
        weightedSum += weights[i] * static_cast<float>(recommended);
    }

    int result = static_cast<int>(std::ceil(weightedSum));

    // Round up to power of 2 and clamp to max
    if (result <= 1) return 1;
    if (result <= 2) return std::min(2, maxOversample);
    if (result <= 4) return std::min(4, maxOversample);
    return maxOversample;
}

} // namespace Krate::DSP
```
