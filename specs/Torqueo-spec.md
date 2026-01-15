# Torqueo - Multiband Morphing Distortion Plugin
## Software Requirements Specification & Development Roadmap

| Field | Value |
|-------|-------|
| Project | Torqueo |
| Version | 1.0 |
| Date | January 2026 |
| Platforms | Windows, macOS, Linux |
| Format | VST3 |
| Framework | VST3 SDK, VSTGUI |

---

## 1. Executive Summary

Torqueo is a multiband morphing distortion plugin for synthesizer processing and sound design.

### Key Differentiators
- Per-band morphable distortion with multi-point morph spaces (A↔B↔C↔D)
- Sweeping distortion traversing the frequency spectrum
- Morph-frequency linking - distortion character changes with sweep position
- Intelligent per-band oversampling based on active distortion types
- Progressive disclosure UI scaling from simple to expert
- Deep modulation architecture with LFOs, envelope followers, and flexible routing

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

> **Note**: This section analyzes which Torqueo requirements are already implemented in the KrateDSP library.

### Distortion Types (FR-DIST-001)

| Torqueo ID | Type | KrateDSP Component | Status |
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

### Supporting Components

| Component | KrateDSP Location | Status |
|-----------|------------------|--------|
| Crossover Filters | `Biquad`, `BiquadCascade` | **EXISTS** (LR4 composable) |
| Oversampler | `Oversampler` (2x/4x, IIR/FIR) | **EXISTS** |
| LFO | `LFO` (6 waveforms, tempo sync) | **EXISTS** |
| Envelope Follower | `EnvelopeFollower` (3 modes) | **EXISTS** |
| Parameter Smoother | `OnePoleSmoother`, `LinearRamp`, `SlewLimiter` | **EXISTS** |
| DC Blocker | `DCBlocker` | **EXISTS** |

### Implementation Summary

- **12 of 14 distortion types** have dedicated implementations
- **2 distortion types** (rectification) are trivially composable
- **All modulation sources** have existing implementations
- **Crossover network** requires composition from existing Biquad primitives

---

## 2. Product Vision

### Problem Statement
Existing distortion plugins offer either:
- Single distortion type with parameter control
- Multiband processing with fixed distortion per band
- Limited morphing between two states

No current solution provides fluid morphing across multiple distortion types per band, combined with frequency-sweeping distortion that evolves as it moves through the spectrum.

### Solution
Torqueo provides a unified environment where:
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

**FR-DIST-002: Common Parameters**
- Drive: 0.0 to 10.0
- Mix: 0% to 100%
- Tone: 200 Hz to 8000 Hz

**FR-DIST-003: Type-Specific Parameters**

Saturation: Bias (-1.0 to +1.0), Sag (0.0 to 1.0)
Wavefold: Folds (1-8), Shape (0.0-1.0), Symmetry (0.0-1.0)
Digital: Bit Depth (1.0-16.0), Sample Rate Ratio (1.0-64.0), Smoothness (0.0-1.0)

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
| Digital | 1x | Aliasing intentional |

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
│                    Torqueo Plugin                        │
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
│  TORQUEO                               [≡ Menu] [? Help]   │
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
// torqueo/src/plugin_ids.h
#pragma once

#include "pluginterfaces/vst/vsttypes.h"

namespace Torqueo {

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

} // namespace Torqueo
```

### Core Structures

Following KrateDSP conventions (`Krate::DSP` namespace, `kConstantName` constants):

```cpp
// dsp/include/krate/dsp/systems/torqueo/distortion_types.h
#pragma once

#include <cstdint>

namespace Krate::DSP {

/// @brief Distortion algorithm types for Torqueo morph engine.
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

    // Digital (use BitcrusherProcessor, SampleRateReducer)
    Bitcrush,           ///< BitcrusherProcessor
    SampleReduce,       ///< SampleRateReducer
    Quantize,           ///< BitCrusher quantization
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
// dsp/include/krate/dsp/systems/torqueo/morph_node.h
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
    float srRatio = 1.0f;           ///< Sample rate reduction [1, 64]
    float smoothness = 0.0f;        ///< Anti-aliasing smoothness [0, 1]
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
// dsp/include/krate/dsp/systems/torqueo/modulation_types.h
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

### File Format (JSON)
```json
{
  "torqueo_preset": {
    "version": 1,
    "meta": {
      "name": "Sweeping Destruction",
      "author": "Factory",
      "tags": ["sweep", "morph", "aggressive"]
    },
    "global": { "inputGain": 0.0, "outputGain": -3.0, "globalMix": 1.0 },
    "bands": [{
      "lowFreq": 20, "highFreq": 200,
      "morphMode": "linear1d", "morphX": 0.3,
      "nodes": [
        { "type": "fuzz", "params": { "drive": 5.0, "mix": 1.0 } },
        { "type": "bitcrush", "params": { "bitDepth": 6 } }
      ]
    }],
    "sweep": { "enabled": true, "frequency": 500, "morphLink": "linear" },
    "modulation": {
      "routings": [
        { "source": "lfo1", "destination": "sweep.frequency", "amount": 0.7 }
      ]
    }
  }
}
```

### Factory Preset Categories
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
| **Total** | **90** |

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
│   └── torqueo/                           # Torqueo VST3 plugin
│       ├── CMakeLists.txt
│       ├── src/
│       │   ├── entry.cpp                  # VST3 entry point
│       │   ├── plugin_ids.h               # Parameter ID definitions
│       │   ├── version.h                  # Plugin version info
│       │   ├── dsp/                       # PLUGIN-SPECIFIC DSP composition
│       │   │   ├── distortion_types.h     # Torqueo distortion enum
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
│           ├── presets/factory/           # Factory presets (JSON)
│           └── installers/                # Platform installers
│
└── specs/
    └── torqueo/                           # Feature specifications
        ├── spec.md                        # Requirements
        ├── plan.md                        # Implementation plan
        └── tasks.md                       # Task breakdown
```

### Key Architecture Notes

1. **DSP library is plugin-agnostic**: KrateDSP contains only generic, reusable building blocks. No plugin-specific code lives there.

2. **Plugin-specific DSP in plugin folder**: Torqueo-specific composition (morph engine, distortion adapter, band processor) lives in `plugins/torqueo/src/dsp/`. This follows the same pattern as Iterum.

3. **Compose existing processors**: The `DistortionAdapter` in the plugin composes existing KrateDSP processors (`SaturationProcessor`, `TubeStage`, `FuzzProcessor`, `WavefolderProcessor`, `BitcrusherProcessor`).

4. **Generic components may be promoted**: If `CrossoverNetwork` proves useful to multiple plugins, it can be added to `dsp/include/krate/dsp/systems/`. Start in the plugin, promote when reuse is proven.

5. **Test hierarchy**:
   - `dsp/tests/`: Tests for generic KrateDSP components
   - `plugins/torqueo/tests/`: Tests for Torqueo-specific composition

---

## Appendix C: Composing Existing KrateDSP Processors

Example code showing how to use existing KrateDSP components for Torqueo:

### Distortion Adapter Pattern

```cpp
// dsp/include/krate/dsp/systems/torqueo/distortion_adapter.h
#pragma once

#include <krate/dsp/processors/saturation_processor.h>
#include <krate/dsp/processors/tube_stage.h>
#include <krate/dsp/processors/tape_saturator.h>
#include <krate/dsp/processors/fuzz_processor.h>
#include <krate/dsp/processors/wavefolder_processor.h>
#include <krate/dsp/processors/bitcrusher_processor.h>
#include <krate/dsp/primitives/sample_rate_reducer.h>
#include "distortion_types.h"
#include "morph_node.h"

#include <variant>
#include <cmath>

namespace Krate::DSP {

/// @brief Unified interface for all distortion types using std::variant.
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
    }

    void reset() noexcept {
        saturation_.reset();
        tube_.reset();
        tape_.reset();
        fuzz_.reset();
        wavefolder_.reset();
        bitcrusher_.reset();
        srReducer_.reset();
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
                fuzz_.setFuzz(params.drive / kMaxDrive);  // Normalize to [0,1]
                fuzz_.setBias(params.bias);
                fuzz_.setTone(params.tone / kMaxToneHz);  // Normalize
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
            default:
                return input;
        }
    }

private:
    double sampleRate_ = 44100.0;
    DistortionType currentType_ = DistortionType::SoftClip;

    // All processor instances (only one active at a time)
    SaturationProcessor saturation_;
    TubeStage tube_;
    TapeSaturator tape_;
    FuzzProcessor fuzz_;
    WavefolderProcessor wavefolder_;
    BitcrusherProcessor bitcrusher_;
    SampleRateReducer srReducer_;
};

} // namespace Krate::DSP
```

### Crossover Network (Linkwitz-Riley 4th Order)

```cpp
// dsp/include/krate/dsp/systems/torqueo/crossover_network.h
#pragma once

#include <krate/dsp/primitives/biquad.h>
#include <array>
#include <vector>

namespace Krate::DSP {

/// @brief Linkwitz-Riley 4th order crossover using cascaded Biquads.
/// LR4 = two cascaded Butterworth 2nd-order filters.
class LinkwitzRileyCrossover {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        for (auto& lpf : lowpass_) lpf.prepare(sampleRate);
        for (auto& hpf : highpass_) hpf.prepare(sampleRate);
    }

    void reset() noexcept {
        for (auto& lpf : lowpass_) lpf.reset();
        for (auto& hpf : highpass_) hpf.reset();
    }

    void setCrossoverFrequency(float freqHz) noexcept {
        crossoverFreq_ = freqHz;
        const float q = 0.7071f;  // Butterworth Q

        // Two cascaded 2nd-order Butterworth = 4th-order LR
        lowpass_[0].setLowpass(freqHz, q);
        lowpass_[1].setLowpass(freqHz, q);
        highpass_[0].setHighpass(freqHz, q);
        highpass_[1].setHighpass(freqHz, q);
    }

    /// @brief Split input into low and high bands.
    /// @param input Input sample
    /// @param lowOut Output for low band (below crossover)
    /// @param highOut Output for high band (above crossover)
    void process(float input, float& lowOut, float& highOut) noexcept {
        // Cascade two filters for LR4 response
        float low = lowpass_[0].process(input);
        lowOut = lowpass_[1].process(low);

        float high = highpass_[0].process(input);
        highOut = highpass_[1].process(high);
    }

private:
    double sampleRate_ = 44100.0;
    float crossoverFreq_ = 1000.0f;
    std::array<Biquad, 2> lowpass_;   // Cascaded for LR4
    std::array<Biquad, 2> highpass_;
};

/// @brief Multi-band crossover network for 1-8 bands.
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
            float low, high;
            crossovers_[i].process(remaining, low, high);
            bands[i] = low;
            remaining = high;
        }
        bands[numBands_ - 1] = remaining;
    }

private:
    double sampleRate_ = 44100.0;
    int numBands_ = kDefaultBands;
    std::vector<LinkwitzRileyCrossover> crossovers_;
};

} // namespace Krate::DSP
```

### Using Existing LFO for Modulation

```cpp
// Example: Using existing LFO in modulation engine
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/processors/envelope_follower.h>

namespace Krate::DSP {

class TorqueoModulationEngine {
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
